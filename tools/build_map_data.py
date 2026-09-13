#!/usr/bin/env python3
"""Convert the raw StarRupture map exports into the compact viewer catalog.

Inputs (see analyse_map/readme.md):

    analyse_map/map_v2_resources.jsonl    385k resource instances / points / actors
    analyse_map/map_v2_placements.jsonl   22k buildings, volumes and technical actors
    analyse_map/map_v2_pois.geojson       241 canonical points of interest

Output: mapview/public/map-data/*.js

The viewer is opened directly from the file system, where `fetch()` on a
`file://` URL is blocked by browsers. Classic `<script>` tags are not, so every
part is written as a tiny JSONP wrapper (`SRMAPDATA(<json>)`) around otherwise
plain compact JSON. The files stay next to `MapExtensionViewer.html` exactly
like `map-tiles/`, and are never inlined into the HTML.

Coordinates are stored in decimetres (world centimetres / 10) because the map
image resolution is roughly 48 cm per pixel; the viewer converts them to map
space with the same projection constants as `map_state_types.h`.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import time
from collections import defaultdict
from collections.abc import Iterator
from pathlib import Path
from typing import Any

# ── Format constants ──────────────────────────────────────────────────────────

CATALOG_VERSION = 1
JSONP_CALLBACK = "SRMAPDATA"
POSITION_SCALE_CM = 10.0  # stored unit = 1 decimetre
ALTITUDE_SCALE_CM = 100.0  # altitudes stored in metres
SORT_CELL = 2000  # 200 m locality blocks, keeps delta values small
RESOURCE_KINDS = ("pcg", "actor", "deposit")
RAW_KIND_TO_KIND = {
    "resource_point": "pcg",
    "resource_actor": "actor",
}
ORE_PURITY_LEVELS = ("unknown", "impure", "normal", "pure")
ORE_LOOKUP_CELL_CM = 5000.0
ORE_LOOKUP_MAX_RADIUS = 20
ORE_SOCKET_RESOURCE_MAX_CM = 2500.0
ORE_SOCKET_PURITY_MAX_CM = 8000.0
GENERIC_ORE_SOCKET_RESOURCES = {"titanium", "wolfram", "calcium"}
ORE_SOCKET_RESOURCES = {
    "BP_GoethiteOreSocket_C": "goethite",
    "BP_SulphurSocket_C": "sulphur",
    "BP_HeliumSocket_C": "helium_3",
}

# How the purity of a vein was established, worst last. `exact` is read straight
# from the only physical material the resource has; the others come from joining
# the socket to the nearest exported collision anchor, because the collision
# triangles the game ray-traces are absent from the export.
ORE_JOIN_CONFIDENCE = ("exact", "high", "medium", "low")

# Radius under which an `actor` or `pcg` point describes the same physical vein
# as a `deposit`. 5 m in decimetres; the coinciding pairs are exact to the
# centimetre, the PCG anchors sit a few metres off.
DUPLICATE_MERGE_DM = 50

# Resource ids never published. `unknown_ore` was 389 `BP_OreSocket` anchors
# that no export identifies: listing them as an ore states something false.
EXCLUDED_RESOURCE_TYPES = frozenset({"unknown_ore"})

# Extractor buildable per ore. The identifiers and French names come from the
# vein dataset; only the English side needs a table here.
EXTRACTOR_LABELS_EN = {
    "MechanicalDrill": "Mechanical Drill",
    "LaserDrill": "Laser Drill",
    "GasExtractor": "Gas Extractor",
    "AcidExtractor": "Acid Extractor",
}

# Placement category -> (layer, group). Groups drive the filter tree.
PLACEMENT_LAYERS: dict[str, tuple[str, str]] = {
    "abandoned_base": ("building", "abandoned_base"),
    "abandoned_base_support": ("building", "abandoned_base"),
    "forgotten_engine": ("building", "forgotten_engine"),
    "antenna": ("building", "antenna"),
    "alien_obelisk": ("building", "obelisk"),
    "monument": ("building", "monument"),
    "orbital_lander": ("building", "orbital_lander"),
    "mineral_or_meteor_actor": ("building", "ore_actor"),
    "resource_actor": ("building", "ore_actor"),
    "cave": ("building", "cave"),
    "resource_spawn_zone": ("zone", "spawn_zone"),
    "resource_exclusion_zone": ("zone", "exclusion_zone"),
    "world_spawn_region": ("zone", "spawn_region"),
    "resource_spawn_marker": ("technical", "resource_marker"),

    "point_of_interest": ("technical", "poi_proxy"),
}

POI_TYPE_TO_GROUP = {
    "ECrPointOfInterestType::Cave": "cave",
    "ECrPointOfInterestType::Obelisk": "obelisk",
    "ECrPointOfInterestType::Antena": "antenna",
    "ECrPointOfInterestType::AbandonedBase": "abandoned_base",
    "ECrPointOfInterestType::ForgottenEngine": "forgotten_engine",
    "ECrPointOfInterestType::OrbitalLander": "orbital_lander",
}

# Canonical resource ids are derived from the export. Display names come from
# the game item tables embedded in the dump (`item.item_name`), so the catalog
# matches what the player reads in game; the tables below only cover resources
# whose export carries no item metadata, plus the few names the item table gets
# wrong ("Polufruit").
RESOURCE_ALIASES = {
    "golden_balloon_skylisk": "skylisk",
    "gold_fruit_tree_lowlands": "gold_fruit",
    "gold_fruit_tree_mountains": "gold_fruit",
    "purplant_large": "purplant",
    "purplant_small": "purplant",
    "sulphur_large": "sulphur",
    "sulphur_small": "sulphur",
}

RESOURCE_LABELS_EN = {
    "unknown_ore": "Unknown Ore",
    "nootka_lupine": "Nootka Lupine",
    "thornfruit": "Thornfruit",
    "sikkim_rhubarb": "Sikkim Rhubarb",
    "aggressive_plant": "Aggressive Plant",
    "gold_fruit": "Gold Fruit",
    # The item table spells it "Polufruit"; the in-game UI and the wiki do not.
    "polifruit": "Polifruit",
    # `soulheart` is the asset id, "Sulheart" the displayed name.
    "soulheart": "Sulheart",
}

RESOURCE_LABELS_FR = {
    "unknown_ore": "Minerai inconnu",
    "nootka_lupine": "Lupin de Nootka",
    "thornfruit": "Ronce-fruit",
    "sikkim_rhubarb": "Rhubarbe du Sikkim",
    "aggressive_plant": "Plante agressive",
    "gold_fruit": "Fruit d'or",
    "polifruit": "Polyfruit",
    "soulheart": "Cœur d'âme",
    # The item table leaves the English name in the French column.
    "skylisk": "Viande de skylisk",
}

CATEGORY_ALIASES = {"creature_resource": "creature"}


# ── Helpers ───────────────────────────────────────────────────────────────────


def humanize(identifier: str) -> str:
    return " ".join(part.capitalize() for part in identifier.split("_") if part)


CAMEL_BOUNDARY = re.compile(r"(?<=[a-z0-9])(?=[A-Z])")
NON_ALNUM = re.compile(r"[^A-Za-z0-9]+")


def canonical_resource_id(raw: str | None) -> str:
    value = (raw or "unknown").replace("/", " ")
    if value.startswith("PGS_"):
        value = value[4:]
    for suffix in ("_Fruit", "_Meat"):
        if value.endswith(suffix):
            value = value[: -len(suffix)]
    value = CAMEL_BOUNDARY.sub("_", value)
    value = NON_ALNUM.sub("_", value).strip("_").lower()
    return RESOURCE_ALIASES.get(value, value or "unknown")


def read_jsonl(path: Path) -> Iterator[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                yield json.loads(line)


def quantize(value: float | None, scale: float) -> int:
    if value is None or not math.isfinite(value):
        return 0
    return int(round(value / scale))


def sort_key(point: tuple[int, ...]) -> tuple[int, int, int, int]:
    x, y = point[0], point[1]
    return (y // SORT_CELL, x // SORT_CELL, x, y)


def ore_purity(static_mesh_path: str | None) -> str:
    """Return an explicit ore purity token, without guessing ambiguous meshes."""
    separated = CAMEL_BOUNDARY.sub("_", static_mesh_path or "")
    tokens = {token.lower() for token in NON_ALNUM.split(separated) if token}
    matches = [level for level in ORE_PURITY_LEVELS[1:] if level in tokens]
    return matches[0] if len(matches) == 1 else "unknown"


def nearest_ore(
    cells: dict[tuple[int, int], list[tuple[float, float, str]]],
    x_cm: float,
    y_cm: float,
) -> tuple[float, str] | None:
    cell_x = math.floor(x_cm / ORE_LOOKUP_CELL_CM)
    cell_y = math.floor(y_cm / ORE_LOOKUP_CELL_CM)
    best: tuple[float, str] | None = None
    for radius in range(ORE_LOOKUP_MAX_RADIUS + 1):
        for offset_x in range(-radius, radius + 1):
            for offset_y in range(-radius, radius + 1):
                if radius and max(abs(offset_x), abs(offset_y)) != radius:
                    continue
                for ore_x, ore_y, resource_id in cells.get((cell_x + offset_x, cell_y + offset_y), []):
                    distance_squared = (ore_x - x_cm) ** 2 + (ore_y - y_cm) ** 2
                    if best is None or distance_squared < best[0]:
                        best = (distance_squared, resource_id)
        if best is not None and math.sqrt(best[0]) <= max(1, radius) * ORE_LOOKUP_CELL_CM:
            break
    return best


def nearest_ore_purity(
    cells: dict[tuple[str, int, int], list[tuple[float, float, str]]],
    resource_id: str,
    x_cm: float,
    y_cm: float,
) -> tuple[float, str] | None:
    cell_x = math.floor(x_cm / ORE_LOOKUP_CELL_CM)
    cell_y = math.floor(y_cm / ORE_LOOKUP_CELL_CM)
    best: tuple[float, str] | None = None
    for radius in range(ORE_LOOKUP_MAX_RADIUS + 1):
        for offset_x in range(-radius, radius + 1):
            for offset_y in range(-radius, radius + 1):
                if radius and max(abs(offset_x), abs(offset_y)) != radius:
                    continue
                key = (resource_id, cell_x + offset_x, cell_y + offset_y)
                for ore_x, ore_y, purity in cells.get(key, []):
                    distance_squared = (ore_x - x_cm) ** 2 + (ore_y - y_cm) ** 2
                    if best is None or distance_squared < best[0]:
                        best = (distance_squared, purity)
        if best is not None and math.sqrt(best[0]) <= max(1, radius) * ORE_LOOKUP_CELL_CM:
            break
    return best


def delta_encode(points: list[tuple[int, ...]]) -> dict[str, list[int]]:
    xs: list[int] = []
    ys: list[int] = []
    zs: list[int] = []
    previous_x = previous_y = previous_z = 0
    for point in points:
        x, y, z = point[0], point[1], point[2]
        xs.append(x - previous_x)
        ys.append(y - previous_y)
        zs.append(z - previous_z)
        previous_x, previous_y, previous_z = x, y, z
    return {"x": xs, "y": ys, "z": zs}


def write_part(directory: Path, name: str, payload: dict[str, Any]) -> int:
    path = directory / f"{name}.js"
    body = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
    path.write_text(f"{JSONP_CALLBACK}({body});\n", encoding="utf-8")
    return path.stat().st_size


def log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


# ── Resources ─────────────────────────────────────────────────────────────────


def read_ore_veins(
    path: Path,
    buckets: dict[tuple[str, str, str], list[tuple[int, int, int, int, int]]],
    type_category: dict[str, str],
    type_extractor: dict[str, str],
    extractors: dict[str, dict[str, Any]],
) -> int:
    """Publish the extractor deposits from the dedicated ore vein export.

    Every position is an exact serialized socket transform. Resource and purity
    are authoritative for the three specialized socket types, and joined to the
    nearest exported collision anchor for the generic `BP_OreSocket` ones; the
    export states its own confidence, which is kept per point so the viewer can
    show which veins rest on an inferred join.
    """

    published = 0
    for entry in read_jsonl(path):
        position = entry.get("position_cm") or {}
        x_cm = position.get("x")
        y_cm = position.get("y")
        resource_id = entry.get("resource_id")
        if x_cm is None or y_cm is None or not resource_id:
            continue

        if resource_id in EXCLUDED_RESOURCE_TYPES:
            continue

        purity = entry.get("purity") or "unknown"
        if purity not in ORE_PURITY_LEVELS:
            purity = "unknown"
        confidence = (entry.get("purity_assignment") or {}).get("confidence")
        if confidence not in ORE_JOIN_CONFIDENCE:
            confidence = "medium"

        type_category.setdefault(resource_id, "mineral")
        buckets[("mineral", resource_id, "deposit")].append(
            (
                quantize(x_cm, POSITION_SCALE_CM),
                quantize(y_cm, POSITION_SCALE_CM),
                quantize(position.get("z"), ALTITUDE_SCALE_CM),
                ORE_PURITY_LEVELS.index(purity),
                ORE_JOIN_CONFIDENCE.index(confidence),
            )
        )

        extractor_id = entry.get("extractor_id")
        if extractor_id:
            type_extractor.setdefault(resource_id, extractor_id)
            extractor = extractors.setdefault(
                extractor_id,
                {
                    "en": EXTRACTOR_LABELS_EN.get(extractor_id, humanize(extractor_id)),
                    "fr": entry.get("extractor_name_fr") or extractor_id,
                    "count": 0,
                    "resources": [],
                },
            )
            extractor["count"] += 1
            if resource_id not in extractor["resources"]:
                extractor["resources"].append(resource_id)

        published += 1

    for extractor in extractors.values():
        extractor["resources"].sort()

    return published


def drop_duplicate_representations(
    buckets: dict[tuple[str, str, str], list[tuple[int, int, int, int, int]]],
) -> int:
    """Drop the `actor` and `pcg` points that sit on top of a `deposit`.

    The export describes the same physical vein up to three times: goethite,
    helium-3 and sulphur publish an actor at the exact socket coordinates, and
    sulphur adds a PCG anchor a few metres away. Publishing all three counted the
    ore two or three times in the filters, painted a flat dot over the middle of
    the quality-coded deposit marker, and let a click land on whichever series
    happened to be nearest. The deposit wins: it alone carries purity and
    extractor.
    """

    dropped = 0
    for category, type_id, kind in list(buckets):
        if kind == "deposit":
            continue
        deposits = buckets.get((category, type_id, "deposit"))
        if not deposits:
            continue

        cells: dict[tuple[int, int], list[tuple[int, ...]]] = defaultdict(list)
        for point in deposits:
            cells[(point[0] // DUPLICATE_MERGE_DM, point[1] // DUPLICATE_MERGE_DM)].append(point)

        kept = []
        for point in buckets[(category, type_id, kind)]:
            cell_x = point[0] // DUPLICATE_MERGE_DM
            cell_y = point[1] // DUPLICATE_MERGE_DM
            near = any(
                abs(other[0] - point[0]) <= DUPLICATE_MERGE_DM
                and abs(other[1] - point[1]) <= DUPLICATE_MERGE_DM
                for offset_x in (-1, 0, 1)
                for offset_y in (-1, 0, 1)
                for other in cells.get((cell_x + offset_x, cell_y + offset_y), ())
            )
            if near:
                dropped += 1
            else:
                kept.append(point)
        buckets[(category, type_id, kind)] = kept

    return dropped


def build_resources(
    path: Path,
    placements_path: Path,
    ore_veins_path: Path | None = None,
) -> tuple[dict[str, list[dict[str, Any]]], dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    """Return per-category delta-encoded groups, the resource type table and the
    extractor table.

    Extractor deposits come from `ore_veins_path` when that export exists: it
    carries the serialized socket transform plus a documented resource/purity
    attribution, where this script can only join sockets to nearby meshes and
    leaves three quarters of them as `unknown` quality.
    """

    # (category, type, kind) -> quantized points plus ore purity and join
    # confidence codes.
    buckets: dict[tuple[str, str, str], list[tuple[int, int, int, int, int]]] = defaultdict(list)
    type_labels: dict[str, dict[str, str]] = {}
    type_items: dict[str, dict[str, str]] = {}
    type_category: dict[str, str] = {}
    ore_cells: dict[tuple[int, int], list[tuple[float, float, str]]] = defaultdict(list)
    purity_cells: dict[tuple[str, int, int], list[tuple[float, float, str]]] = defaultdict(list)
    total = 0
    hism_indexed = 0
    hism_dropped = 0

    for entry in read_jsonl(path):
        total += 1
        raw_kind = entry.get("kind")
        kind = RAW_KIND_TO_KIND.get(raw_kind) if isinstance(raw_kind, str) else None
        is_hism = raw_kind == "resource_hism_instance"
        if kind is None and not is_hism:
            continue

        position = entry.get("position_cm") or {}
        x_cm = position.get("x")
        y_cm = position.get("y")
        if x_cm is None or y_cm is None:
            continue

        category = CATEGORY_ALIASES.get(entry.get("category") or "unknown", entry.get("category") or "unknown")
        type_id = canonical_resource_id(entry.get("resource_id"))
        if type_id in EXCLUDED_RESOURCE_TYPES:
            continue

        item_name = ((entry.get("item") or {}).get("item_name")) or {}
        if type_id not in type_items and item_name.get("source"):
            type_items[type_id] = {
                "en": item_name["source"].strip(),
                "fr": (item_name.get("localized") or item_name["source"]).strip(),
            }
        localized = (entry.get("display_name") or "").strip()
        if localized and type_id not in type_labels:
            type_labels[type_id] = {"fr": localized}

        # HISM meshes are hand-mineable rocks, not the extractor deposits shown
        # in the viewer. Keep only a lightweight spatial index so each ore socket
        # can inherit its resource and any explicit nearby purity marker.
        if is_hism:
            if category != "mineral":
                hism_dropped += 1
                continue
            cell_x = math.floor(x_cm / ORE_LOOKUP_CELL_CM)
            cell_y = math.floor(y_cm / ORE_LOOKUP_CELL_CM)
            ore_cells[(cell_x, cell_y)].append((x_cm, y_cm, type_id))
            purity = ore_purity(entry.get("static_mesh_path"))
            if purity != "unknown":
                purity_cells[(type_id, cell_x, cell_y)].append((x_cm, y_cm, purity))
            hism_indexed += 1
            continue

        type_category.setdefault(type_id, category)
        point = (
            quantize(x_cm, POSITION_SCALE_CM),
            quantize(y_cm, POSITION_SCALE_CM),
            quantize(position.get("z"), ALTITUDE_SCALE_CM),
            ORE_PURITY_LEVELS.index("unknown"),
            ORE_JOIN_CONFIDENCE.index("exact"),
        )
        buckets[(category, type_id, kind)].append(point)

    extractors: dict[str, dict[str, Any]] = {}
    type_extractor: dict[str, str] = {}
    veins_path = (
        ore_veins_path
        if ore_veins_path is not None and ore_veins_path.exists()
        else None
    )

    if veins_path is not None:
        socket_count = read_ore_veins(
            veins_path, buckets, type_category, type_extractor, extractors
        )
        source_note = f"published from {veins_path.name}"
    else:
        socket_count = 0
        for entry in read_jsonl(placements_path):
            if entry.get("category") != "resource_deposit_socket":
                continue
            position = entry.get("position_cm") or {}
            x_cm = position.get("x")
            y_cm = position.get("y")
            if x_cm is None or y_cm is None:
                continue

            resource_id = ORE_SOCKET_RESOURCES.get(entry.get("actor_type") or "")
            if resource_id is None:
                nearest = nearest_ore(ore_cells, x_cm, y_cm)
                if (
                    nearest is None
                    or math.sqrt(nearest[0]) > ORE_SOCKET_RESOURCE_MAX_CM
                    or nearest[1] not in GENERIC_ORE_SOCKET_RESOURCES
                ):
                    resource_id = "unknown_ore"
                else:
                    resource_id = nearest[1]

            purity = "unknown"
            nearest_purity = nearest_ore_purity(purity_cells, resource_id, x_cm, y_cm)
            if (
                nearest_purity is not None
                and math.sqrt(nearest_purity[0]) <= ORE_SOCKET_PURITY_MAX_CM
            ):
                purity = nearest_purity[1]

            if resource_id in EXCLUDED_RESOURCE_TYPES:
                continue

            type_category.setdefault(resource_id, "mineral")
            buckets[("mineral", resource_id, "deposit")].append(
                (
                    quantize(x_cm, POSITION_SCALE_CM),
                    quantize(y_cm, POSITION_SCALE_CM),
                    quantize(position.get("z"), ALTITUDE_SCALE_CM),
                    ORE_PURITY_LEVELS.index(purity),
                    ORE_JOIN_CONFIDENCE.index("medium"),
                )
            )
            socket_count += 1
        source_note = "published from mesh proximity (ore vein export absent)"

    duplicates = drop_duplicate_representations(buckets)

    log(
        f"resources: {total} rows, {hism_indexed} mineral HISM instances indexed but not published, "
        f"{hism_dropped} plant HISM instances excluded, {duplicates} duplicate vein points merged, "
        f"{socket_count} extractor deposits {source_note}"
    )

    groups_by_category: dict[str, list[dict[str, Any]]] = defaultdict(list)
    counts: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    purity_counts: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    confidence_counts: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))

    for (category, type_id, kind), points in sorted(buckets.items()):
        if not points:
            continue
        points.sort(key=sort_key)
        encoded = delta_encode(points)
        group: dict[str, Any] = {
            "t": type_id,
            "k": RESOURCE_KINDS.index(kind),
            "n": len(points),
            **encoded,
        }
        if kind == "deposit":
            group["p"] = [point[3] for point in points]
            # Join confidence rides along only when at least one vein is not
            # exact, so an export without inferred joins stays byte-identical.
            confidences = [point[4] for point in points]
            if any(code != ORE_JOIN_CONFIDENCE.index("exact") for code in confidences):
                group["c"] = confidences
            for point in points:
                purity_counts[type_id][ORE_PURITY_LEVELS[point[3]]] += 1
                confidence_counts[type_id][ORE_JOIN_CONFIDENCE[point[4]]] += 1
        groups_by_category[category].append(group)
        counts[type_id][kind] += len(points)

    types: dict[str, dict[str, Any]] = {}
    for type_id, kind_counts in counts.items():
        item = type_items.get(type_id, {})
        english = RESOURCE_LABELS_EN.get(type_id) or item.get("en") or humanize(type_id)
        french = (
            RESOURCE_LABELS_FR.get(type_id)
            or item.get("fr")
            or type_labels.get(type_id, {}).get("fr")
            or english
        )
        entry_types: dict[str, Any] = {
            "category": type_category.get(type_id, "unknown"),
            "en": english,
            "fr": french,
            "counts": {kind: kind_counts.get(kind, 0) for kind in RESOURCE_KINDS if kind_counts.get(kind)},
            "total": sum(kind_counts.values()),
        }
        if purity_counts.get(type_id):
            entry_types["purity_counts"] = {
                level: purity_counts[type_id].get(level, 0)
                for level in ORE_PURITY_LEVELS
                if purity_counts[type_id].get(level)
            }
        if confidence_counts.get(type_id):
            entry_types["purity_confidence_counts"] = {
                level: confidence_counts[type_id].get(level, 0)
                for level in ORE_JOIN_CONFIDENCE
                if confidence_counts[type_id].get(level)
            }
        if type_extractor.get(type_id):
            entry_types["extractor"] = type_extractor[type_id]
        types[type_id] = entry_types

    return groups_by_category, types, extractors


# ── Placements ────────────────────────────────────────────────────────────────


def build_placements(path: Path) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, int]]]:
    """Return one payload per layer, plus per-group counts."""

    entries: dict[str, list[dict[str, Any]]] = defaultdict(list)
    counts: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    skipped: set[str] = set()

    for entry in read_jsonl(path):
        category = entry.get("category") or "unknown"
        mapping = PLACEMENT_LAYERS.get(category)
        if mapping is None:
            skipped.add(category)
            continue
        layer, group = mapping

        position = entry.get("position_cm") or {}
        if position.get("x") is None or position.get("y") is None:
            continue

        record: dict[str, Any] = {
            "g": group,
            "a": entry.get("actor_type") or "",
            "l": entry.get("actor_label") or "",
            "x": quantize(position.get("x"), POSITION_SCALE_CM),
            "y": quantize(position.get("y"), POSITION_SCALE_CM),
            "z": quantize(position.get("z"), ALTITUDE_SCALE_CM),
        }

        boxes = []
        for shape in entry.get("shapes") or []:
            extent = shape.get("box_extent_scaled_cm") or shape.get("box_extent_unscaled_cm")
            center = shape.get("center_cm") or position
            if not extent:
                continue
            boxes.append(
                [
                    quantize(center.get("x"), POSITION_SCALE_CM),
                    quantize(center.get("y"), POSITION_SCALE_CM),
                    quantize(extent.get("x"), POSITION_SCALE_CM),
                    quantize(extent.get("y"), POSITION_SCALE_CM),
                    int(round((shape.get("rotation_deg") or {}).get("yaw") or 0.0)),
                ]
            )
        if boxes:
            record["b"] = boxes

        entries[layer].append(record)
        counts[layer][group] += 1

    if skipped:
        log(f"placements: ignored categories {sorted(skipped)}")

    payloads: dict[str, dict[str, Any]] = {}
    for layer, records in entries.items():
        records.sort(key=lambda item: (item["g"], item["y"], item["x"]))
        groups = sorted({record["g"] for record in records})
        actor_types = sorted({record["a"] for record in records})
        actor_index = {name: index for index, name in enumerate(actor_types)}
        group_index = {name: index for index, name in enumerate(groups)}
        rows = []
        for record in records:
            row: list[Any] = [
                group_index[record["g"]],
                actor_index[record["a"]],
                record["x"],
                record["y"],
                record["z"],
                record["l"],
            ]
            if "b" in record:
                row.append(record["b"])
            rows.append(row)
        payloads[layer] = {
            "groups": groups,
            "actor_types": actor_types,
            "rows": rows,
        }

    return payloads, counts


# ── Points of interest ────────────────────────────────────────────────────────


def build_pois(path: Path) -> tuple[list[dict[str, Any]], dict[str, int]]:
    document = json.loads(path.read_text(encoding="utf-8"))
    features = document.get("features") or []
    records: list[dict[str, Any]] = []
    counts: dict[str, int] = defaultdict(int)

    for feature in features:
        properties = feature.get("properties") or {}
        coordinates = (feature.get("geometry") or {}).get("coordinates") or []
        if len(coordinates) < 2:
            continue

        poi_type = properties.get("point_of_interest_type") or ""
        group = POI_TYPE_TO_GROUP.get(poi_type, "other")
        name = properties.get("display_name") or {}
        description = properties.get("description") or {}

        # GeoJSON coordinates are metres; the catalog stores decimetres.
        records.append(
            {
                "g": group,
                "x": quantize(float(coordinates[0]) * 100.0, POSITION_SCALE_CM),
                "y": quantize(float(coordinates[1]) * 100.0, POSITION_SCALE_CM),
                "z": quantize(float(coordinates[2]) * 100.0 if len(coordinates) > 2 else 0.0, ALTITUDE_SCALE_CM),
                "en": name.get("source") or properties.get("actor_label") or "",
                "fr": name.get("localized") or name.get("source") or "",
                "den": description.get("source") or "",
                "dfr": description.get("localized") or description.get("source") or "",
                "guid": properties.get("poi_guid") or "",
            }
        )
        counts[group] += 1

    records.sort(key=lambda item: (item["g"], item["y"], item["x"]))
    return records, dict(counts)


# ── Entry point ───────────────────────────────────────────────────────────────


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "analyse_map",
        help="directory holding the map_v2_* exports",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "mapview" / "public" / "map-data",
        help="directory receiving the generated catalog",
    )
    arguments = parser.parse_args()

    source: Path = arguments.source
    output: Path = arguments.output
    output.mkdir(parents=True, exist_ok=True)

    started = time.monotonic()
    parts: list[dict[str, Any]] = []

    poi_records, poi_counts = build_pois(source / "map_v2_pois.geojson")
    size = write_part(
        output,
        "pois",
        {"id": "pois", "version": CATALOG_VERSION, "kind": "pois", "rows": poi_records},
    )
    parts.append(
        {
            "id": "pois",
            "layer": "poi",
            "url": "map-data/pois.js",
            "count": len(poi_records),
            "bytes": size,
            "groups": poi_counts,
        }
    )
    log(f"pois: {len(poi_records)} features -> {size / 1024:.0f} KiB")

    placement_payloads, placement_counts = build_placements(source / "map_v2_placements.jsonl")
    for layer in ("building", "zone", "technical"):
        payload = placement_payloads.get(layer)
        if not payload:
            continue
        part_id = f"placements-{layer}"
        size = write_part(
            output,
            part_id,
            {"id": part_id, "version": CATALOG_VERSION, "kind": "placements", **payload},
        )
        parts.append(
            {
                "id": part_id,
                "layer": layer,
                "url": f"map-data/{part_id}.js",
                "count": len(payload["rows"]),
                "bytes": size,
                "groups": dict(placement_counts[layer]),
            }
        )
        log(f"{part_id}: {len(payload['rows'])} rows -> {size / 1024:.0f} KiB")

    resource_groups, resource_types, extractors = build_resources(
        source / "map_v2_resources.jsonl",
        source / "map_v2_placements.jsonl",
        source / "map_v2_ore_veins.jsonl",
    )
    for category, groups in sorted(resource_groups.items()):
        part_id = f"resources-{category}"
        count = sum(group["n"] for group in groups)
        size = write_part(
            output,
            part_id,
            {
                "id": part_id,
                "version": CATALOG_VERSION,
                "kind": "resources",
                "category": category,
                "groups": groups,
            },
        )
        parts.append(
            {
                "id": part_id,
                "layer": "resource",
                "category": category,
                "url": f"map-data/{part_id}.js",
                "count": count,
                "bytes": size,
                "groups": {},
            }
        )
        log(f"{part_id}: {count} points -> {size / 1024 / 1024:.1f} MiB")

    manifest = {
        "version": CATALOG_VERSION,
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "position_scale_cm": POSITION_SCALE_CM,
        "altitude_scale_cm": ALTITUDE_SCALE_CM,
        "resource_kinds": list(RESOURCE_KINDS),
        "resource_types": resource_types,
        "extractors": extractors,
        "purity_confidence_levels": list(ORE_JOIN_CONFIDENCE),
        "poi_groups": poi_counts,
        "placement_groups": {layer: dict(groups) for layer, groups in placement_counts.items()},
        "parts": parts,
    }
    size = write_part(output, "manifest", {"id": "manifest", **manifest})

    total_bytes = size + sum(part["bytes"] for part in parts)
    log(
        f"manifest -> {size / 1024:.0f} KiB | catalog total {total_bytes / 1024 / 1024:.1f} MiB "
        f"in {time.monotonic() - started:.1f}s"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
