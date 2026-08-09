<script setup lang="ts">
import { ref } from "vue";

import type {
    MapStaticFiltersModel,
    StaticFilterOption,
    StaticFilterToggle,
} from "../../lib/types";

const props = defineProps<{
    model: MapStaticFiltersModel;
}>();

const emit = defineEmits<{
    toggle: [value: StaticFilterToggle];
    "update:search": [value: string];
    "show-all": [];
    "hide-all": [];
}>();

const expandedCategories = ref<Record<string, boolean>>({});

function isExpanded(key: string): boolean {
    return expandedCategories.value[key] === true;
}

function toggleExpanded(key: string): void {
    expandedCategories.value[key] = !isExpanded(key);
}

function optionStyle(option: StaticFilterOption): Record<string, string> {
    return { "--static-swatch": option.color ?? "#64748b" };
}

function handleSearch(event: Event): void {
    emit("update:search", (event.target as HTMLInputElement).value);
}
</script>

<template>
    <div class="static-filters">
        <!-- Title and loaded count live in the collapsible section header. -->
        <p v-if="!model.available" class="static-hint">
            {{ model.ui.staticFilters.unavailable }}
        </p>

        <template v-if="model.available">
            <div class="static-filters-toolbar">
                <label class="static-search-field">
                    <span class="sr-only">
                        {{ model.ui.staticFilters.searchLabel }}
                    </span>
                    <input
                        class="static-search"
                        type="search"
                        :value="model.search"
                        :placeholder="model.ui.staticFilters.searchPlaceholder"
                        @input="handleSearch"
                    />
                </label>
                <div class="static-bulk-actions">
                    <button class="button subtle small" type="button" @click="emit('show-all')">
                        {{ model.ui.staticFilters.showAll }}
                    </button>
                    <button class="button subtle small" type="button" @click="emit('hide-all')">
                        {{ model.ui.staticFilters.hideAll }}
                    </button>
                </div>
            </div>

            <p v-if="model.loading" class="static-hint">
                {{ model.ui.staticFilters.loading }}
            </p>
            <p v-else-if="model.error" class="static-hint static-error">
                {{ model.error }}
            </p>

            <div class="chip-group static-chip-group">
                <button
                    v-for="layer in model.layers"
                    :key="layer.key"
                    class="chip-button"
                    :class="{ active: layer.enabled, muted: !layer.enabled }"
                    type="button"
                    :aria-pressed="layer.enabled"
                    @click="emit('toggle', { scope: 'layer', key: layer.key })"
                >
                    <span class="static-option-label">{{ layer.label }}</span>
                    <strong class="filter-option-count">{{ layer.count }}</strong>
                </button>
            </div>

            <div v-if="model.poiGroups.length" class="static-subsection">
                <h4>{{ model.ui.staticFilters.poiTitle }}</h4>
                <div class="chip-group static-chip-group">
                    <button
                        v-for="group in model.poiGroups"
                        :key="group.key"
                        class="chip-button"
                        :class="{ active: group.enabled, muted: !group.enabled }"
                        type="button"
                        :style="optionStyle(group)"
                        :aria-pressed="group.enabled"
                        @click="emit('toggle', { scope: 'poiGroup', key: group.key })"
                    >
                        <span class="static-option-label">
                            <span class="static-swatch" aria-hidden="true"></span>
                            {{ group.label }}
                        </span>
                        <strong class="filter-option-count">{{ group.count }}</strong>
                    </button>
                </div>
            </div>

            <div v-if="model.representations.length" class="static-subsection">
                <h4>{{ model.ui.staticFilters.representationTitle }}</h4>
                <p class="static-hint">{{ model.ui.staticFilters.representationHelp }}</p>
                <div class="chip-group static-chip-group">
                    <button
                        v-for="representation in model.representations"
                        :key="representation.key"
                        class="chip-button"
                        :class="{
                            active: representation.enabled,
                            muted: !representation.enabled,
                        }"
                        type="button"
                        :aria-pressed="representation.enabled"
                        @click="emit('toggle', { scope: 'representation', key: representation.key })"
                    >
                        <span class="static-option-label">{{ representation.label }}</span>
                        <strong class="filter-option-count">{{ representation.count }}</strong>
                    </button>
                </div>
            </div>

            <div v-if="model.resourceCategories.length" class="static-subsection">
                <h4>{{ model.ui.staticFilters.resourcesTitle }}</h4>
                <div
                    v-for="category in model.resourceCategories"
                    :key="category.key"
                    class="static-category"
                >
                    <div class="static-category-head">
                        <button
                            class="chip-button static-category-toggle"
                            :class="{ active: category.enabled, muted: !category.enabled }"
                            type="button"
                            :aria-pressed="category.enabled"
                            @click="emit('toggle', { scope: 'resourceCategory', key: category.key })"
                        >
                            <span class="static-option-label">{{ category.label }}</span>
                            <strong class="filter-option-count">{{ category.count }}</strong>
                        </button>
                        <button
                            class="button subtle small static-expand"
                            type="button"
                            :aria-expanded="isExpanded(category.key)"
                            :aria-label="`${category.label} - ${
                                isExpanded(category.key)
                                    ? model.ui.staticFilters.hideAll
                                    : model.ui.staticFilters.showAll
                            }`"
                            @click="toggleExpanded(category.key)"
                        >
                            <span aria-hidden="true">
                                {{ isExpanded(category.key) ? "-" : "+" }}
                            </span>
                        </button>
                    </div>

                    <div v-if="isExpanded(category.key)" class="chip-group static-chip-group nested">
                        <button
                            v-for="type in category.types"
                            :key="type.key"
                            class="chip-button"
                            :class="{ active: type.enabled, muted: !type.enabled }"
                            type="button"
                            :style="optionStyle(type)"
                            :aria-pressed="type.enabled"
                            @click="emit('toggle', { scope: 'resourceType', key: type.key })"
                        >
                            <span class="static-option-label">
                                <span class="static-swatch" aria-hidden="true"></span>
                                {{ type.label }}
                            </span>
                            <strong class="filter-option-count">{{ type.count }}</strong>
                        </button>
                    </div>
                </div>
            </div>

            <div
                v-for="section in model.placementSections"
                :key="section.layer"
                class="static-subsection"
            >
                <h4>{{ section.title }}</h4>
                <div class="chip-group static-chip-group">
                    <button
                        v-for="option in section.options"
                        :key="option.key"
                        class="chip-button"
                        :class="{ active: option.enabled, muted: !option.enabled }"
                        type="button"
                        :style="optionStyle(option)"
                        :aria-pressed="option.enabled"
                        @click="
                            emit('toggle', {
                                scope: 'placementGroup',
                                key: option.key,
                                layer: section.layer,
                            })
                        "
                    >
                        <span class="static-option-label">
                            <span class="static-swatch" aria-hidden="true"></span>
                            {{ option.label }}
                        </span>
                        <strong class="filter-option-count">{{ option.count }}</strong>
                    </button>
                </div>
            </div>
        </template>
    </div>
</template>

<style scoped>
.static-filters {
    display: grid;
    gap: 10px;
}

.static-filters-toolbar {
    display: grid;
    gap: 8px;
}

.static-search-field {
    display: block;
    min-width: 0;
}

.static-search {
    width: 100%;
    padding: 7px 10px;
    border-radius: 10px;
    border: 1px solid var(--border);
    background: rgba(8, 14, 26, 0.6);
    color: inherit;
    font: inherit;
}


.static-bulk-actions {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 8px;
}

.static-subsection {
    display: grid;
    gap: 6px;
}

.static-subsection h4 {
    margin: 0;
    font-size: 0.78rem;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: #9fb2d4;
}

.static-hint {
    margin: 0;
    font-size: 0.76rem;
    color: #8fa3c6;
}

.static-error {
    color: #fca5a5;
}

.static-chip-group {
    display: grid;
    grid-template-columns: 1fr;
    gap: 6px;
}

.static-chip-group.nested {
    padding-left: 10px;
}

.static-chip-group .chip-button {
    width: 100%;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
    text-align: left;
}

.static-option-label {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
}

.static-swatch {
    width: 10px;
    height: 10px;
    flex: 0 0 auto;
    border-radius: 3px;
    background: var(--static-swatch, #64748b);
    box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.25);
}

.static-category {
    display: grid;
    gap: 6px;
}

.static-category-head {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    gap: 6px;
    align-items: center;
}

.static-expand {
    min-width: 34px;
}

.filter-option-count {
    flex: 0 0 auto;
    min-width: 2.25rem;
    padding: 2px 8px;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.08);
    color: #dbe6ff;
    text-align: center;
    font-size: 0.76rem;
    line-height: 1.2;
}
</style>
