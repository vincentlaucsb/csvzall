/*!
 * Popright
 * Copyright (c) 2026 Vincent La
 * Released under the MIT License.
 */
export function normalizeOptions(options) {
    return {
        trigger: "contextmenu",
        placement: "cursor",
        strategy: "fixed",
        closeOnSelect: true,
        closeOnBlur: true,
        closeOnEscape: true,
        closeOnScroll: true,
        closeOnResize: true,
        modal: false,
        collisionPadding: 8,
        ...options
    };
}
export function normalizeTargets(target) {
    if (!target || !canUseDom()) {
        return [];
    }
    if (isEventTarget(target)) {
        return [target];
    }
    if (typeof target[Symbol.iterator] === "function") {
        return [...target].filter(isEventTarget);
    }
    return [];
}
export function isEventTarget(value) {
    return (Boolean(value) &&
        typeof value.addEventListener === "function" &&
        typeof value.removeEventListener === "function");
}
export function resolveItems(items, context) {
    if (typeof items === "function") {
        const resolved = items(context);
        if (resolved && typeof resolved.then === "function") {
            throw new Error("Popright does not support async item resolvers yet.");
        }
        return resolved;
    }
    return items;
}
export function normalizeItems(items) {
    if (!Array.isArray(items)) {
        return [];
    }
    return items.filter((item) => Boolean(item) && !("hidden" in item && item.hidden));
}
export function hasSelectableOrLabelContent(items) {
    return items.some((item) => item.type !== "separator");
}
export function selectableIndexes(items) {
    return items.flatMap((item, index) => (isSelectable(item) ? [index] : []));
}
export function firstSelectableIndex(items) {
    return selectableIndexes(items)[0] ?? -1;
}
export function lastSelectableIndex(items) {
    const indexes = selectableIndexes(items);
    return indexes[indexes.length - 1] ?? -1;
}
export function isSelectable(item) {
    return !!item && (item.type === undefined || item.type === "item" || item.type === "submenu") && !item.disabled;
}
export function isChildMenuItem(item) {
    return item.type !== "submenu";
}
export function createMenuContext(input) {
    return {
        triggerEvent: input.triggerEvent,
        target: input.target,
        x: input.x,
        y: input.y,
        data: input.context
    };
}
export function containsEventTarget(candidate, target) {
    if (!target) {
        return false;
    }
    if (candidate === target) {
        return true;
    }
    return candidate instanceof Node && target instanceof Node && candidate.contains(target);
}
export function applyStyle(element, style) {
    if (!style) {
        return;
    }
    for (const [key, value] of Object.entries(style)) {
        if (value !== undefined && value !== null) {
            element.style.setProperty(kebabCase(key), String(value));
        }
    }
}
export function composeClass(...values) {
    return values.filter(Boolean).join(" ");
}
export function splitClasses(value) {
    return String(value).split(/\s+/).filter(Boolean);
}
export function toCssValue(value) {
    return typeof value === "number" ? `${value}px` : value;
}
export function kebabCase(value) {
    return value.replace(/[A-Z]/g, (match) => `-${match.toLowerCase()}`);
}
export function canUseDom() {
    return typeof document !== "undefined";
}
