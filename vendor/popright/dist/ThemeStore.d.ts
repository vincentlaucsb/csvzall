import type { ContextMenuTheme, ContextMenuThemeInput, ContextMenuThemeStore } from "./types.js";
/**
 * Framework-agnostic global theme store used by every open menu.
 *
 * The store never exposes its internal theme object directly. Callers always
 * receive a clone so an accidental mutation cannot silently change future menu
 * renders without notifying subscribers.
 */
export declare class ThemeStore implements ContextMenuThemeStore {
    #private;
    /** Returns a defensive copy of the current normalized theme. */
    get(): ContextMenuTheme;
    /** Normalizes string, object, or store input before notifying live menus. */
    set(theme: ContextMenuThemeInput): void;
    /** Applies an atomic update against a cloned snapshot of the current theme. */
    update(updater: (theme: ContextMenuTheme) => ContextMenuTheme): void;
    /**
     * Subscribes to future theme changes and immediately sends the current value.
     * Immediate delivery lets open menus share one update path for initial mount
     * and later global theme changes.
     */
    subscribe(listener: (theme: ContextMenuTheme) => void): () => void;
}
export declare const contextMenuTheme: ThemeStore;
//# sourceMappingURL=ThemeStore.d.ts.map