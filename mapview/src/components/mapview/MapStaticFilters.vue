<script setup lang="ts">
import { computed, ref } from "vue";

import type {
    MapStaticFiltersModel,
    StaticFilterCategory,
    StaticFilterOption,
    StaticFilterToggle,
} from "../../lib/types";

const props = defineProps<{
    model: MapStaticFiltersModel;
    /** Raw export layers are only meaningful in the Technical preset. */
    developerMode: boolean;
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

function swatchStyle(option: StaticFilterOption): Record<string, string> {
    return { "--swatch": option.color ?? "#64748b" };
}

function handleSearch(event: Event): void {
    emit("update:search", (event.target as HTMLInputElement).value);
}

function groupCount(options: StaticFilterOption[]): string {
    const enabled = options.filter((option) => option.enabled).length;
    return props.model.ui.filters.groupCount(enabled, options.length);
}

/** Catalog counts run into five digits; raw they read as one long number. */
function formatCount(value: number): string {
    return value.toLocaleString(props.model.ui.locale);
}

// A category is a master switch over its types: on with a few types turned off
// is neither "everything shown" nor "nothing shown", and a plain tick claimed
// the first. Mixed rows carry a dash instead.
function isMixed(category: StaticFilterCategory): boolean {
    if (!category.enabled || category.types.length === 0) return false;
    return category.types.some((type) => !type.enabled);
}

/** No bulk endpoint per group: replay the toggles not already in that state. */
function setOptionGroup(
    options: StaticFilterOption[],
    enabled: boolean,
    toggle: (option: StaticFilterOption) => StaticFilterToggle,
): void {
    for (const option of options) {
        if (option.enabled !== enabled) emit("toggle", toggle(option));
    }
}

function setResourceTypes(category: StaticFilterCategory, enabled: boolean): void {
    setOptionGroup(category.types, enabled, (option) => ({
        scope: "resourceType",
        key: option.key,
    }));
}

// Only the resource and placement lists honour the search; when both come back
// empty the remaining groups make it look like the search did nothing.
const searchHasNoMatch = computed(
    () =>
        props.model.search.trim().length > 0 &&
        props.model.resourceCategories.length === 0 &&
        (!props.developerMode || props.model.placementSections.length === 0),
);
</script>

<template>
    <div class="static-filters">
        <p v-if="!model.available" class="filter-section-help">
            {{ model.ui.staticFilters.unavailable }}
        </p>

        <template v-if="model.available">
            <!-- Search and bulk actions stay in reach while the lists scroll. -->
            <div class="static-toolbar">
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

            <p v-if="model.loading" class="filter-section-help">
                {{ model.ui.staticFilters.loading }}
            </p>
            <p v-else-if="model.error" class="filter-section-help static-error">
                {{ model.error }}
            </p>
            <p v-else-if="searchHasNoMatch" class="filter-section-help static-nomatch">
                {{ model.ui.staticFilters.noMatch }}
            </p>

            <section v-if="props.developerMode && model.layers.length" class="filter-group">
                <header class="filter-group-head">
                    <h4>{{ model.ui.staticFilters.layersTitle }}</h4>
                    <span class="filter-group-count">{{ groupCount(model.layers) }}</span>
                    <span class="filter-group-actions">
                        <button
                            class="group-action"
                            type="button"
                            @click="setOptionGroup(model.layers, true, (option) => ({ scope: 'layer', key: option.key }))"
                        >
                            {{ model.ui.filters.selectAll }}
                        </button>
                        <button
                            class="group-action"
                            type="button"
                            @click="setOptionGroup(model.layers, false, (option) => ({ scope: 'layer', key: option.key }))"
                        >
                            {{ model.ui.filters.selectNone }}
                        </button>
                    </span>
                </header>
                <div class="filter-rows">
                    <button
                        v-for="layer in model.layers"
                        :key="layer.key"
                        class="filter-row no-icon"
                        :class="{ active: layer.enabled, empty: layer.count === 0 }"
                        type="button"
                        :aria-pressed="layer.enabled"
                        @click="emit('toggle', { scope: 'layer', key: layer.key })"
                    >
                        <span class="filter-row-check" aria-hidden="true"></span>
                        <span class="filter-row-label" :title="layer.label">
                            {{ layer.label }}
                        </span>
                        <span class="filter-row-count">{{ formatCount(layer.count) }}</span>
                    </button>
                </div>
            </section>

            <section
                v-if="props.developerMode && model.representations.length"
                class="filter-group"
            >
                <header class="filter-group-head">
                    <h4>{{ model.ui.staticFilters.representationTitle }}</h4>
                    <span class="filter-group-count">
                        {{ groupCount(model.representations) }}
                    </span>
                </header>
                <p class="filter-section-help">
                    {{ model.ui.staticFilters.representationHelp }}
                </p>
                <div class="filter-rows">
                    <button
                        v-for="representation in model.representations"
                        :key="representation.key"
                        class="filter-row no-icon"
                        :class="{
                            active: representation.enabled,
                            empty: representation.count === 0,
                        }"
                        type="button"
                        :aria-pressed="representation.enabled"
                        @click="emit('toggle', { scope: 'representation', key: representation.key })"
                    >
                        <span class="filter-row-check" aria-hidden="true"></span>
                        <span class="filter-row-label" :title="representation.label">
                            {{ representation.label }}
                        </span>
                        <span class="filter-row-count">
                            {{ formatCount(representation.count) }}
                        </span>
                    </button>
                </div>
            </section>

            <section v-if="model.resourceCategories.length" class="filter-group">
                <header class="filter-group-head">
                    <h4>{{ model.ui.staticFilters.resourcesTitle }}</h4>
                    <span class="filter-group-count">
                        {{ groupCount(model.resourceCategories) }}
                    </span>
                    <span class="filter-group-actions">
                        <button
                            class="group-action"
                            type="button"
                            @click="setOptionGroup(model.resourceCategories, true, (option) => ({ scope: 'resourceCategory', key: option.key }))"
                        >
                            {{ model.ui.filters.selectAll }}
                        </button>
                        <button
                            class="group-action"
                            type="button"
                            @click="setOptionGroup(model.resourceCategories, false, (option) => ({ scope: 'resourceCategory', key: option.key }))"
                        >
                            {{ model.ui.filters.selectNone }}
                        </button>
                    </span>
                </header>

                <div
                    v-for="category in model.resourceCategories"
                    :key="category.key"
                    class="static-category"
                    :class="{ open: isExpanded(category.key) }"
                >
                    <div class="static-category-head">
                        <button
                            class="filter-row no-icon"
                            :class="{
                                active: category.enabled,
                                mixed: isMixed(category),
                                empty: category.count === 0,
                            }"
                            type="button"
                            :aria-pressed="isMixed(category) ? 'mixed' : category.enabled"
                            @click="emit('toggle', { scope: 'resourceCategory', key: category.key })"
                        >
                            <span class="filter-row-check" aria-hidden="true"></span>
                            <span class="filter-row-label" :title="category.label">
                                {{ category.label }}
                            </span>
                            <span class="filter-row-count">
                                {{ formatCount(category.count) }}
                            </span>
                        </button>
                        <button
                            class="static-expand"
                            type="button"
                            :aria-expanded="isExpanded(category.key)"
                            :title="
                                isExpanded(category.key)
                                    ? model.ui.staticFilters.collapse
                                    : model.ui.staticFilters.expand
                            "
                            :aria-label="`${category.label} - ${
                                isExpanded(category.key)
                                    ? model.ui.staticFilters.collapse
                                    : model.ui.staticFilters.expand
                            }`"
                            @click="toggleExpanded(category.key)"
                        >
                            <span
                                class="collapse-arrow"
                                :class="isExpanded(category.key) ? 'down' : 'right'"
                                aria-hidden="true"
                            ></span>
                        </button>
                    </div>

                    <div v-if="isExpanded(category.key)" class="static-nested">
                        <div class="filter-group-head static-nested-head">
                            <h4>{{ category.label }}</h4>
                            <span class="filter-group-count">
                                {{ groupCount(category.types) }}
                            </span>
                            <span class="filter-group-actions">
                                <button
                                    class="group-action"
                                    type="button"
                                    @click="setResourceTypes(category, true)"
                                >
                                    {{ model.ui.filters.selectAll }}
                                </button>
                                <button
                                    class="group-action"
                                    type="button"
                                    @click="setResourceTypes(category, false)"
                                >
                                    {{ model.ui.filters.selectNone }}
                                </button>
                            </span>
                        </div>

                        <div class="filter-rows">
                            <button
                                v-for="type in category.types"
                                :key="type.key"
                                class="filter-row"
                                :class="{ active: type.enabled, empty: type.count === 0 }"
                                type="button"
                                :style="swatchStyle(type)"
                                :aria-pressed="type.enabled"
                                @click="emit('toggle', { scope: 'resourceType', key: type.key })"
                            >
                                <span class="filter-row-check" aria-hidden="true"></span>
                                <span class="filter-row-swatch" aria-hidden="true"></span>
                                <span class="filter-row-label" :title="type.label">
                                    {{ type.label }}
                                </span>
                                <span class="filter-row-count">
                                    {{ formatCount(type.count) }}
                                </span>
                            </button>
                        </div>
                    </div>
                </div>
            </section>

            <!-- Which machine goes on the vein. Filtering by it answers "where
                 can I put a laser drill?" without reading each ore in turn. -->
            <section v-if="model.extractors.length" class="filter-group">
                <header class="filter-group-head">
                    <h4>{{ model.ui.staticFilters.extractorTitle }}</h4>
                    <span class="filter-group-count">
                        {{ groupCount(model.extractors) }}
                    </span>
                    <span class="filter-group-actions">
                        <button
                            class="group-action"
                            type="button"
                            @click="setOptionGroup(model.extractors, true, (option) => ({ scope: 'extractor', key: option.key }))"
                        >
                            {{ model.ui.filters.selectAll }}
                        </button>
                        <button
                            class="group-action"
                            type="button"
                            @click="setOptionGroup(model.extractors, false, (option) => ({ scope: 'extractor', key: option.key }))"
                        >
                            {{ model.ui.filters.selectNone }}
                        </button>
                    </span>
                </header>
                <p class="filter-section-help">
                    {{ model.ui.staticFilters.extractorHelp }}
                </p>
                <div class="filter-rows">
                    <button
                        v-for="extractor in model.extractors"
                        :key="extractor.key"
                        class="filter-row"
                        :class="{ active: extractor.enabled, empty: extractor.count === 0 }"
                        type="button"
                        :style="swatchStyle(extractor)"
                        :aria-pressed="extractor.enabled"
                        @click="emit('toggle', { scope: 'extractor', key: extractor.key })"
                    >
                        <span class="filter-row-check" aria-hidden="true"></span>
                        <span class="filter-row-swatch" aria-hidden="true"></span>
                        <span class="filter-row-label" :title="extractor.label">
                            {{ extractor.label }}
                        </span>
                        <span class="filter-row-count">
                            {{ formatCount(extractor.count) }}
                        </span>
                    </button>
                </div>
            </section>

            <!-- Ore quality applies across every ore, so it sits beside the
                 resource list rather than inside one of its categories. -->
            <section v-if="model.orePurities.length" class="filter-group">
                <header class="filter-group-head">
                    <h4>{{ model.ui.staticFilters.purityTitle }}</h4>
                    <span class="filter-group-count">
                        {{ groupCount(model.orePurities) }}
                    </span>
                    <span class="filter-group-actions">
                        <button
                            class="group-action"
                            type="button"
                            @click="setOptionGroup(model.orePurities, true, (option) => ({ scope: 'orePurity', key: option.key }))"
                        >
                            {{ model.ui.filters.selectAll }}
                        </button>
                        <button
                            class="group-action"
                            type="button"
                            @click="setOptionGroup(model.orePurities, false, (option) => ({ scope: 'orePurity', key: option.key }))"
                        >
                            {{ model.ui.filters.selectNone }}
                        </button>
                    </span>
                </header>
                <p class="filter-section-help">{{ model.ui.staticFilters.purityHelp }}</p>
                <div class="filter-rows">
                    <button
                        v-for="purity in model.orePurities"
                        :key="purity.key"
                        class="filter-row"
                        :class="{ active: purity.enabled, empty: purity.count === 0 }"
                        type="button"
                        :style="swatchStyle(purity)"
                        :aria-pressed="purity.enabled"
                        @click="emit('toggle', { scope: 'orePurity', key: purity.key })"
                    >
                        <span class="filter-row-check" aria-hidden="true"></span>
                        <span class="filter-row-swatch" aria-hidden="true"></span>
                        <span class="filter-row-label" :title="purity.label">
                            {{ purity.label }}
                        </span>
                        <span class="filter-row-count">{{ formatCount(purity.count) }}</span>
                    </button>
                </div>
            </section>

            <section
                v-for="section in props.developerMode ? model.placementSections : []"
                :key="section.layer"
                class="filter-group"
            >
                <header class="filter-group-head">
                    <h4>{{ section.title }}</h4>
                    <span class="filter-group-count">{{ groupCount(section.options) }}</span>
                    <span class="filter-group-actions">
                        <button
                            class="group-action"
                            type="button"
                            @click="setOptionGroup(section.options, true, (option) => ({ scope: 'placementGroup', key: option.key, layer: section.layer }))"
                        >
                            {{ model.ui.filters.selectAll }}
                        </button>
                        <button
                            class="group-action"
                            type="button"
                            @click="setOptionGroup(section.options, false, (option) => ({ scope: 'placementGroup', key: option.key, layer: section.layer }))"
                        >
                            {{ model.ui.filters.selectNone }}
                        </button>
                    </span>
                </header>
                <div class="filter-rows">
                    <button
                        v-for="option in section.options"
                        :key="option.key"
                        class="filter-row"
                        :class="{ active: option.enabled, empty: option.count === 0 }"
                        type="button"
                        :style="swatchStyle(option)"
                        :aria-pressed="option.enabled"
                        @click="
                            emit('toggle', {
                                scope: 'placementGroup',
                                key: option.key,
                                layer: section.layer,
                            })
                        "
                    >
                        <span class="filter-row-check" aria-hidden="true"></span>
                        <span class="filter-row-swatch" aria-hidden="true"></span>
                        <span class="filter-row-label" :title="option.label">
                            {{ option.label }}
                        </span>
                        <span class="filter-row-count">{{ formatCount(option.count) }}</span>
                    </button>
                </div>
            </section>
        </template>
    </div>
</template>

<style scoped>
.static-filters {
    display: grid;
    align-content: start;
    gap: 14px;
}

/* Sticky inside the scrolling tab body: with ~80k catalog elements the search
   used to scroll out of reach after the first category. */
.static-toolbar {
    position: sticky;
    top: 0;
    z-index: 2;
    display: grid;
    gap: 6px;
    padding: 12px 0 8px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    background: rgb(13, 21, 38);
}

.static-search-field {
    display: block;
    min-width: 0;
}

.static-search {
    width: 100%;
    min-height: 34px;
    padding: 7px 10px;
    border: 1px solid var(--border);
    background: rgba(8, 14, 26, 0.6);
    color: inherit;
    font: inherit;
}

.static-bulk-actions {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 6px;
}

.filter-section-help {
    margin: 0;
    color: var(--muted);
    font-size: 0.78rem;
}

.static-error {
    color: #fca5a5;
}

.static-nomatch {
    color: var(--amber);
}

.static-category {
    display: grid;
    gap: 3px;
}

.static-category + .static-category {
    margin-top: 3px;
}

.static-category-head {
    display: grid;
    grid-template-columns: minmax(0, 1fr) 34px;
    gap: 3px;
    align-items: stretch;
}

.static-expand {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-height: 38px;
    border: 1px solid transparent;
    background: rgba(255, 255, 255, 0.028);
    color: var(--muted);
    cursor: pointer;
    transition: background 0.14s, color 0.14s, border-color 0.14s;
}

.static-expand:hover {
    background: rgba(255, 255, 255, 0.07);
    border-color: var(--border);
    color: var(--text);
}

/* Indented and rule-marked, so a type row is never mistaken for a category. */
.static-nested {
    display: grid;
    gap: 6px;
    margin: 3px 0 6px 12px;
    padding-left: 10px;
    border-left: 1px solid rgba(34, 211, 238, 0.24);
}

.static-nested-head h4 {
    color: var(--dim);
}
</style>
