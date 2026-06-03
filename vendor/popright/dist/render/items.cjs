"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.renderMenuItems = renderMenuItems;
exports.renderSeparator = renderSeparator;
exports.renderHeader = renderHeader;
exports.renderLabel = renderLabel;
exports.renderActionRow = renderActionRow;
exports.renderShortcut = renderShortcut;
exports.renderSubmenuArrow = renderSubmenuArrow;
exports.renderIcon = renderIcon;
exports.getItemClassName = getItemClassName;
exports.getRenderableItemState = getRenderableItemState;
exports.isRenderableMenuItem = isRenderableMenuItem;
exports.getMenuItemType = getMenuItemType;
const constants_js_1 = require("../constants.cjs");
const types_js_1 = require("../types.cjs");
const utils_js_1 = require("../utils.cjs");
const MENU_ITEM_RENDERERS = {
    [types_js_1.MenuItemType.Item]: renderActionRow,
    [types_js_1.MenuItemType.Separator]: renderSeparator,
    [types_js_1.MenuItemType.Header]: renderHeader,
    [types_js_1.MenuItemType.Label]: renderLabel,
    [types_js_1.MenuItemType.Submenu]: renderActionRow
};
/**
 * Renders normalized menu data into stable DOM/data-attribute contracts.
 *
 * The renderer is intentionally dumb: it does not own active state, callbacks,
 * positioning, or item resolution. Keeping it a pure DOM projection makes the
 * controller/menu lifecycle easier to reason about and simpler to test.
 */
function renderMenuItems(root, { items, context, options, onItemEnter }) {
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
function renderSeparator({ root, item, options }) {
    if (item.type !== types_js_1.MenuItemType.Separator) {
        return;
    }
    const separator = root.ownerDocument.createElement("div");
    separator.className = (0, utils_js_1.composeClass)(constants_js_1.DEFAULT_CLASSES.separator, options.classes?.separator);
    separator.dataset.poprightSeparator = "";
    separator.setAttribute("role", "separator");
    root.append(separator);
}
function renderHeader({ root, item, options }) {
    if (item.type !== types_js_1.MenuItemType.Header) {
        return;
    }
    const header = root.ownerDocument.createElement("div");
    header.className = (0, utils_js_1.composeClass)(constants_js_1.DEFAULT_CLASSES.header, options.classes?.header, item.className);
    header.dataset.poprightHeader = "";
    header.dataset.poprightHeaderAlign = item.align ?? "left";
    header.textContent = item.label;
    (0, utils_js_1.applyStyle)(header, options.styles?.header);
    (0, utils_js_1.applyStyle)(header, item.style);
    root.append(header);
}
function renderLabel({ root, item, options }) {
    if (item.type !== types_js_1.MenuItemType.Label) {
        return;
    }
    const label = root.ownerDocument.createElement("div");
    label.className = (0, utils_js_1.composeClass)(constants_js_1.DEFAULT_CLASSES.label, options.classes?.label);
    label.dataset.poprightLabel = "";
    label.textContent = item.label;
    root.append(label);
}
function renderActionRow({ root, item, index, context, options, onItemEnter, hasIcons }) {
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
    if (item.type === types_js_1.MenuItemType.Submenu) {
        element.dataset.poprightSubmenuTrigger = "";
        element.setAttribute("aria-haspopup", "menu");
        element.setAttribute("aria-expanded", "false");
    }
    if (item.disabled) {
        element.dataset.disabled = "";
        element.setAttribute("aria-disabled", "true");
    }
    (0, utils_js_1.applyStyle)(element, options.styles?.item);
    (0, utils_js_1.applyStyle)(element, item.style);
    if (hasIcons) {
        const icon = renderIcon(root.ownerDocument, item.icon, { item, context });
        element.append(icon);
    }
    const label = root.ownerDocument.createElement("span");
    label.className = (0, utils_js_1.composeClass)(options.classes?.label, item.classes?.label);
    label.dataset.poprightLabelText = "";
    label.textContent = item.label;
    element.append(label);
    if (item.type === types_js_1.MenuItemType.Submenu) {
        element.append(renderSubmenuArrow(root.ownerDocument, root.dir, options));
    }
    else if (item.shortcut) {
        element.append(renderShortcut(root.ownerDocument, item, options));
    }
    element.addEventListener("pointerenter", () => onItemEnter(index));
    root.append(element);
}
function renderShortcut(ownerDocument, item, options) {
    const shortcut = ownerDocument.createElement("span");
    shortcut.className = (0, utils_js_1.composeClass)(constants_js_1.DEFAULT_CLASSES.shortcut, options.classes?.shortcut, item.classes?.shortcut);
    shortcut.dataset.poprightShortcut = "";
    shortcut.textContent = item.shortcut ?? "";
    return shortcut;
}
function renderSubmenuArrow(ownerDocument, dir, options) {
    const trigger = ownerDocument.createElement("span");
    trigger.className = (0, utils_js_1.composeClass)(constants_js_1.DEFAULT_CLASSES.submenuTrigger, options.classes?.submenuTrigger);
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
function renderIcon(ownerDocument, iconInput, context) {
    const icon = ownerDocument.createElement("span");
    icon.className = constants_js_1.DEFAULT_CLASSES.icon;
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
function getItemClassName(item, options) {
    const isDanger = "variant" in item && item.variant === "danger";
    return (0, utils_js_1.composeClass)(constants_js_1.DEFAULT_CLASSES.item, options.classes?.item, "classes" in item ? item.classes?.item : undefined, item.disabled && constants_js_1.DEFAULT_CLASSES.itemDisabled, item.disabled && options.classes?.itemDisabled, item.disabled && "classes" in item ? item.classes?.itemDisabled : undefined, isDanger && constants_js_1.DEFAULT_CLASSES.itemDanger, isDanger && options.classes?.itemDanger, isDanger && "classes" in item ? item.classes?.itemDanger : undefined, "className" in item ? item.className : undefined);
}
/**
 * Exposes the same render classification used by `renderMenuItems` for unit
 * tests, without forcing tests to parse DOM strings.
 */
function getRenderableItemState(item, index, activeIndex, options) {
    if (item.type === types_js_1.MenuItemType.Separator) {
        return {
            kind: types_js_1.MenuItemType.Separator,
            className: (0, utils_js_1.composeClass)(constants_js_1.DEFAULT_CLASSES.separator, options.classes?.separator),
            role: "separator",
            disabled: false,
            active: false
        };
    }
    if (item.type === types_js_1.MenuItemType.Label) {
        return {
            kind: types_js_1.MenuItemType.Label,
            className: (0, utils_js_1.composeClass)(constants_js_1.DEFAULT_CLASSES.label, options.classes?.label),
            disabled: false,
            active: false
        };
    }
    if (item.type === types_js_1.MenuItemType.Header) {
        return {
            kind: types_js_1.MenuItemType.Header,
            className: (0, utils_js_1.composeClass)(constants_js_1.DEFAULT_CLASSES.header, options.classes?.header, item.className),
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
        kind: types_js_1.MenuItemType.Item,
        className: getItemClassName(item, options),
        role: "menuitem",
        disabled: Boolean(item.disabled),
        active: !item.disabled && index === activeIndex
    };
}
/** Only action and submenu items become focusable/selectable menu rows today. */
function isRenderableMenuItem(item) {
    const itemType = getMenuItemType(item);
    return itemType === types_js_1.MenuItemType.Item || itemType === types_js_1.MenuItemType.Submenu;
}
function getMenuItemType(item) {
    return item.type ?? types_js_1.MenuItemType.Item;
}
