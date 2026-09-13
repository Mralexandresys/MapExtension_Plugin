#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
DATA_DIR = ROOT / "data"


def load_json(path: Path) -> Any:
    if not path.exists() or path.stat().st_size == 0:
        return None

    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def load_json_bytes(path: Path) -> bytes | None:
    if not path.exists() or path.stat().st_size == 0:
        return None

    return path.read_bytes()


ENDPOINT_FILES = {
    "health": ["health.json"],
    "cargo": ["cargo.json"],
    "rupture-cycle": ["rupture-cycle.json", "rupture_cycle.json"],
}

# Must match VIEWER_CONTRACT_VERSION in mapview/src/lib/viewerContract.ts so the
# default mock payload does NOT trigger the viewer update dialog. To test the
# dialog locally, drop a local-test/data/health.json with a higher value.
MOCK_VIEWER_CONTRACT_VERSION = 2
MOCK_PLUGIN_VERSION = "ML-v1.16.0-v0.5"
MOCK_RELEASE_BASE = (
    "https://github.com/Mralexandresys/MapExtension_Plugin/releases"
)


def default_payloads(port: int) -> dict[str, Any]:
    return {
        "health": {
            "ok": True,
            "plugin": "mapview-local-mock",
            "version": 1,
            "world": "LocalTest",
            "port": port,
            "snapshot_generation": 1,
            "marker_count": 0,
            "teleporter_count": 0,
            "player_count": 0,
            "plugin_version": MOCK_PLUGIN_VERSION,
            "viewer_contract_version": MOCK_VIEWER_CONTRACT_VERSION,
            "viewer_update": {
                "download_url": (
                    f"{MOCK_RELEASE_BASE}/download/{MOCK_PLUGIN_VERSION}"
                    f"/MapExtension_Plugin-{MOCK_PLUGIN_VERSION}-viewer.zip"
                ),
                "release_url": f"{MOCK_RELEASE_BASE}/tag/{MOCK_PLUGIN_VERSION}",
                "mod_page_url": "https://www.nexusmods.com/starrupture/mods/91",
            },
        },
        "cargo": {
            "generation": 1,
            "world": "LocalTest",
            "counts": {
                "markers": 0,
                "teleporters": 0,
                "players": 2,
                "pois": 6,
                "abandoned_bases": 1,
                "plant_resources": 3,
                "ignitium": 1,
                "star_tears": 1,
            },
            "map": {
                "src_x1": -358583.0,
                "src_y1": -263782.0,
                "dst_x1": 1518.414983,
                "dst_y1": 2272.832995,
                "src_x2": -98583.0,
                "src_y2": -9439.0,
                "dst_x2": 6895.078786,
                "dst_y2": 7535.110312,
                "content_width": 5376.663803,
                "content_height": 5262.277317,
                "image_width": 9019,
                "image_height": 11691,
            },
            "markers": [],
            "connections": [],
            "teleporters": [],
            "players": [
                {
                    "label": "MockSelf",
                    "source": "local_player_pawn",
                    "unique_key": "player:mock-self",
                    "self": True,
                    "world": {"x": -220000.0, "y": -145000.0, "z": 100.0},
                    "map": {"x": 2865.8, "y": 2457.5},
                },
                {
                    "label": "MockAlly",
                    "source": "actor_scan.player",
                    "unique_key": "player:mock-ally",
                    "self": False,
                    "world": {"x": -200000.0, "y": -135000.0, "z": 95.0},
                    "map": {"x": 3279.4, "y": 2664.4},
                },
            ],
            "pois": [
                {
                    "kind": "abandoned_base",
                    "label": "Abandoned Base",
                    "resource": "",
                    "depleted": False,
                    "source": "actor_scan.abandoned_base",
                    "unique_key": "poi:mock-abandoned-base-1",
                    "world": {"x": -230000.0, "y": -150000.0, "z": 120.0},
                    "map": {"x": 2659.0, "y": 2354.1},
                },
                {
                    "kind": "plant_resource",
                    "label": "Gold Fruit",
                    "resource": "Gold Fruit",
                    "depleted": False,
                    "source": "actor_scan.gatherable",
                    "unique_key": "poi:mock-gold-fruit-available",
                    "world": {"x": -210000.0, "y": -140000.0, "z": 85.0},
                    "map": {"x": 3072.6, "y": 2561.0},
                },
                {
                    "kind": "plant_resource",
                    "label": "Gold Fruit",
                    "resource": "Gold Fruit",
                    "depleted": True,
                    "source": "actor_scan.gatherable",
                    "unique_key": "poi:mock-gold-fruit-depleted",
                    "world": {"x": -190000.0, "y": -130000.0, "z": 90.0},
                    "map": {"x": 3486.2, "y": 2767.9},
                },
                {
                    "kind": "plant_resource",
                    "label": "Plant Fiber",
                    "resource": "Plant Fiber",
                    "depleted": False,
                    "source": "actor_scan.gatherable",
                    "unique_key": "poi:mock-plant-fiber-available",
                    "world": {"x": -250000.0, "y": -120000.0, "z": 75.0},
                    "map": {"x": 2245.4, "y": 2974.8},
                },
                {
                    "kind": "ignitium",
                    "label": "Ignitium",
                    "resource": "Ignitium",
                    "depleted": False,
                    "source": "actor_scan.ore",
                    "unique_key": "poi:mock-ignitium-available",
                    "world": {"x": -180000.0, "y": -110000.0, "z": 140.0},
                    "map": {"x": 3693.0, "y": 3181.0},
                },
                {
                    "kind": "star_tears",
                    "label": "Star Tears",
                    "resource": "Star Tears",
                    "depleted": False,
                    "source": "actor_scan.gatherable",
                    "unique_key": "poi:mock-star-tears-available",
                    "world": {"x": -170000.0, "y": -100000.0, "z": 145.0},
                    "map": {"x": 3900.0, "y": 3387.0},
                },
            ],
        },
        "rupture-cycle": {
            "ok": True,
            "generation": 1,
            "world": "LocalTest",
            "timeline": {
                "cycle_total_seconds": 3240,
                "phase_seconds": {
                    "burning": 30,
                    "cooling": 60,
                    "stabilizing": 600,
                    "stable": 2550,
                },
            },
            "rupture_cycle": {
                "available": False,
                "wave": "mock",
                "stage": "PreWave",
                "step": "None",
                "elapsed_seconds": 0,
                "observed_at_unix_ms": 0,
            },
        },
    }


