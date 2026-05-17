import type { ContextMenuOptions, MenuActionItem, MenuContext, MenuItem, MenuSubmenuItem, NormalizedContextMenuOptions } from "./types.js";
export interface MenuRootOptions {
    ownerDocument: Document;
    options: NormalizedContextMenuOptions;
    onKeyDown: (event: KeyboardEvent) => void;
    onPointerMove: (event: PointerEvent) => void;
    onClick: (event: MouseEvent) => void;
}
export interface RenderMenuItemsOptions {
    items: MenuItem[];
    context: MenuContext;
    options: NormalizedContextMenuOptions;
    onItemEnter: (index: number) => void;
}
/** Creates the DOM root that owns keyboard, pointer, and click delegation. */
export declare function createMenuRoot({ ownerDocument, options, onKeyDown, onPointerMove, onClick }: MenuRootOptions): HTMLElement;
/**
 * Renders normalized menu data into stable DOM/data-attribute contracts.
 *
 * The renderer is intentionally dumb: it does not own active state, callbacks,
 * positioning, or item resolution. Keeping it a pure DOM projection makes the
 * controller/menu lifecycle easier to reason about and simpler to test.
 */
export declare function renderMenuItems(root: HTMLElement, { items, context, options, onItemEnter }: RenderMenuItemsOptions): void;
export declare function updateActiveDom(root: HTMLElement, items: MenuItem[], activeIndex: number, options: NormalizedContextMenuOptions): void;
/** Computes the structural classes for an actionable row without touching DOM. */
export declare function getItemClassName(item: MenuActionItem | MenuSubmenuItem, options: Pick<ContextMenuOptions, "classes">): string;
export interface RenderableItemState {
    kind: "separator" | "header" | "label" | "item" | "skipped";
    className: string;
    role?: string;
    align?: string;
    disabled: boolean;
    active: boolean;
}
/**
 * Exposes the same render classification used by `renderMenuItems` for unit
 * tests, without forcing tests to parse DOM strings.
 */
export declare function getRenderableItemState(item: MenuItem, index: number, activeIndex: number, options: Pick<ContextMenuOptions, "classes">): RenderableItemState;
//# sourceMappingURL=render.d.ts.map