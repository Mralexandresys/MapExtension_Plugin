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
