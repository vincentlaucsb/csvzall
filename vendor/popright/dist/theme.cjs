"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.applyTheme = applyTheme;
const ThemeStore_js_1 = require("./ThemeStore.cjs");
const theme_utils_js_1 = require("./theme-utils.cjs");
const utils_js_1 = require("./utils.cjs");
function applyTheme(root, localThemeInput) {
    const globalTheme = ThemeStore_js_1.contextMenuTheme.get();
    const localTheme = (0, theme_utils_js_1.normalizeThemeInput)(localThemeInput ?? globalTheme, globalTheme);
    root.dataset.poprightTheme = localTheme.mode === "system" ? "automatic" : localTheme.mode ?? "automatic";
    root.className = (0, utils_js_1.composeClass)(root.className, localTheme.className);
    const tokens = { ...globalTheme.tokens, ...localTheme.tokens };
    for (const [key, value] of Object.entries(tokens)) {
        if (value !== undefined) {
            root.style.setProperty(`--popright-${(0, utils_js_1.kebabCase)(key)}`, String(value));
        }
    }
    (0, utils_js_1.applyStyle)(root, globalTheme.styles?.menu);
    (0, utils_js_1.applyStyle)(root, localTheme.styles?.menu);
}
