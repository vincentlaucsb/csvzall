/*!
 * Popright
 * Copyright (c) 2026 Vincent La
 * Released under the MIT License.
 */
import { contextMenuTheme } from "./ThemeStore.js";
import { normalizeThemeInput } from "./theme-utils.js";
import { applyStyle, composeClass, kebabCase } from "./utils.js";
export function applyTheme(root, localThemeInput) {
    const globalTheme = contextMenuTheme.get();
    const localTheme = normalizeThemeInput(localThemeInput ?? globalTheme, globalTheme);
    root.dataset.poprightTheme = localTheme.mode ?? "system";
    root.className = composeClass(root.className, localTheme.className);
    const tokens = { ...globalTheme.tokens, ...localTheme.tokens };
    for (const [key, value] of Object.entries(tokens)) {
        if (value !== undefined) {
            root.style.setProperty(`--popright-${kebabCase(key)}`, String(value));
        }
    }
    applyStyle(root, globalTheme.styles?.menu);
    applyStyle(root, localTheme.styles?.menu);
}
