/*!
 * Popright
 * Copyright (c) 2026 Vincent La
 * Released under the MIT License.
 */
import { DEFAULT_CLASSES } from "../constants.js";
import { MenuItemType } from "../types.js";
import { applyStyle, composeClass } from "../utils.js";
const MENU_ITEM_RENDERERS = {
    [MenuItemType.Item]: renderActionRow,
    [MenuItemType.Separator]: renderSeparator,
    [MenuItemType.Header]: renderHeader,
    [MenuItemType.Label]: renderLabel,
    [MenuItemType.Submenu]: renderActionRow
};
/**
 * Renders normalized menu data into stable DOM/data-attribute contracts.
 *
 * The renderer is intentionally dumb: it does not own active state, callbacks,
 * positioning, or item resolution. Keeping it a pure DOM projection makes the
 * controller/menu lifecycle easier to reason about and simpler to test.
 */
export function renderMenuItems(root, { items, context, options, onItemEnter }) {
    /**
     * Icon columns are all-or-nothing per menu. When any item has an icon, every
     * actionable row receives an icon cell so all text starts on the same x-axis.
     */
    const hasIcons = items.some((item) => isRenderableMenuItem(item) && Boolean(item.icon));
    root.toggleAttribute("data-popright-has-icons", hasIcons);
    items.forEach((item, index) => {
        const itemType = getMenuItemType(item);
        const renderer = MENU_ITEM_RENDERERS[itemType];
        if (!renderer) {
            return;
        }
        renderer({ root, item, index, items, context, options, onItemEnter, hasIcons });
    });
}
export function renderSeparator({ root, item, options }) {
    if (item.type !== MenuItemType.Separator) {
        return;
    }
    const separator = root.ownerDocument.createElement("div");
    separator.className = composeClass(DEFAULT_CLASSES.separator, options.classes?.separator);
    separator.dataset.poprightSeparator = "";
    separator.setAttribute("role", "separator");
    root.append(separator);
}
export function renderHeader({ root, item, options }) {
    if (item.type !== MenuItemType.Header) {
        return;
    }
    const header = root.ownerDocument.createElement("div");
    header.className = composeClass(DEFAULT_CLASSES.header, options.classes?.header, item.className);
    header.dataset.poprightHeader = "";
    header.dataset.poprightHeaderAlign = item.align ?? "left";
    header.textContent = item.label;
    applyStyle(header, options.styles?.header);
    applyStyle(header, item.style);
    root.append(header);
}
export function renderLabel({ root, item, options }) {
    if (item.type !== MenuItemType.Label) {
        return;
    }
    const label = root.ownerDocument.createElement("div");
    label.className = composeClass(DEFAULT_CLASSES.label, options.classes?.label);
    label.dataset.poprightLabel = "";
    label.textContent = item.label;
    root.append(label);
}
export function renderActionRow({ root, item, index, context, options, onItemEnter, hasIcons }) {
    if (!isRenderableMenuItem(item)) {
        return;
    }
    const element = root.ownerDocument.createElement("div");
    element.className = getItemClassName(item, options);
    element.dataset.poprightItem = "";
    element.dataset.index = String(index);
    element.setAttribute("role", "menuitem");
    element.tabIndex = -1;
    if ("variant" in item && item.variant === "danger") {
        element.dataset.variant = "danger";
    }
    if (item.type === MenuItemType.Submenu) {
        element.dataset.poprightSubmenuTrigger = "";
        element.setAttribute("aria-haspopup", "menu");
        element.setAttribute("aria-expanded", "false");
    }
    if (item.disabled) {
        element.dataset.disabled = "";
        element.setAttribute("aria-disabled", "true");
    }
    applyStyle(element, options.styles?.item);
    applyStyle(element, item.style);
    if (hasIcons) {
        const icon = renderIcon(root.ownerDocument, item.icon, { item, context });
        element.append(icon);
    }
    const label = root.ownerDocument.createElement("span");
    label.className = composeClass(options.classes?.label, item.classes?.label);
    label.dataset.poprightLabelText = "";
    label.textContent = item.label;
    element.append(label);
    if (item.type === MenuItemType.Submenu) {
        element.append(renderSubmenuArrow(root.ownerDocument, root.dir, options));
    }
    else if (item.shortcut) {
        element.append(renderShortcut(root.ownerDocument, item, options));
    }
    element.addEventListener("pointerenter", () => onItemEnter(index));
    root.append(element);
}
export function renderShortcut(ownerDocument, item, options) {
    const shortcut = ownerDocument.createElement("span");
    shortcut.className = composeClass(DEFAULT_CLASSES.shortcut, options.classes?.shortcut, item.classes?.shortcut);
    shortcut.dataset.poprightShortcut = "";
    shortcut.textContent = item.shortcut ?? "";
    return shortcut;
}
export function renderSubmenuArrow(ownerDocument, dir, options) {
    const trigger = ownerDocument.createElement("span");
    trigger.className = composeClass(DEFAULT_CLASSES.submenuTrigger, options.classes?.submenuTrigger);
    trigger.dataset.poprightSubmenuArrow = "";
    trigger.setAttribute("aria-hidden", "true");
    trigger.textContent = dir === "rtl" ? "‹" : "›";
    return trigger;
}
/**
 * Normalizes all icon inputs into a wrapper span owned by this menu.
 *
 * Node inputs are cloned so passing the same HTMLElement to multiple menu items
 * cannot move it between rows.
 */
