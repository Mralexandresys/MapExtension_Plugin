/**
 * Payload contract version understood by this build of the viewer.
 *
 * The plugin exposes its own `kViewerContractVersion` (see `map_state_json.cpp`)
 * through `GET /health` as `viewer_contract_version`. When the plugin reports a
 * value greater than the one compiled here, the local `MapExtensionViewer.html`
 * is too old for the payloads it receives and the update dialog is shown.
 *
 * Bump this constant AND `kViewerContractVersion` in `map_state_json.cpp` in the
 * same change whenever a payload change makes older viewers incompatible
 * (removed/renamed fields, changed semantics, changed coordinate projection...).
 * Purely additive, backward-compatible fields must NOT bump it, otherwise every
 * user gets an update prompt for nothing.
 */
export const VIEWER_CONTRACT_VERSION = 2;
