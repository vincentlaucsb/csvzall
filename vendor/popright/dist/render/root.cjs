"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.createMenuRoot = createMenuRoot;
exports.updateActiveDom = updateActiveDom;
const constants_js_1 = require("../constants.cjs");
const utils_js_1 = require("../utils.cjs");
/** Creates the DOM root that owns keyboard, pointer, and click delegation. */
function createMenuRoot({ ownerDocument, options, onKeyDown, onPointerMove, onClick }) {
    const root = ownerDocument.createElement("div");
    if (options.id) {
        root.id = options.id;
    }
    root.className = (0, utils_js_1.composeClass)(constants_js_1.DEFAULT_CLASSES.menu, options.className);
    root.dataset.poprightMenu = "";
    root.dataset.poprightMenuType = options.menuType;
    root.setAttribute("role", "menu");
    root.tabIndex = -1;
    root.dir = (options.dir ?? ownerDocument.dir) || "ltr";
    root.style.position = options.strategy;
    root.style.left = "0px";
    root.style.top = "0px";
    if (options.minWidth !== undefined) {
        root.style.minWidth = (0, utils_js_1.toCssValue)(options.minWidth);
    }
    if (options.maxHeight !== undefined) {
        root.style.maxHeight = (0, utils_js_1.toCssValue)(options.maxHeight);
        root.style.overflowY = "auto";
    }
    if (options.zIndex !== undefined) {
        root.style.zIndex = String(options.zIndex);
    }
    (0, utils_js_1.applyStyle)(root, options.styles?.menu);
    root.addEventListener("keydown", onKeyDown);
    root.addEventListener("pointermove", onPointerMove);
    root.addEventListener("click", onClick);
    return root;
}
function updateActiveDom(root, items, activeIndex, options) {
    const elements = root.querySelectorAll("[data-popright-item]");
    for (const element of Array.from(elements)) {
        const index = Number(element.dataset.index);
        const active = index === activeIndex;
        element.toggleAttribute("data-active", active);
        element.classList.toggle(constants_js_1.DEFAULT_CLASSES.itemActive, active);
        if (options.classes?.itemActive) {
            for (const className of (0, utils_js_1.splitClasses)(options.classes.itemActive)) {
                element.classList.toggle(className, active);
            }
        }
    }
}
