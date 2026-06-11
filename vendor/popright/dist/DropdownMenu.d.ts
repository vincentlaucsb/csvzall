import { ContextMenu } from "./ContextMenu.js";
import type { MenuController } from "./MenuController.js";
import type { DropdownMenuOptions } from "./types.js";
/**
 * Dropdown menus share the same runtime as context menus. The only difference
 * is the activation/anchoring preset: click a target and position from its box.
 */
export declare class DropdownMenu extends ContextMenu {
    constructor(controller: MenuController, target: unknown, options: DropdownMenuOptions);
}
export declare function normalizeDropdownOptions(options: DropdownMenuOptions): import("./types.js").NormalizedContextMenuOptions;
//# sourceMappingURL=DropdownMenu.d.ts.map