def resolve_payload(endpoint: str, port: int) -> Any:
    for filename in ENDPOINT_FILES[endpoint]:
        payload = load_json(DATA_DIR / filename)
        if payload is not None:
            return payload

    return default_payloads(port)[endpoint]


def resolve_payload_bytes(endpoint: str) -> bytes | None:
    for filename in ENDPOINT_FILES[endpoint]:
        payload = load_json_bytes(DATA_DIR / filename)
        if payload is not None:
            return payload

    return None


class MockHandler(BaseHTTPRequestHandler):
    server_version = "MapViewMock/0.1"

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(HTTPStatus.NO_CONTENT)
        self.send_cors_headers()
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"

        if path == "/":
            self.send_json(
                {
                    "ok": True,
                    "service": "mapview-local-mock",
                    "endpoints": ["/health", "/cargo", "/rupture-cycle"],
                    "data_dir": str(DATA_DIR),
                }
            )
            return

        if path in {"/health", "/cargo", "/rupture-cycle"}:
            endpoint = path.lstrip("/")
            port = self.server.server_port
            payload_bytes = resolve_payload_bytes(endpoint)
            if payload_bytes is not None:
                self.send_json_bytes(payload_bytes)
                return
            self.send_json(resolve_payload(endpoint, port))
            return

        self.send_json(
            {"ok": False, "error": "not_found", "path": parsed.path},
            status=HTTPStatus.NOT_FOUND,
        )

    def log_message(self, format: str, *args: Any) -> None:
        return

    def send_json(self, payload: Any, status: HTTPStatus = HTTPStatus.OK) -> None:
        body = json.dumps(payload, ensure_ascii=True, indent=2).encode("utf-8")
        self.send_json_bytes(body, status)

    def send_json_bytes(
        self,
        body: bytes,
        status: HTTPStatus = HTTPStatus.OK,
    ) -> None:
        self.send_response(status)
        self.send_cors_headers()
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_cors_headers(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Local mock API for mapview")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    for endpoint, filenames in ENDPOINT_FILES.items():
        matched_path = next(
            (
                DATA_DIR / name
                for name in filenames
                if (DATA_DIR / name).exists() and (DATA_DIR / name).stat().st_size > 0
            ),
            None,
        )
        if matched_path is not None:
            print(f"{endpoint}: serving file {matched_path.name}", flush=True)
        else:
            print(f"{endpoint}: using built-in fallback payload", flush=True)
    server = ThreadingHTTPServer((args.host, args.port), MockHandler)
    print(
        f"Mapview mock server listening on http://{args.host}:{args.port}",
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
