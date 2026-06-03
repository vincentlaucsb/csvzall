/*!
 * Popright
 * Copyright (c) 2026 Vincent La
 * Released under the MIT License.
 */
import { BUILT_IN_THEME } from "./constants.js";
export function normalizeThemeInput(input, fallback = BUILT_IN_THEME) {
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
export function cloneTheme(theme) {
    return {
        ...theme,
        classes: { ...theme.classes },
        styles: { ...theme.styles },
        tokens: { ...theme.tokens }
    };
}
