import type { MenuContext, MenuItem, NormalizedContextMenuOptions } from "../types.js";
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
export interface RenderItemOptions extends RenderMenuItemsOptions {
    root: HTMLElement;
    item: MenuItem;
    index: number;
    hasIcons: boolean;
}
export type MenuItemRenderer = (options: RenderItemOptions) => void;
//# sourceMappingURL=types.d.ts.map