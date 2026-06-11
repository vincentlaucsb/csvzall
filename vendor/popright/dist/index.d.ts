import { MenuController } from "./MenuController.js";
import { contextMenuTheme } from "./ThemeStore.js";
import type { ContextMenuInstance, ContextMenuOptions, DropdownMenuOptions } from "./types.js";
export declare function createContextMenu(target: Element | Document | Window | Iterable<Element>, options: ContextMenuOptions): ContextMenuInstance;
export declare function createContextMenu(options: ContextMenuOptions): ContextMenuInstance;
export declare function createDropdownMenu(target: Element | Document | Window | Iterable<Element>, options: DropdownMenuOptions): ContextMenuInstance;
export declare function createDropdownMenu(options: DropdownMenuOptions): ContextMenuInstance;
export declare function __getDefaultControllerForTests(): MenuController;
export { contextMenuTheme };
export type * from "./types.js";
//# sourceMappingURL=index.d.ts.map