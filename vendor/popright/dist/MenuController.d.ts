import type { ContextMenu } from "./ContextMenu.js";
import type { CloseReason, OpenInput } from "./types.js";
/**
 * Coordinates all root context menus that share a document-level interaction space.
 *
 * The controller owns the "only one active root menu" invariant. Individual
 * `ContextMenu` instances own rendering and local state, but every public open
 * request must pass through this object so nested or overlapping targets do not
 * flicker competing menus open and closed during the same native event.
 */
export declare class MenuController {
    #private;
    register(menu: ContextMenu): void;
    unregister(menu: ContextMenu): void;
    requestOpen(menu: ContextMenu, input: OpenInput): void;
    closeActive(reason?: CloseReason, nativeEvent?: Event): void;
    setActive(menu: ContextMenu): void;
    clearActive(menu: ContextMenu): void;
    get activeMenu(): ContextMenu | null;
}
//# sourceMappingURL=MenuController.d.ts.map