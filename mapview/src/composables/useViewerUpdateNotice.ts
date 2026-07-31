import { computed, type ComputedRef, ref, type Ref } from "vue";

/**
 * Dedicated storage key for the viewer-update prompt.
 *
 * Never reuse the preferences (`starrupture-mapview:v3`) or annotations
 * (`user-annotations`) keys: those hold user data and must stay untouched.
 */
const STORAGE_KEY = "starrupture-mapview:viewer-update-dismissed:v1";

/** Fallback bucket used when the plugin does not report a `plugin_version`. */
const UNKNOWN_PLUGIN_VERSION = "unknown";

interface PersistedDismissal {
    dismissedPluginVersion?: unknown;
}

function readDismissedVersion(): string {
    if (typeof localStorage === "undefined") return "";

    try {
        const raw = localStorage.getItem(STORAGE_KEY);
        if (!raw) return "";

        const parsed = JSON.parse(raw) as PersistedDismissal;
        return typeof parsed.dismissedPluginVersion === "string"
            ? parsed.dismissedPluginVersion
            : "";
    } catch {
        // storage unavailable or corrupted payload — behave as "never dismissed"
        return "";
    }
}

function writeDismissedVersion(version: string): void {
    if (typeof localStorage === "undefined") return;

    try {
        localStorage.setItem(
            STORAGE_KEY,
            JSON.stringify({ dismissedPluginVersion: version }),
        );
    } catch {
        // storage unavailable — the prompt simply comes back on the next reload
    }
}

/**
 * Drives the viewer-update dialog: it opens whenever the plugin reports a newer
 * payload contract, unless the user already dismissed it for that exact plugin
 * version. A newer plugin version re-opens the prompt.
 */
export function useViewerUpdateNotice(
    outdated: Readonly<Ref<boolean>> | ComputedRef<boolean>,
    pluginVersion: Readonly<Ref<string>> | ComputedRef<string>,
) {
    const dismissedVersion = ref(readDismissedVersion());

    const dismissKey = computed(
        () => pluginVersion.value || UNKNOWN_PLUGIN_VERSION,
    );

    const viewerUpdateOpen = computed(
        () => outdated.value && dismissedVersion.value !== dismissKey.value,
    );

    function dismissViewerUpdate(): void {
        dismissedVersion.value = dismissKey.value;
        writeDismissedVersion(dismissKey.value);
    }

    return {
        viewerUpdateOpen,
        dismissViewerUpdate,
    };
}
