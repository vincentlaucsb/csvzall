import type { MenuItem, NormalizedContextMenuOptions } from "../types.js";
import type { MenuRootOptions } from "./types.js";
/** Creates the DOM root that owns keyboard, pointer, and click delegation. */
export declare function createMenuRoot({ ownerDocument, options, onKeyDown, onPointerMove, onClick }: MenuRootOptions): HTMLElement;
export declare function updateActiveDom(root: HTMLElement, items: MenuItem[], activeIndex: number, options: NormalizedContextMenuOptions): void;
//# sourceMappingURL=root.d.ts.map