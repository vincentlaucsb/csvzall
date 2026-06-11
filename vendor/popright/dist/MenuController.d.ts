import type { CloseReason, OpenInput } from "./types.js";
/**
 * Root menu behavior owned by `MenuController`.
 *
 * The controller deliberately depends on this narrow contract rather than a
 * concrete class so context menus, dropdowns, and future menu surfaces can
 * share the same active-menu invariant without forcing a fixed inheritance
 * tree.
 */
export interface ControlledMenu {
    /** The rendered root is not used by the controller, but is useful for tests and diagnostics. */
    readonly root: HTMLElement | null;
    /** Assigned by the controller and used for same-depth native-event tie-breaking. */
    registeredAt: number;
    openNow(input: OpenInput): void;
    close(reason?: CloseReason, nativeEvent?: Event): void;
    getTargetDepth(target: EventTarget | null): number;
    hasTargets(): boolean;
    canOpenFromNativeEvent(event: Event): boolean;
    getClosestTarget(target: EventTarget | null): Element | undefined;
}
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
    register(menu: ControlledMenu): void;
    unregister(menu: ControlledMenu): void;
    requestOpen(menu: ControlledMenu, input: OpenInput): void;
    closeActive(reason?: CloseReason, nativeEvent?: Event): void;
    setActive(menu: ControlledMenu): void;
    clearActive(menu: ControlledMenu): void;
    get activeMenu(): ControlledMenu | null;
}
//# sourceMappingURL=MenuController.d.ts.map