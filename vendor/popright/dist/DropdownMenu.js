/*!
 * Popright
 * Copyright (c) 2026 Vincent La
 * Released under the MIT License.
 */
import { ContextMenu } from "./ContextMenu.js";
import { normalizeOptions } from "./utils.js";
/**
 * Dropdown menus share the same runtime as context menus. The only difference
 * is the activation/anchoring preset: click a target and position from its box.
 */
export class DropdownMenu extends ContextMenu {
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
export function normalizeDropdownOptions(options) {
    return normalizeOptions({
        trigger: "click",
        placement: "target",
        side: "bottom",
        align: "start",
        menuType: "dropdown",
        ...options
    });
}
