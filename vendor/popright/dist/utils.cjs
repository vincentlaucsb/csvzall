"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.normalizeOptions = normalizeOptions;
exports.normalizeTargets = normalizeTargets;
exports.isEventTarget = isEventTarget;
exports.resolveItems = resolveItems;
exports.normalizeItems = normalizeItems;
exports.hasSelectableOrLabelContent = hasSelectableOrLabelContent;
exports.selectableIndexes = selectableIndexes;
exports.firstSelectableIndex = firstSelectableIndex;
exports.lastSelectableIndex = lastSelectableIndex;
exports.isSelectable = isSelectable;
exports.isChildMenuItem = isChildMenuItem;
exports.createMenuContext = createMenuContext;
exports.containsEventTarget = containsEventTarget;
exports.applyStyle = applyStyle;
exports.composeClass = composeClass;
exports.splitClasses = splitClasses;
exports.toCssValue = toCssValue;
exports.kebabCase = kebabCase;
exports.canUseDom = canUseDom;
function normalizeOptions(options) {
    return {
        trigger: "contextmenu",
        placement: "cursor",
        side: "bottom",
        align: "start",
        sideOffset: 0,
        alignOffset: 0,
        strategy: "fixed",
        closeOnSelect: true,
        closeOnBlur: true,
        closeOnEscape: true,
        closeOnScroll: true,
        closeOnResize: true,
        modal: false,
        collisionPadding: 8,
        menuType: "context",
        ...options
    };
}
function normalizeTargets(target) {
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
function isEventTarget(value) {
    return (Boolean(value) &&
        typeof value.addEventListener === "function" &&
        typeof value.removeEventListener === "function");
}
function resolveItems(items, context) {
    if (typeof items === "function") {
        const resolved = items(context);
        if (resolved && typeof resolved.then === "function") {
            throw new Error("Popright does not support async item resolvers yet.");
        }
        return resolved;
    }
    return items;
}
function normalizeItems(items) {
    if (!Array.isArray(items)) {
        return [];
    }
    return items.filter((item) => Boolean(item) && !("hidden" in item && item.hidden));
}
function hasSelectableOrLabelContent(items) {
    return items.some((item) => item.type !== "separator");
}
function selectableIndexes(items) {
    return items.flatMap((item, index) => (isSelectable(item) ? [index] : []));
}
function firstSelectableIndex(items) {
    return selectableIndexes(items)[0] ?? -1;
}
function lastSelectableIndex(items) {
    const indexes = selectableIndexes(items);
    return indexes[indexes.length - 1] ?? -1;
}
function isSelectable(item) {
    return !!item && (item.type === undefined || item.type === "item" || item.type === "submenu") && !item.disabled;
}
function isChildMenuItem(item) {
    return item.type !== "submenu";
}
function createMenuContext(input) {
    return {
        triggerEvent: input.triggerEvent,
        target: input.target,
        x: input.x,
        y: input.y,
        data: input.context
    };
}
function containsEventTarget(candidate, target) {
    if (!target) {
        return false;
    }
    if (candidate === target) {
        return true;
    }
    return candidate instanceof Node && target instanceof Node && candidate.contains(target);
}
function applyStyle(element, style) {
    if (!style) {
        return;
    }
    for (const [key, value] of Object.entries(style)) {
        if (value !== undefined && value !== null) {
            element.style.setProperty(kebabCase(key), String(value));
        }
    }
}
function composeClass(...values) {
    return values.filter(Boolean).join(" ");
}
function splitClasses(value) {
    return String(value).split(/\s+/).filter(Boolean);
}
function toCssValue(value) {
    return typeof value === "number" ? `${value}px` : value;
}
function kebabCase(value) {
    return value.replace(/[A-Z]/g, (match) => `-${match.toLowerCase()}`);
}
function canUseDom() {
    return typeof document !== "undefined";
}
