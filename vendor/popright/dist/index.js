/*!
 * Popright
 * Copyright (c) 2026 Vincent La
 * Released under the MIT License.
 */
import { ContextMenu } from "./ContextMenu.js";
import { MenuController } from "./MenuController.js";
import { contextMenuTheme } from "./ThemeStore.js";
const defaultController = new MenuController();
export function createContextMenu(targetOrOptions, maybeOptions) {
    const hasTarget = maybeOptions !== undefined;
    const target = hasTarget ? targetOrOptions : null;
    const options = hasTarget ? maybeOptions : targetOrOptions;
    const menu = new ContextMenu(defaultController, target, options);
    return {
        open(input = {}) {
            menu.requestOpen(input);
        },
        close(reason = "manual") {
            menu.close(reason);
        },
        update(options) {
            menu.update(options);
        },
        destroy() {
            menu.destroy();
        },
        get isOpen() {
            return menu.isOpen;
        },
        get root() {
            return menu.root;
        }
    };
}
export function __getDefaultControllerForTests() {
    return defaultController;
}
export { contextMenuTheme };
