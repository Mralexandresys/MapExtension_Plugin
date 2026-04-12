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
