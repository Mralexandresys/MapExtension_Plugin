import type { Messages } from "../lang";
import type { Poi, PoiKind, PoiState } from "./types";

export function poiState(poi: Poi): PoiState {
    if (poi.depleted || poi.state === "depleted") return "depleted";
    // Older plugins only expose the depletion flag.
    return poi.state ?? "available";
}

export function poiStateLabel(poi: Poi, labels: Messages["map"]): string {
    switch (poiState(poi)) {
        case "available": return poi.source?.startsWith("rupture_phase.")
            ? labels.estimatedAvailableLabel : labels.availableLabel;
        case "unavailable": return labels.unavailableLabel;
        case "depleted": return labels.depletedLabel;
        default: return labels.unknownStateLabel;
    }
}

export function poiKindLabel(kind: PoiKind, labels: Messages["map"]): string {
    switch (kind) {
        case "abandoned_base": return labels.abandonedBaseLabel;
        case "plant_resource": return labels.plantResourceLabel;
        case "ignitium": return labels.ignitiumLabel;
        case "star_tears": return labels.starTearsLabel;
    }
}
