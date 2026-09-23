<script setup lang="ts">
import { nextTick, onBeforeUnmount, ref, watch } from "vue";

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

function handleWindowKeydown(event: KeyboardEvent): void {
    if (!props.open) return;

    if (event.key === "Escape") {
        event.preventDefault();
        emit("close");
        return;
    }

    if (event.key !== "Tab") return;

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
    () => props.open,
    async (open) => {
        if (open) {
            previousFocusedElement = document.activeElement as HTMLElement | null;
            window.addEventListener("keydown", handleWindowKeydown);
            await nextTick();
            closeButtonRef.value?.focus();
            return;
        }

        window.removeEventListener("keydown", handleWindowKeydown);
        previousFocusedElement?.focus();
        previousFocusedElement = null;
    },
    { immediate: true },
);

onBeforeUnmount(() => {
    window.removeEventListener("keydown", handleWindowKeydown);
});
</script>

<template>
    <div v-if="open" class="shortcut-backdrop" @click.self="emit('close')">
        <section
            ref="dialogRef"
            class="card shortcut-dialog"
            role="dialog"
            aria-modal="true"
            aria-labelledby="shortcut-dialog-title"
            aria-describedby="shortcut-dialog-subtitle"
        >
            <div class="panel-top-row compact">
                <div>
                    <span class="panel-kicker">{{ ui.shortcuts.kicker }}</span>
                    <h2 id="shortcut-dialog-title">{{ ui.shortcuts.title }}</h2>
                    <p id="shortcut-dialog-subtitle">
                        {{ ui.shortcuts.subtitle }}
                    </p>
                </div>
                <button
                    ref="closeButtonRef"
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
        </section>
    </div>
</template>

<style scoped>
/* Without the scroll, a viewport shorter than the dialog pushed its top edge
   off-screen with no way to reach it: the page itself is locked to 100vh. */
.shortcut-backdrop {
    position: fixed;
    inset: 0;
    z-index: 30;
    display: grid;
    place-items: center;
    padding: 20px;
    overflow: auto;
    background: rgba(2, 4, 12, 0.82);
    backdrop-filter: blur(6px);
}

.shortcut-dialog {
    width: min(760px, 100%);
    max-height: calc(100vh - 40px);
    overflow-y: auto;
    box-sizing: border-box;
    padding: 20px;
    background: var(--panel-strong);
    --cut: 14px;
    clip-path: polygon(var(--cut) 0%, 100% 0%, 100% calc(100% - var(--cut)), calc(100% - var(--cut)) 100%, 0% 100%, 0% var(--cut));
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
}

@media (max-width: 720px) {
    .shortcut-dialog {
        padding: 14px;
    }
}
</style>
