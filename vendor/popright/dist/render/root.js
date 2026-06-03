/*!
 * Popright
 * Copyright (c) 2026 Vincent La
 * Released under the MIT License.
 */
import { DEFAULT_CLASSES } from "../constants.js";
import { applyStyle, splitClasses, toCssValue, composeClass } from "../utils.js";
/** Creates the DOM root that owns keyboard, pointer, and click delegation. */
export function createMenuRoot({ ownerDocument, options, onKeyDown, onPointerMove, onClick }) {
    const root = ownerDocument.createElement("div");
    if (options.id) {
        root.id = options.id;
    }
    root.className = composeClass(DEFAULT_CLASSES.menu, options.className);
    root.dataset.poprightMenu = "";
    root.dataset.poprightMenuType = options.menuType;
    root.setAttribute("role", "menu");
    root.tabIndex = -1;
    root.dir = (options.dir ?? ownerDocument.dir) || "ltr";
    root.style.position = options.strategy;
    root.style.left = "0px";
    root.style.top = "0px";
    if (options.minWidth !== undefined) {
        root.style.minWidth = toCssValue(options.minWidth);
    }
    if (options.maxHeight !== undefined) {
        root.style.maxHeight = toCssValue(options.maxHeight);
        root.style.overflowY = "auto";
    }
    if (options.zIndex !== undefined) {
        root.style.zIndex = String(options.zIndex);
    }
    applyStyle(root, options.styles?.menu);
    root.addEventListener("keydown", onKeyDown);
    root.addEventListener("pointermove", onPointerMove);
    root.addEventListener("click", onClick);
    return root;
}
export function updateActiveDom(root, items, activeIndex, options) {
    const elements = root.querySelectorAll("[data-popright-item]");
    for (const element of Array.from(elements)) {
        const index = Number(element.dataset.index);
        const active = index === activeIndex;
        element.toggleAttribute("data-active", active);
        element.classList.toggle(DEFAULT_CLASSES.itemActive, active);
        if (options.classes?.itemActive) {
            for (const className of splitClasses(options.classes.itemActive)) {
                element.classList.toggle(className, active);
            }
        }
    }
}
