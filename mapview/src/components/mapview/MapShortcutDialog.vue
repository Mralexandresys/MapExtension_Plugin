<script setup lang="ts">
import { ref, watchPostEffect } from "vue";

import type { Messages } from "../../lang";
import type { ShortcutItem } from "../../lib/types";

const props = defineProps<{
    open: boolean;
    ui: Messages;
    items: ShortcutItem[];
}>();

const emit = defineEmits<{
    "close": [];
}>();

// A modal <dialog> traps focus, closes on Escape, makes the page inert and
// restores focus on close; the parent's `open` flag stays the source of truth.
const dialogRef = ref<HTMLDialogElement | null>(null);
watchPostEffect(() => {
    const dialog = dialogRef.value;
    if (!dialog) return;
    if (props.open && !dialog.open) dialog.showModal();
    else if (!props.open && dialog.open) dialog.close();
});
</script>

<template>
    <dialog
        ref="dialogRef"
        class="card shortcut-dialog"
        aria-labelledby="shortcut-dialog-title"
        aria-describedby="shortcut-dialog-subtitle"
        @close="open && emit('close')"
        @click.self="emit('close')"
    >
        <div class="shortcut-dialog-body">
            <div class="panel-top-row compact">
                <div>
                    <span class="panel-kicker">{{ ui.shortcuts.kicker }}</span>
                    <h2 id="shortcut-dialog-title">{{ ui.shortcuts.title }}</h2>
                    <p id="shortcut-dialog-subtitle">
                        {{ ui.shortcuts.subtitle }}
                    </p>
                </div>
                <button
                    autofocus
                    class="button subtle small"
                    type="button"
                    @click="emit('close')"
                >
                    {{ ui.buttons.close }}
                </button>
            </div>

            <div class="shortcut-list">
                <div v-for="item in items" :key="item.label" class="shortcut-row">
                    <div class="keycap-row">
                        <kbd v-for="key in item.keys" :key="key" class="keycap">{{ key }}</kbd>
                    </div>
                    <div>
                        <strong>{{ item.label }}</strong>
                        <p>{{ item.description }}</p>
                    </div>
                </div>
            </div>
        </div>
    </dialog>
</template>

<style scoped>
/* The padding lives on the body: a click on the <dialog> element itself is a
   click on the backdrop. The max-height keeps short viewports scrollable. */
.shortcut-dialog {
    width: min(760px, calc(100% - 40px));
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

.shortcut-dialog::backdrop {
    background: rgba(2, 4, 12, 0.82);
    backdrop-filter: blur(6px);
}

.shortcut-dialog-body {
    padding: 20px;
}

.shortcut-list {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(310px, 1fr));
    gap: 8px;
    margin-top: 14px;
}

.shortcut-row {
    display: grid;
    grid-template-columns: minmax(88px, auto) minmax(0, 1fr);
    align-items: center;
    gap: 12px;
    padding: 10px 12px;
    border-radius: 12px;
    border: 1px solid var(--border);
    background: rgba(8, 14, 26, 0.62);
}

.shortcut-row p {
    margin-top: 4px;
    color: var(--muted);
}

.shortcut-row strong {
    font-size: 0.86rem;
}

.keycap-row {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    align-items: center;
}

.keycap {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-width: 34px;
    min-height: 34px;
    padding: 2px 7px;
    border-radius: 2px;
    border: 1px solid var(--border-strong);
    background: rgba(34, 211, 238, 0.08);
    color: var(--accent);
    font-family: var(--font-mono);
    font-size: 0.72rem;
    font-weight: 700;
}

@media (max-width: 720px) {
    .shortcut-row {
        grid-template-columns: 1fr;
    }

    .shortcut-dialog-body {
        padding: 14px;
    }
}
</style>
