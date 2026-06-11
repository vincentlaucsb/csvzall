import type { MenuController } from "./MenuController.js";
import type { CloseReason, ContextMenuOptions, MenuContext, MenuItem, NormalizedContextMenuOptions, OpenInput } from "./types.js";
type Cleanup = () => void;
interface ContextMenuInternalOptions {
    /** Parent root when this instance is acting as an unregistered submenu. */
    parent?: ContextMenu;
    /** Submenus are real ContextMenu objects, but they do not participate as root menus. */
    register?: boolean;
}
/**
 * Owns one context menu instance: target listeners, resolved items, DOM, focus,
 * global cleanup, and any open submenu branch.
 *
 * Root menus register with `MenuController`; submenus reuse the same behavior
 * but opt out of controller registration so the controller's active-menu
 * invariant remains about root menus only.
 */
export declare class ContextMenu {
    readonly controller: MenuController;
    /** Native targets that may trigger this menu. Empty means programmatic/manual only. */
    readonly targets: EventTarget[];
    options: NormalizedContextMenuOptions;
    /** The currently rendered menu root. Null means no DOM or global listeners should exist. */
    root: HTMLElement | null;
    items: MenuItem[];
    /** Index into `items`; disabled items, labels, and separators are skipped. */
    activeIndex: number;
    /** Open-time context is retained so selection callbacks see the original trigger data. */
    currentContext: MenuContext | null;
    destroyed: boolean;
    /** Assigned by the controller and used only for same-depth native-event tie-breaking. */
    registeredAt: number;
    /** Element that had focus before open; restored on close when it is still focusable. */
    previousFocus: Element | null;
    /** Theme subscriptions exist only while the DOM root is open. */
    unsubscribeTheme: Cleanup | null;
    /** Target listeners live for the menu instance lifetime. */
    targetCleanups: Cleanup[];
    /** Global listeners live only while the menu is open. */
    globalCleanups: Cleanup[];
    /** Only one child submenu branch may be open from a menu at a time. */
    childMenu: ContextMenu | null;
    readonly parent: ContextMenu | null;
    readonly registeredWithController: boolean;
    /**
     * Tracks the submenu item source that produced the current child.
     * Re-entering the already-open trigger should preserve the submenu instead
     * of tearing down and recreating its DOM.
     */
    private itemsSource;
    constructor(controller: MenuController, target: unknown, options: ContextMenuOptions, internal?: ContextMenuInternalOptions);
    requestOpen(input: OpenInput): void;
    openNow(input: OpenInput): void;
    close(reason?: CloseReason, nativeEvent?: Event): void;
    update(options: Partial<ContextMenuOptions>): void;
    destroy(): void;
    containsTarget(target: EventTarget | null): boolean;
    getTargetDepth(eventTarget: EventTarget | null): number;
    hasTargets(): boolean;
    canOpenFromNativeEvent(event: Event): boolean;
    getClosestTarget(eventTarget: EventTarget | null): Element | undefined;
    positionRoot(root: HTMLElement, input: OpenInput): void;
    getPreferredPosition(input: OpenInput, rect: DOMRect): {
        left: number;
        top: number;
        fallbackLeft?: number;
        fallbackTop?: number;
    };
    get isOpen(): boolean;
    attachTargets(): void;
    attachGlobalListeners(): void;
    onPointerMove(event: PointerEvent): void;
    onClick(event: MouseEvent): void;
    onKeyDown(event: KeyboardEvent): void;
    moveActive(delta: number): void;
    selectIndex(index: number, nativeEvent: Event): void;
    updateActiveDom(): void;
    containsRoot(target: EventTarget | null): boolean;
    openSubmenu(index: number): void;
    closeChild(reason?: CloseReason, nativeEvent?: Event): void;
}
export {};
//# sourceMappingURL=ContextMenu.d.ts.map