<script setup lang="ts">
import { ref, watchPostEffect } from "vue";

import type { MapViewerUpdateDialogModel } from "../../lib/types";

const props = defineProps<{
    panel: MapViewerUpdateDialogModel;
}>();

const emit = defineEmits<{
    "close": [];
}>();

// A modal <dialog> traps focus, closes on Escape, makes the page inert and
// restores focus on close. Opened last, it sits above the shortcut dialog.
const dialogRef = ref<HTMLDialogElement | null>(null);
watchPostEffect(() => {
    const dialog = dialogRef.value;
    if (!dialog) return;
    if (props.panel.open && !dialog.open) dialog.showModal();
    else if (!props.panel.open && dialog.open) dialog.close();
});
</script>

<template>
    <dialog
        ref="dialogRef"
        class="card viewer-update-dialog"
        aria-labelledby="viewer-update-dialog-title"
        aria-describedby="viewer-update-dialog-subtitle"
        @close="panel.open && emit('close')"
        @click.self="emit('close')"
    >
        <div class="viewer-update-dialog-body">
            <div class="panel-top-row compact">
                <div>
                    <span class="panel-kicker">{{ panel.ui.viewerUpdate.kicker }}</span>
                    <h2 id="viewer-update-dialog-title">{{ panel.ui.viewerUpdate.title }}</h2>
                    <p id="viewer-update-dialog-subtitle">
                        {{ panel.ui.viewerUpdate.subtitle }}
                    </p>
                </div>
                <button
                    autofocus
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
        </div>
    </dialog>
</template>

<style scoped>
/* The padding lives on the body: a click on the <dialog> element itself is a
   click on the backdrop. */
.viewer-update-dialog {
    width: min(640px, calc(100% - 40px));
    max-width: none;
    max-height: calc(100vh - 40px);
    overflow-y: auto;
    box-sizing: border-box;
    padding: 0;
    color: var(--text);
    background: var(--panel-strong);
    --cut: 14px;
    clip-path: polygon(var(--cut) 0%, 100% 0%, 100% calc(100% - var(--cut)), calc(100% - var(--cut)) 100%, 0% 100%, 0% var(--cut));
}

.viewer-update-dialog::backdrop {
    background: rgba(2, 4, 12, 0.82);
    backdrop-filter: blur(6px);
}

.viewer-update-dialog-body {
    padding: 20px;
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
    .viewer-update-dialog-body {
        padding: 14px;
    }

    .viewer-update-actions .button {
        flex: 1 1 100%;
        justify-content: center;
    }
}
</style>
