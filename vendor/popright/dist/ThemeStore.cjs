"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.contextMenuTheme = exports.ThemeStore = void 0;
const constants_js_1 = require("./constants.cjs");
const theme_utils_js_1 = require("./theme-utils.cjs");
/**
 * Framework-agnostic global theme store used by every open menu.
 *
 * The store never exposes its internal theme object directly. Callers always
 * receive a clone so an accidental mutation cannot silently change future menu
 * renders without notifying subscribers.
 */
class ThemeStore {
    /** Current normalized global theme; cloned on every read and notification. */
    #theme = constants_js_1.BUILT_IN_THEME;
    /** Open menus subscribe while mounted so theme changes can update live DOM. */
    #listeners = new Set();
    /** Returns a defensive copy of the current normalized theme. */
    get() {
        return (0, theme_utils_js_1.cloneTheme)(this.#theme);
    }
    /** Normalizes string, object, or store input before notifying live menus. */
    set(theme) {
        this.#theme = (0, theme_utils_js_1.normalizeThemeInput)(theme, this.#theme);
        this.#emit();
    }
    /** Applies an atomic update against a cloned snapshot of the current theme. */
    update(updater) {
        this.set(updater(this.get()));
    }
    /**
     * Subscribes to future theme changes and immediately sends the current value.
     * Immediate delivery lets open menus share one update path for initial mount
     * and later global theme changes.
     */
    subscribe(listener) {
        this.#listeners.add(listener);
        listener(this.get());
        return () => this.#listeners.delete(listener);
    }
    #emit() {
        const theme = this.get();
        for (const listener of this.#listeners) {
            listener(theme);
        }
    }
}
exports.ThemeStore = ThemeStore;
exports.contextMenuTheme = new ThemeStore();
