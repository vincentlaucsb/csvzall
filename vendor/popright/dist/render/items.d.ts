import { MenuItemType } from "../types.js";
import type { ContextMenuOptions, MenuActionItem, MenuIcon, MenuItem, MenuRenderContext, MenuSubmenuItem, NormalizedContextMenuOptions } from "../types.js";
import type { RenderItemOptions, RenderMenuItemsOptions } from "./types.js";
/**
 * Renders normalized menu data into stable DOM/data-attribute contracts.
 *
 * The renderer is intentionally dumb: it does not own active state, callbacks,
 * positioning, or item resolution. Keeping it a pure DOM projection makes the
 * controller/menu lifecycle easier to reason about and simpler to test.
 */
export declare function renderMenuItems(root: HTMLElement, { items, context, options, onItemEnter }: RenderMenuItemsOptions): void;
export declare function renderSeparator({ root, item, options }: RenderItemOptions): void;
export declare function renderHeader({ root, item, options }: RenderItemOptions): void;
export declare function renderLabel({ root, item, options }: RenderItemOptions): void;
export declare function renderActionRow({ root, item, index, context, options, onItemEnter, hasIcons }: RenderItemOptions): void;
export declare function renderShortcut(ownerDocument: Document, item: MenuActionItem, options: NormalizedContextMenuOptions): HTMLElement;
export declare function renderSubmenuArrow(ownerDocument: Document, dir: string, options: NormalizedContextMenuOptions): HTMLElement;
/**
 * Normalizes all icon inputs into a wrapper span owned by this menu.
 *
 * Node inputs are cloned so passing the same HTMLElement to multiple menu items
 * cannot move it between rows.
 */
export declare function renderIcon(ownerDocument: Document, iconInput: MenuIcon | undefined, context: MenuRenderContext): HTMLElement;
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
/** Only action and submenu items become focusable/selectable menu rows today. */
export declare function isRenderableMenuItem(item: MenuItem): item is MenuActionItem | MenuSubmenuItem;
export declare function getMenuItemType(item: MenuItem): MenuItemType;
//# sourceMappingURL=items.d.ts.map