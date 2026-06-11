"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.contextMenuTheme = void 0;
exports.createContextMenu = createContextMenu;
exports.createDropdownMenu = createDropdownMenu;
exports.__getDefaultControllerForTests = __getDefaultControllerForTests;
const ContextMenu_js_1 = require("./ContextMenu.cjs");
const DropdownMenu_js_1 = require("./DropdownMenu.cjs");
const MenuController_js_1 = require("./MenuController.cjs");
const ThemeStore_js_1 = require("./ThemeStore.cjs");
Object.defineProperty(exports, "contextMenuTheme", { enumerable: true, get: function () { return ThemeStore_js_1.contextMenuTheme; } });
const defaultController = new MenuController_js_1.MenuController();
function createContextMenu(targetOrOptions, maybeOptions) {
    const hasTarget = maybeOptions !== undefined;
    const target = hasTarget ? targetOrOptions : null;
    const options = hasTarget ? maybeOptions : targetOrOptions;
    const menu = new ContextMenu_js_1.ContextMenu(defaultController, target, options);
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
function createDropdownMenu(targetOrOptions, maybeOptions) {
    const hasTarget = maybeOptions !== undefined;
    const target = hasTarget ? targetOrOptions : null;
    const options = hasTarget ? maybeOptions : targetOrOptions;
    const menu = new DropdownMenu_js_1.DropdownMenu(defaultController, target, options);
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
function __getDefaultControllerForTests() {
    return defaultController;
}
