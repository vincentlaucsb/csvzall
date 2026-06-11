"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.normalizeThemeInput = normalizeThemeInput;
exports.cloneTheme = cloneTheme;
const constants_js_1 = require("./constants.cjs");
function normalizeThemeInput(input, fallback = constants_js_1.BUILT_IN_THEME) {
    if (!input) {
        return cloneTheme(fallback);
    }
    if (typeof input === "string") {
        return { ...cloneTheme(fallback), mode: input === "system" ? "automatic" : input };
    }
    if (isThemeStoreInput(input)) {
        return normalizeThemeInput(input.get(), fallback);
    }
    return {
        ...cloneTheme(fallback),
        ...input,
        classes: { ...fallback.classes, ...input.classes },
        styles: { ...fallback.styles, ...input.styles },
        tokens: { ...fallback.tokens, ...input.tokens }
    };
}
function isThemeStoreInput(input) {
    return "get" in input && typeof input.get === "function";
}
function cloneTheme(theme) {
    return {
        ...theme,
        classes: { ...theme.classes },
        styles: { ...theme.styles },
        tokens: { ...theme.tokens }
    };
}
