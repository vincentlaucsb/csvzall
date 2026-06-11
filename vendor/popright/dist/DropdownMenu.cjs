"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.DropdownMenu = void 0;
exports.normalizeDropdownOptions = normalizeDropdownOptions;
const ContextMenu_js_1 = require("./ContextMenu.cjs");
const utils_js_1 = require("./utils.cjs");
/**
 * Dropdown menus share the same runtime as context menus. The only difference
 * is the activation/anchoring preset: click a target and position from its box.
 */
class DropdownMenu extends ContextMenu_js_1.ContextMenu {
    constructor(controller, target, options) {
        super(controller, target, {
            trigger: "click",
            placement: "target",
            side: "bottom",
            align: "start",
            menuType: "dropdown",
            ...options
        });
    }
}
exports.DropdownMenu = DropdownMenu;
function normalizeDropdownOptions(options) {
    return (0, utils_js_1.normalizeOptions)({
        trigger: "click",
        placement: "target",
        side: "bottom",
        align: "start",
        menuType: "dropdown",
        ...options
    });
}
