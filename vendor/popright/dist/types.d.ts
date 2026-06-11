/** Reasons emitted when a root menu closes. */
export type CloseReason = "select" | "escape" | "outside-pointer" | "blur" | "scroll" | "resize" | "manual" | "destroy" | "reopen";
/**
 * Coordinates and metadata for programmatic or native-event opens.
 *
 * `context` is user data carried through to item resolvers and selection
 * callbacks; it is not interpreted by core.
 */
export interface OpenInput {
    x?: number;
    y?: number;
    target?: Element;
    triggerEvent?: Event;
    context?: unknown;
}
/** Open-time context passed to resolvers, callbacks, and render helpers. */
export interface MenuContext {
    triggerEvent?: Event;
    target?: Element;
    x?: number;
    y?: number;
    data?: unknown;
}
/** Public handle returned by `createContextMenu`. */
export interface ContextMenuInstance {
    open(input?: OpenInput): void;
    close(reason?: CloseReason): void;
    update(options: Partial<ContextMenuOptions>): void;
    destroy(): void;
    readonly isOpen: boolean;
    readonly root: HTMLElement | null;
}
/**
 * Configuration for one root context menu.
 *
 * Options describe behavior and rendering. Core still owns DOM creation, event
 * listeners, focus management, and cleanup for the instance.
 */
