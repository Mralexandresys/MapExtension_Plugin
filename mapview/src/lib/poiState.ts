import type { Messages } from "../lang";
import type { Poi, PoiState } from "./types";

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
