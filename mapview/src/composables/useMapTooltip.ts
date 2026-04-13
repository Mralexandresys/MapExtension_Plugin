import { ref, type Ref } from "vue";

import { clamp } from "../lib/formatters";

export interface TooltipState {
    visible: boolean;
    title: string;
    lines: string[];
    left: number;
    top: number;
}

export function useMapTooltip(mapShell: Ref<HTMLElement | null>) {
    const tooltip = ref<TooltipState>({
        visible: false,
        title: "",
        lines: [],
        left: 0,
        top: 0,
    });

    let showTimer: ReturnType<typeof setTimeout> | null = null;

    function hideTooltip(): void {
        if (showTimer) { clearTimeout(showTimer); showTimer = null; }
        tooltip.value.visible = false;
    }

    function updateTooltipPosition(clientX: number, clientY: number): void {
        if (!mapShell.value) return;

        const rect = mapShell.value.getBoundingClientRect();
        const estimatedWidth = 292;
        const estimatedHeight = 72 + tooltip.value.lines.length * 18;
        let left = clientX - rect.left + 16;
        let top = clientY - rect.top + 16;

        if (left + estimatedWidth > rect.width - 12) {
            left = clientX - rect.left - estimatedWidth - 16;
        }
        if (top + estimatedHeight > rect.height - 12) {
            top = clientY - rect.top - estimatedHeight - 16;
        }

        tooltip.value.left = clamp(left, 12, Math.max(12, rect.width - estimatedWidth));
        tooltip.value.top = clamp(top, 12, Math.max(12, rect.height - estimatedHeight));
    }

    function showTooltipAtPosition(title: string, lines: string[], clientX: number, clientY: number): void {
        if (showTimer) clearTimeout(showTimer);
        showTimer = setTimeout(() => {
            tooltip.value.visible = true;
            tooltip.value.title = title;
            tooltip.value.lines = lines;
            updateTooltipPosition(clientX, clientY);
            showTimer = null;
        }, 120);
    }

    function showTooltip(title: string, lines: string[], event: MouseEvent): void {
        showTooltipAtPosition(title, lines, event.clientX, event.clientY);
    }

    function showTooltipFromElement(title: string, lines: string[], element: Element): void {
        const rect = element.getBoundingClientRect();
        showTooltipAtPosition(title, lines, rect.left + rect.width / 2, rect.top + rect.height / 2);
    }

    function moveTooltip(event: MouseEvent): void {
        if (!tooltip.value.visible) return;
        updateTooltipPosition(event.clientX, event.clientY);
    }

    return {
        tooltip,
        hideTooltip,
        showTooltip,
        showTooltipFromElement,
        moveTooltip,
    };
}