export interface ContextMenuOptions {
    /** Static items or a synchronous resolver evaluated at open time. */
    items: MenuItemsInput;
    id?: string;
    context?: unknown | ((event: Event) => unknown);
    onSelect?: (event: MenuSelectEvent) => void;
    onOpen?: (event: MenuOpenEvent) => void;
    onClose?: (event: MenuCloseEvent) => void;
    onBeforeOpen?: (event: MenuBeforeOpenEvent) => boolean | void;
    /** Native target event. `contextmenu` is default; `click` is opt-in. */
    trigger?: "contextmenu" | "click" | "manual";
    placement?: "cursor" | "target";
    side?: "top" | "right" | "bottom" | "left";
    align?: "start" | "center" | "end";
    sideOffset?: number;
    alignOffset?: number;
    strategy?: "fixed" | "absolute";
    closeOnSelect?: boolean;
    closeOnBlur?: boolean;
    closeOnEscape?: boolean;
    /** Scroll invalidates pointer/target geometry, so menus close by default. */
    closeOnScroll?: boolean;
    /** Resize invalidates viewport collision math, so menus close by default. */
    closeOnResize?: boolean;
    modal?: boolean;
    collisionPadding?: number;
    maxHeight?: number | string;
    minWidth?: number | string;
    zIndex?: number;
    className?: string;
    classes?: ContextMenuClassNames;
    styles?: ContextMenuStyles;
    theme?: ContextMenuThemeInput;
    portal?: Element | false;
    dir?: "ltr" | "rtl";
    menuType?: "context" | "dropdown";
}
export interface DropdownMenuOptions extends Omit<ContextMenuOptions, "trigger" | "placement" | "menuType"> {
    trigger?: "click" | "manual";
    placement?: "target";
}
/** Internal option shape after defaults have been applied. */
export type NormalizedContextMenuOptions = Required<Pick<ContextMenuOptions, "trigger" | "placement" | "strategy" | "side" | "align" | "sideOffset" | "alignOffset" | "closeOnSelect" | "closeOnBlur" | "closeOnEscape" | "closeOnScroll" | "closeOnResize" | "modal" | "collisionPadding">> & Omit<ContextMenuOptions, "trigger" | "placement" | "strategy" | "side" | "align" | "sideOffset" | "alignOffset" | "closeOnSelect" | "closeOnBlur" | "closeOnEscape" | "closeOnScroll" | "closeOnResize" | "modal" | "collisionPadding">;
/** Optional class hooks composed with Popright's structural classes. */
export interface ContextMenuClassNames {
    menu?: string;
    item?: string;
    itemActive?: string;
    itemDisabled?: string;
    itemDanger?: string;
    separator?: string;
    header?: string;
    label?: string;
    icon?: string;
    shortcut?: string;
    submenu?: string;
    submenuTrigger?: string;
}
/** Inline-style escape hatches for generated or highly local customization. */
export interface ContextMenuStyles {
    menu?: Partial<CSSStyleDeclaration>;
    item?: Partial<CSSStyleDeclaration>;
    itemActive?: Partial<CSSStyleDeclaration>;
    itemDisabled?: Partial<CSSStyleDeclaration>;
    itemDanger?: Partial<CSSStyleDeclaration>;
    separator?: Partial<CSSStyleDeclaration>;
    header?: Partial<CSSStyleDeclaration>;
    label?: Partial<CSSStyleDeclaration>;
    icon?: Partial<CSSStyleDeclaration>;
    shortcut?: Partial<CSSStyleDeclaration>;
}
/** Accepted theme inputs for global or per-menu theming. */
export type ContextMenuThemeInput = "automatic" | "light" | "dark" | "system" | ContextMenuTheme | ContextMenuThemeStore;
/** Normalized theme object consumed by `ThemeStore` and menu roots. */
export interface ContextMenuTheme {
    mode: "automatic" | "light" | "dark" | "system";
    className?: string;
    classes?: ContextMenuClassNames;
    styles?: ContextMenuStyles;
    tokens?: Partial<ContextMenuThemeTokens>;
}
export interface ContextMenuThemeTokens {
    bg: string;
    color: string;
    border: string;
    shadow: string;
    radius: string;
    padding: string;
    itemHeight: string;
    itemGap: string;
    activeBg: string;
    disabledColor: string;
    dangerColor: string;
    zIndex: string | number;
}
/** Minimal evented store used without tying core to any framework. */
export interface ContextMenuThemeStore {
    get(): ContextMenuTheme;
    set(theme: ContextMenuThemeInput): void;
    update(updater: (theme: ContextMenuTheme) => ContextMenuTheme): void;
    subscribe(listener: (theme: ContextMenuTheme) => void): () => void;
}
/** Menu data can be static or derived synchronously from open-time context. */
export type MenuItemsInput = MenuItem[] | ((context: MenuContext) => MenuItem[]);
/** Stable string constants for menu item variants. */
export declare const MenuItemType: {
    readonly Item: "item";
    readonly Separator: "separator";
    readonly Header: "header";
    readonly Label: "label";
    readonly Checkbox: "checkbox";
    readonly Radio: "radio";
    readonly Submenu: "submenu";
};
export type MenuItemType = (typeof MenuItemType)[keyof typeof MenuItemType];
/** All supported menu item variants in the data-driven model. */
export type MenuItem = MenuActionItem | MenuSeparatorItem | MenuHeaderItem | MenuLabelItem | MenuCheckboxItem | MenuRadioItem | MenuSubmenuItem;
/** Items that can represent an actionable user choice. */
export type MenuSelectableItem = MenuActionItem | MenuCheckboxItem | MenuRadioItem | MenuSubmenuItem;
/** Items allowed inside a submenu; recursive submenus are intentionally deferred. */
export type MenuChildItem = MenuActionItem | MenuSeparatorItem | MenuHeaderItem | MenuLabelItem | MenuCheckboxItem | MenuRadioItem;
/** Static or synchronous submenu children. */
export type MenuChildItemsInput = MenuChildItem[] | ((context: MenuContext) => MenuChildItem[]);
/** Standard selectable command row. */
export interface MenuActionItem {
    type?: typeof MenuItemType.Item;
    id: string;
    label: string;
    disabled?: boolean;
    hidden?: boolean;
    variant?: "default" | "danger";
    shortcut?: string;
    icon?: MenuIcon;
    description?: string;
    className?: string;
    classes?: Partial<ContextMenuClassNames>;
    style?: Partial<CSSStyleDeclaration>;
    value?: unknown;
    onSelect?: (event: MenuSelectEvent) => void;
}
/** Visual divider that is never focusable or selectable. */
export interface MenuSeparatorItem {
    type: typeof MenuItemType.Separator;
    hidden?: boolean;
}
/** Non-selectable title row for menus that need a contextual heading. */
export interface MenuHeaderItem {
    type: typeof MenuItemType.Header;
    label: string;
    align?: "left" | "right" | "items";
    hidden?: boolean;
    className?: string;
    style?: Partial<CSSStyleDeclaration>;
}
/** Non-selectable group label. */
export interface MenuLabelItem {
    type: typeof MenuItemType.Label;
    label: string;
    hidden?: boolean;
}
/** Reserved checkbox shape; full checkbox behavior is a later phase. */
export interface MenuCheckboxItem {
    type: typeof MenuItemType.Checkbox;
    id: string;
    label: string;
    checked: boolean;
    disabled?: boolean;
    shortcut?: string;
    onCheckedChange?: (checked: boolean, event: MenuSelectEvent) => void;
}
/** Reserved radio shape; full radio behavior is a later phase. */
export interface MenuRadioItem {
    type: typeof MenuItemType.Radio;
    id: string;
    label: string;
    name: string;
    value: string;
    checked: boolean;
    disabled?: boolean;
}
/** Selectable row that opens a child menu branch instead of firing selection. */
export interface MenuSubmenuItem {
    type: typeof MenuItemType.Submenu;
    id: string;
    label: string;
    disabled?: boolean;
    hidden?: boolean;
    shortcut?: string;
    icon?: MenuIcon;
    description?: string;
    className?: string;
    classes?: Partial<ContextMenuClassNames>;
    style?: Partial<CSSStyleDeclaration>;
    value?: unknown;
    items: MenuChildItemsInput;
}
/** Icon input normalized into a fixed icon column by the renderer. */
export type MenuIcon = string | HTMLElement | ((context: MenuRenderContext) => Node | string);
/** Context passed to icon render callbacks. */
export interface MenuRenderContext {
    item: MenuItem;
    context: MenuContext;
}
/** Event object sent to per-item and global selection callbacks. */
export interface MenuSelectEvent {
    id: string;
    item: MenuSelectableItem;
    nativeEvent: Event;
    context: MenuContext;
    close: () => void;
    preventClose: () => void;
}
/** Event object sent after a menu root has been rendered and positioned. */
export interface MenuOpenEvent {
    context: MenuContext;
    root: HTMLElement;
}
/** Event object sent after menu DOM and global listeners have been cleaned up. */
export interface MenuCloseEvent {
    reason: CloseReason;
    nativeEvent?: Event;
}
/** Event object sent before item resolution/rendering; return false to cancel. */
export interface MenuBeforeOpenEvent {
    context: MenuContext;
}
//# sourceMappingURL=types.d.ts.map