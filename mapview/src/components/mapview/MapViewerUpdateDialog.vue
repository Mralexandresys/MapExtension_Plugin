<script setup lang="ts">
import { nextTick, onBeforeUnmount, ref, watch } from "vue";

import type { MapViewerUpdateDialogModel } from "../../lib/types";

const props = defineProps<{
    panel: MapViewerUpdateDialogModel;
}>();

const emit = defineEmits<{
    "close": [];
}>();

const dialogRef = ref<HTMLElement | null>(null);
const closeButtonRef = ref<HTMLButtonElement | null>(null);
let previousFocusedElement: HTMLElement | null = null;

function getFocusableElements(): HTMLElement[] {
    if (!dialogRef.value) return [];

    return Array.from(
        dialogRef.value.querySelectorAll<HTMLElement>(
            'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])',
        ),
    ).filter((element) => !element.hasAttribute("disabled"));
}

// Registered in the capture phase so this dialog, which sits above every other
// overlay, deterministically consumes Escape/Tab before the shortcut dialog and
// the global shortcut handler can react to them.
function handleWindowKeydown(event: KeyboardEvent): void {
    if (!props.panel.open) return;

    if (event.key === "Escape") {
        event.preventDefault();
        event.stopImmediatePropagation();
        emit("close");
        return;
    }

    if (event.key !== "Tab") return;

    event.stopImmediatePropagation();

    const focusableElements = getFocusableElements();
    if (!focusableElements.length) return;

    const first = focusableElements[0];
    const last = focusableElements[focusableElements.length - 1];
    const active = document.activeElement as HTMLElement | null;

    if (event.shiftKey && active === first) {
        event.preventDefault();
        last.focus();
    } else if (!event.shiftKey && active === last) {
        event.preventDefault();
        first.focus();
    }
}

watch(
    () => props.panel.open,
    async (open) => {
        if (open) {
            previousFocusedElement = document.activeElement as HTMLElement | null;
            window.addEventListener("keydown", handleWindowKeydown, true);
            await nextTick();
            closeButtonRef.value?.focus();
            return;
        }

        window.removeEventListener("keydown", handleWindowKeydown, true);
        previousFocusedElement?.focus();
        previousFocusedElement = null;
    },
    { immediate: true },
);

onBeforeUnmount(() => {
    window.removeEventListener("keydown", handleWindowKeydown, true);
});
</script>

<template>
    <div
        v-if="panel.open"
        class="viewer-update-backdrop"
        @click.self="emit('close')"
    >
        <section
            ref="dialogRef"
            class="card viewer-update-dialog"
            role="dialog"
            aria-modal="true"
            aria-labelledby="viewer-update-dialog-title"
            aria-describedby="viewer-update-dialog-subtitle"
        >
            <div class="panel-top-row compact">
                <div>
                    <span class="panel-kicker">{{ panel.ui.viewerUpdate.kicker }}</span>
                    <h2 id="viewer-update-dialog-title">{{ panel.ui.viewerUpdate.title }}</h2>
                    <p id="viewer-update-dialog-subtitle">
                        {{ panel.ui.viewerUpdate.subtitle }}
                    </p>
                </div>
                <button
                    ref="closeButtonRef"
                    class="button subtle small"
                    type="button"
                    @click="emit('close')"
                >
                    {{ panel.ui.buttons.close }}
                </button>
            </div>

            <div class="viewer-update-body">
                <p class="viewer-update-text">{{ panel.ui.viewerUpdate.body }}</p>

                <p v-if="panel.pluginVersion" class="viewer-update-version">
                    <span>{{ panel.ui.viewerUpdate.pluginVersionLabel }}</span>
                    <strong>{{ panel.pluginVersion }}</strong>
                </p>

                <ol class="viewer-update-steps">
                    <li>{{ panel.ui.viewerUpdate.steps.download }}</li>
                    <li>{{ panel.ui.viewerUpdate.steps.replace }}</li>
                    <li>{{ panel.ui.viewerUpdate.steps.reload }}</li>
                </ol>

                <p class="viewer-update-warning">{{ panel.ui.viewerUpdate.tilesReminder }}</p>

                <p v-if="!panel.downloadUrl" class="viewer-update-note">
                    {{ panel.ui.viewerUpdate.noDownloadUrl }}
                </p>
            </div>

            <div class="viewer-update-actions">
                <a
                    v-if="panel.downloadUrl"
                    class="button primary"
                    :href="panel.downloadUrl"
                    target="_blank"
                    rel="noopener noreferrer"
                >
                    {{ panel.ui.viewerUpdate.downloadAction }}
                </a>
                <a
                    v-if="panel.releaseUrl"
                    class="button"
                    :href="panel.releaseUrl"
                    target="_blank"
                    rel="noopener noreferrer"
                >
                    {{ panel.ui.viewerUpdate.releaseAction }}
                </a>
                <a
                    v-if="panel.modPageUrl"
                    class="button"
                    :href="panel.modPageUrl"
                    target="_blank"
                    rel="noopener noreferrer"
                >
                    {{ panel.ui.viewerUpdate.modPageAction }}
                </a>
                <button
                    class="button subtle"
                    type="button"
                    @click="emit('close')"
                >
                    {{ panel.ui.viewerUpdate.laterAction }}
                </button>
            </div>
        </section>
    </div>
</template>

<style scoped>
.viewer-update-backdrop {
    position: fixed;
    inset: 0;
    z-index: 40;
    display: grid;
    place-items: center;
    padding: 20px;
    background: rgba(2, 4, 12, 0.82);
    backdrop-filter: blur(6px);
}

.viewer-update-dialog {
    width: min(640px, 100%);
    box-sizing: border-box;
    padding: 20px;
    background: var(--panel-strong);
    --cut: 14px;
    clip-path: polygon(var(--cut) 0%, 100% 0%, 100% calc(100% - var(--cut)), calc(100% - var(--cut)) 100%, 0% 100%, 0% var(--cut));
}

.viewer-update-body {
    display: grid;
    gap: 12px;
    margin-top: 14px;
    padding: 14px;
    border-radius: 16px;
    border: 1px solid var(--border);
    background: rgba(8, 14, 26, 0.62);
}

.viewer-update-text {
    color: var(--muted);
}

.viewer-update-version {
    display: flex;
    flex-wrap: wrap;
    align-items: baseline;
    gap: 8px;
    color: var(--muted);
}

.viewer-update-version strong {
    font-family: var(--font-mono);
    font-size: 0.78rem;
    color: var(--accent);
}

.viewer-update-steps {
    display: grid;
    gap: 6px;
    margin: 0;
    padding-left: 18px;
    color: var(--muted);
}

.viewer-update-warning {
    padding: 8px 12px;
    border-left: 2px solid var(--amber);
    background: rgba(232, 184, 75, 0.08);
    color: var(--text);
    font-size: 0.82rem;
}

.viewer-update-note {
    color: var(--dim);
    font-size: 0.78rem;
}

.viewer-update-actions {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-top: 14px;
}

.viewer-update-actions .button {
    display: inline-flex;
    align-items: center;
    text-decoration: none;
}

@media (max-width: 720px) {
    .viewer-update-dialog {
        padding: 14px;
    }

    .viewer-update-actions .button {
        flex: 1 1 100%;
        justify-content: center;
    }
}
</style>