export function renderIcon(ownerDocument, iconInput, context) {
    const icon = ownerDocument.createElement("span");
    icon.className = DEFAULT_CLASSES.icon;
    icon.dataset.poprightIcon = "";
    icon.setAttribute("aria-hidden", "true");
    if (!iconInput) {
        return icon;
    }
    const rendered = typeof iconInput === "function" ? iconInput(context) : iconInput;
    if (rendered instanceof HTMLElement) {
        icon.append(rendered.cloneNode(true));
        return icon;
    }
    if (rendered instanceof Node) {
        icon.append(rendered.cloneNode(true));
        return icon;
    }
    icon.textContent = rendered;
    return icon;
}
/** Computes the structural classes for an actionable row without touching DOM. */
export function getItemClassName(item, options) {
    const isDanger = "variant" in item && item.variant === "danger";
    return composeClass(DEFAULT_CLASSES.item, options.classes?.item, "classes" in item ? item.classes?.item : undefined, item.disabled && DEFAULT_CLASSES.itemDisabled, item.disabled && options.classes?.itemDisabled, item.disabled && "classes" in item ? item.classes?.itemDisabled : undefined, isDanger && DEFAULT_CLASSES.itemDanger, isDanger && options.classes?.itemDanger, isDanger && "classes" in item ? item.classes?.itemDanger : undefined, "className" in item ? item.className : undefined);
}
/**
 * Exposes the same render classification used by `renderMenuItems` for unit
 * tests, without forcing tests to parse DOM strings.
 */
export function getRenderableItemState(item, index, activeIndex, options) {
    if (item.type === MenuItemType.Separator) {
        return {
            kind: MenuItemType.Separator,
            className: composeClass(DEFAULT_CLASSES.separator, options.classes?.separator),
            role: "separator",
            disabled: false,
            active: false
        };
    }
    if (item.type === MenuItemType.Label) {
        return {
            kind: MenuItemType.Label,
            className: composeClass(DEFAULT_CLASSES.label, options.classes?.label),
            disabled: false,
            active: false
        };
    }
    if (item.type === MenuItemType.Header) {
        return {
            kind: MenuItemType.Header,
            className: composeClass(DEFAULT_CLASSES.header, options.classes?.header, item.className),
            align: item.align ?? "left",
            disabled: false,
            active: false
        };
    }
    if (!isRenderableMenuItem(item)) {
        return {
            kind: "skipped",
            className: "",
            disabled: Boolean("disabled" in item && item.disabled),
            active: false
        };
    }
    return {
        kind: MenuItemType.Item,
        className: getItemClassName(item, options),
        role: "menuitem",
        disabled: Boolean(item.disabled),
        active: !item.disabled && index === activeIndex
    };
}
/** Only action and submenu items become focusable/selectable menu rows today. */
export function isRenderableMenuItem(item) {
    const itemType = getMenuItemType(item);
    return itemType === MenuItemType.Item || itemType === MenuItemType.Submenu;
}
export function getMenuItemType(item) {
    return item.type ?? MenuItemType.Item;
}
