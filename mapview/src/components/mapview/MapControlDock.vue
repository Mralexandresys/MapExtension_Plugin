<script setup lang="ts">
import type { Language } from "../../lang";
import type { MapControlDockModel } from "../../lib/types";

const props = defineProps<{
    panel: MapControlDockModel;
}>();

const emit = defineEmits<{
    "toggle-settings": [];
    "update:endpoint-draft": [value: string];
    "endpoint-keydown": [value: KeyboardEvent];
    "apply-endpoint": [];
    "refresh": [];
    "toggle-auto-refresh": [];
    "update:lang": [value: Language];
    "open-shortcuts": [];
}>();

function handleEndpointInput(event: Event): void {
    emit("update:endpoint-draft", (event.target as HTMLInputElement).value);
}

function handleLanguageChange(event: Event): void {
    emit("update:lang", (event.target as HTMLSelectElement).value as Language);
}
</script>

<template>
    <section class="command-header-shell">
        <div class="command-header">
            <div class="command-header-main">
                <div class="command-header-brand">
                    <span class="panel-kicker">{{ panel.ui.hero.eyebrow }}</span>
                    <div class="command-header-title-row">
                        <h1>{{ panel.ui.hero.title }}</h1>
                        <span class="status-inline status-pill" :class="panel.statusTone">
                            <span class="status-dot small"></span>
                            {{ panel.statusBadgeLabel }}
                        </span>
                    </div>
                    <div class="control-dock-meta">
                        <span>{{ panel.currentTimeLabel }}</span>
                        <span class="status-footer-dot" aria-hidden="true"></span>
                        <span>{{ panel.liveAgeLabel }}</span>
                    </div>
                </div>

                <div class="command-header-actions">
                    <button
                        class="button primary small"
                        type="button"
                        :disabled="panel.loading"
                        @click="emit('refresh')"
                    >
                        {{
                            panel.loading
                                ? panel.ui.buttons.refreshing
                                : panel.ui.buttons.refresh
                        }}
                    </button>
                    <button
                        class="button subtle small"
                        type="button"
                        @click="emit('open-shortcuts')"
                    >
                        {{ panel.ui.buttons.help }}
                    </button>
                    <button
                        class="button subtle small settings-trigger"
                        :class="{ active: panel.settingsOpen }"
                        type="button"
                        :aria-expanded="panel.settingsOpen"
                        @click="emit('toggle-settings')"
                    >
                        <span class="gear-icon" aria-hidden="true"></span>
                        <span>{{ panel.ui.buttons.settings }}</span>
                    </button>
                </div>
            </div>

            <div v-if="panel.settingsOpen" class="command-settings-popover">
                <label class="field compact endpoint-field dock-endpoint-field">
                    <span>{{ panel.ui.hero.endpoint }}</span>
                    <div class="endpoint-inline-row">
                        <input
                            :value="panel.endpointDraft"
                            type="text"
                            :placeholder="panel.defaultEndpoint"
                            spellcheck="false"
                            :class="{
                                pending: panel.endpointHasPendingChanges,
                            }"
                            @input="handleEndpointInput"
                            @keydown="emit('endpoint-keydown', $event)"
                        />
                        <button
                            class="button primary small endpoint-apply-button"
                            type="button"
                            :disabled="!panel.endpointHasPendingChanges"
                            @click="emit('apply-endpoint')"
                        >
                            {{ panel.ui.buttons.applyEndpoint }}
                        </button>
                    </div>
                    <small
                        class="field-note"
                        :class="{ pending: panel.endpointHasPendingChanges }"
                    >
                        {{
                            panel.endpointHasPendingChanges
                                ? panel.ui.status.endpointPending
                                : panel.ui.format.endpointReady(
                                      panel.normalizedEndpoint || "--",
                                  )
                        }}
                    </small>
                </label>

                <div class="control-dock-utility-row">
                    <button
                        class="button subtle small"
                        type="button"
                        @click="emit('toggle-auto-refresh')"
                    >
                        {{
                            panel.autoRefresh
                                ? panel.ui.buttons.live
                                : panel.ui.buttons.pause
                        }}
                    </button>

                    <label
                        class="field compact language-field inline-language-field"
                    >
                        <span>{{ panel.ui.languageLabel }}</span>
                        <select
                            :value="panel.lang"
                            @change="handleLanguageChange"
                        >
                            <option
                                v-for="option in panel.languageOptions"
                                :key="option"
                                :value="option"
                            >
                                {{ panel.ui.languages[option] }}
                            </option>
                        </select>
                    </label>

                    <div
                        class="inline-note control-dock-note"
                        :class="{ error: !!props.panel.statusError }"
                    >
                        <strong>{{ panel.statusText }}</strong>
                        <span v-if="panel.statusError">{{ panel.statusError }}</span>
                        <span v-else-if="panel.endpointHasPendingChanges">
                            {{ panel.ui.status.endpointPending }}
                        </span>
                        <span v-else>
                            {{ panel.liveAgeLabel }}
                        </span>
                    </div>
                </div>
            </div>
            <div v-if="panel.commandStats?.length" class="command-stats-row">
                <div
                    v-for="stat in panel.commandStats"
                    :key="stat.key"
                    class="command-stat"
                    :class="stat.tone"
                >
                    <span class="command-stat-label">{{ stat.label }}</span>
                    <span class="command-stat-value">{{ stat.value }}</span>
                </div>
            </div>
        </div>
    </section>
</template>

<style scoped>
.command-header-shell {
    position: relative;
    width: 100%;
    padding: 0;
    overflow: visible;
    border: 0;
    border-radius: 0;
    box-shadow: none;
    backdrop-filter: none;
}

.command-header {
    position: relative;
    display: block;
    width: 100%;
    padding: 12px 14px 14px;
    border-radius: 0;
    background: rgba(12, 20, 38, 0.99);
    border-bottom: 1px solid var(--border-strong);
    box-shadow: inset 0 -1px 0 rgba(255, 255, 255, 0.05);
}

.command-header-main {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    gap: 12px;
    align-items: start;
    min-height: 52px;
}

.command-header-brand {
    min-width: 0;
    display: grid;
    gap: 6px;
}

.command-header-actions {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: flex-end;
    gap: 6px;
}

.command-settings-popover {
    position: absolute;
    top: calc(100% + 10px);
    right: 8px;
    z-index: 6;
    width: min(620px, 100%);
    display: grid;
    gap: 10px;
    padding: 12px;
    border-radius: 10px;
    border: 1px solid var(--border);
    background: rgba(14, 22, 40, 0.99);
    box-shadow: 0 22px 46px rgba(0, 0, 0, 0.34);
}

.command-header-title-row {
    display: flex;
    align-items: center;
    gap: 10px;
    min-width: 0;
    flex-wrap: wrap;
}

.command-header h1 {
    margin: 0;
    font-size: 0.85rem;
    line-height: 1.1;
    white-space: nowrap;
    font-family: var(--font-display);
    text-transform: uppercase;
    letter-spacing: 0.12em;
}

.control-dock-meta {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    color: #c8d8f0;
    font-size: 0.74rem;
}

.command-stat {
    min-width: 94px;
    padding: 12px 14px;
    display: grid;
    gap: 4px;
    border-right: 1px solid rgba(255, 255, 255, 0.08);
    background: rgba(14, 22, 42, 0.98);
}

.command-stat:last-child {
    border-right: 0;
}

.command-stat strong {
    font-size: 1rem;
    line-height: 1;
    color: var(--text);
    font-family: var(--font-mono);
    font-weight: 700;
}

.command-stat span {
    font-size: 0.62rem;
    text-transform: uppercase;
    letter-spacing: 0.1em;
    color: var(--muted);
    font-family: var(--font-mono);
}

.command-stat.primary strong { color: var(--accent); }
.command-stat.good strong    { color: var(--good); }
.command-stat.warn strong    { color: var(--warn); }
.command-stat.bad strong     { color: var(--bad); }

.command-stats-row {
    display: flex;
    gap: 16px;
    align-items: center;
    padding: 6px 16px;
    border-top: 1px solid var(--border-strong);
    background: rgba(14, 22, 42, 0.96);
    font-family: var(--font-mono);
    font-size: 0.62rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
}

.command-stat-label { color: var(--muted); display: block; margin-bottom: 1px; }
.command-stat-value { color: var(--text); font-weight: 700; display: block; font-size: 0.76rem; }
.command-stat.good .command-stat-value { color: var(--good); }
.command-stat.warn .command-stat-value { color: var(--warn); }
.command-stat.bad  .command-stat-value { color: var(--bad); }

.dock-endpoint-field {
    gap: 8px;
}

.endpoint-inline-row {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    gap: 8px;
    align-items: center;
}

.endpoint-apply-button {
    min-width: 108px;
}

.control-dock-utility-row {
    display: grid;
    grid-template-columns: auto minmax(100px, 132px) minmax(0, 1fr);
    gap: 10px;
    align-items: stretch;
}

.control-dock-note {
    min-height: 100%;
    background: rgba(8, 14, 26, 0.82);
}

.control-dock-note strong {
    line-height: 1.3;
}

.settings-trigger {
    display: inline-flex;
    align-items: center;
    gap: 8px;
}

.settings-trigger.active {
    background: var(--accent-soft);
    border-color: var(--border-strong);
}

.gear-icon {
    position: relative;
    display: inline-block;
    width: 14px;
    height: 14px;
    border: 2px solid currentColor;
    border-radius: 999px;
}

.gear-icon::before {
    content: "";
    position: absolute;
    inset: -4px;
    border: 2px dashed currentColor;
    border-radius: 999px;
    opacity: 0.62;
}

@media (max-width: 980px) {
    .endpoint-inline-row {
        grid-template-columns: 1fr;
    }

    .command-header-main {
        grid-template-columns: 1fr;
    }

    .control-dock-utility-row {
        grid-template-columns: 1fr;
    }

    .command-settings-popover {
        position: static;
        width: 100%;
        margin-top: 10px;
    }
}
</style>
