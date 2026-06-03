/*!
 * Popright
 * Copyright (c) 2026 Vincent La
 * Released under the MIT License.
 */
import { computeMenuPosition } from "./positioning.js";
import { createMenuRoot, renderMenuItems, updateActiveDom } from "./render.js";
import { applyTheme } from "./theme.js";
import { contextMenuTheme } from "./ThemeStore.js";
import { applyStyle, canUseDom, containsEventTarget, createMenuContext, firstSelectableIndex, hasSelectableOrLabelContent, isChildMenuItem, isSelectable, lastSelectableIndex, normalizeItems, normalizeOptions, normalizeTargets, resolveItems, selectableIndexes } from "./utils.js";
/**
 * Owns one context menu instance: target listeners, resolved items, DOM, focus,
 * global cleanup, and any open submenu branch.
 *
 * Root menus register with `MenuController`; submenus reuse the same behavior
 * but opt out of controller registration so the controller's active-menu
 * invariant remains about root menus only.
 */
export class ContextMenu {
    controller;
    /** Native targets that may trigger this menu. Empty means programmatic/manual only. */
    targets;
    options;
    /** The currently rendered menu root. Null means no DOM or global listeners should exist. */
    root = null;
    items = [];
    /** Index into `items`; disabled items, labels, and separators are skipped. */
    activeIndex = -1;
    /** Open-time context is retained so selection callbacks see the original trigger data. */
    currentContext = null;
    destroyed = false;
    /** Assigned by the controller and used only for same-depth native-event tie-breaking. */
    registeredAt = 0;
    /** Element that had focus before open; restored on close when it is still focusable. */
    previousFocus = null;
    /** Theme subscriptions exist only while the DOM root is open. */
    unsubscribeTheme = null;
    /** Target listeners live for the menu instance lifetime. */
    targetCleanups = [];
    /** Global listeners live only while the menu is open. */
    globalCleanups = [];
    /** Only one child submenu branch may be open from a menu at a time. */
    childMenu = null;
    parent;
    registeredWithController;
    /**
     * Tracks the submenu item source that produced the current child.
     * Re-entering the already-open trigger should preserve the submenu instead
     * of tearing down and recreating its DOM.
     */
    itemsSource = null;
    constructor(controller, target, options, internal = {}) {
        this.controller = controller;
        this.targets = normalizeTargets(target);
        this.options = normalizeOptions(options);
        this.parent = internal.parent ?? null;
        this.registeredWithController = internal.register ?? true;
        if (this.registeredWithController) {
            this.controller.register(this);
        }
        this.attachTargets();
    }
    requestOpen(input) {
        if (this.destroyed) {
            return;
        }
        this.controller.requestOpen(this, input);
    }
    openNow(input) {
        if (this.destroyed || !canUseDom()) {
            return;
        }
        const context = createMenuContext(input);
        const before = this.options.onBeforeOpen?.({ context });
        if (before === false) {
            if (this.registeredWithController) {
                this.controller.clearActive(this);
            }
            return;
        }
        const items = normalizeItems(resolveItems(this.options.items, context));
        if (!hasSelectableOrLabelContent(items)) {
            if (this.registeredWithController) {
                this.controller.clearActive(this);
            }
            return;
        }
        /**
         * Reopening the same instance must clear its old DOM and listeners first.
         * The controller may already have selected this menu as active, so this
         * close call intentionally does not unregister the instance.
         */
        this.close("reopen");
        if (this.registeredWithController) {
            this.controller.setActive(this);
        }
        this.previousFocus = document.activeElement;
        this.currentContext = context;
        this.items = items;
        this.activeIndex = firstSelectableIndex(items);
        const root = createMenuRoot({
            ownerDocument: document,
            options: this.options,
            onKeyDown: (event) => this.onKeyDown(event),
            onPointerMove: (event) => this.onPointerMove(event),
            onClick: (event) => this.onClick(event)
        });
        applyTheme(root, this.options.theme);
        renderMenuItems(root, {
            items: this.items,
            context,
            options: this.options,
            onItemEnter: (index) => {
                if (isSelectable(this.items[index])) {
                    this.activeIndex = index;
                    this.updateActiveDom();
                    if (this.items[index]?.type === "submenu") {
                        this.openSubmenu(index);
                    }
                    else {
                        this.closeChild("manual");
                    }
                }
            }
        });
        const portal = this.options.portal === false ? document.body : this.options.portal ?? document.body;
        const initialVisibility = root.style.visibility;
        /**
         * Layout must be measured after insertion, but users should never see the
         * menu flash at the unpositioned origin.
         */
        root.style.visibility = "hidden";
        portal.append(root);
        this.positionRoot(root, input);
        root.style.visibility = initialVisibility;
        this.root = root;
        this.unsubscribeTheme = contextMenuTheme.subscribe(() => {
            if (this.root) {
                applyTheme(this.root, this.options.theme);
            }
        });
        this.attachGlobalListeners();
        root.focus({ preventScroll: true });
        this.updateActiveDom();
        this.options.onOpen?.({ context, root });
    }
    close(reason = "manual", nativeEvent) {
        /** Closing a parent owns closing its descendant submenu branch. */
        this.closeChild(reason, nativeEvent);
        if (!this.root) {
            if (reason === "destroy" && this.registeredWithController) {
                this.controller.clearActive(this);
            }
            return;
        }
        const root = this.root;
        this.root = null;
        this.items = [];
        this.activeIndex = -1;
        this.currentContext = null;
        this.globalCleanups.splice(0).forEach((cleanup) => cleanup());
        this.unsubscribeTheme?.();
        this.unsubscribeTheme = null;
        root.remove();
        if (this.previousFocus instanceof HTMLElement) {
            this.previousFocus.focus({ preventScroll: true });
        }
        this.previousFocus = null;
        if (this.registeredWithController) {
            this.controller.clearActive(this);
        }
        this.options.onClose?.({ reason, nativeEvent });
    }
    update(options) {
        this.options = normalizeOptions({ ...this.options, ...options });
        if (this.root) {
            applyTheme(this.root, this.options.theme);
        }
    }
    destroy() {
        if (this.destroyed) {
            return;
        }
        this.destroyed = true;
        this.close("destroy");
        this.targetCleanups.splice(0).forEach((cleanup) => cleanup());
        if (this.registeredWithController) {
            this.controller.unregister(this);
        }
    }
    containsTarget(target) {
        return this.targets.some((candidate) => containsEventTarget(candidate, target));
    }
    getTargetDepth(eventTarget) {
        if (!eventTarget) {
            return -1;
        }
        if (this.targets.length === 0) {
            return 0;
        }
        let depth = 0;
        let current = eventTarget;
        while (current) {
            if (this.targets.includes(current)) {
                return depth;
            }
            current = current instanceof Node ? current.parentNode : null;
            depth += 1;
        }
        return -1;
    }
    hasTargets() {
        return this.targets.length > 0;
    }
    canOpenFromNativeEvent(event) {
        return this.options.trigger === event.type;
    }
    getClosestTarget(eventTarget) {
        if (!eventTarget || this.targets.length === 0) {
            return undefined;
        }
        let current = eventTarget;
        while (current) {
            if (this.targets.includes(current) && current instanceof Element) {
                return current;
            }
            current = current instanceof Node ? current.parentNode : null;
        }
        return undefined;
    }
    positionRoot(root, input) {
        const ownerDocument = root.ownerDocument;
        const ownerWindow = ownerDocument.defaultView;
        const documentElement = ownerDocument.documentElement;
        const viewportWidth = documentElement.clientWidth || ownerWindow?.innerWidth || 0;
        const viewportHeight = documentElement.clientHeight || ownerWindow?.innerHeight || 0;
        const padding = Math.max(0, this.options.collisionPadding);
        const availableHeight = Math.max(0, viewportHeight - padding * 2);
        let rect = root.getBoundingClientRect();
        if (rect.height > availableHeight && this.options.maxHeight === undefined) {
            /**
             * Until full positioning constraints are configurable, long menus prefer
             * becoming scrollable over overflowing the viewport.
             */
            root.style.maxHeight = `${availableHeight}px`;
            root.style.overflowY = "auto";
            rect = root.getBoundingClientRect();
        }
        const preferred = this.getPreferredPosition(input, rect);
        const position = computeMenuPosition({
            preferredLeft: preferred.left,
            preferredTop: preferred.top,
            fallbackLeft: preferred.fallbackLeft,
            fallbackTop: preferred.fallbackTop,
            width: rect.width,
            height: rect.height,
            viewportWidth,
            viewportHeight,
            padding,
            strategy: this.options.strategy,
            scrollX: ownerWindow?.scrollX,
            scrollY: ownerWindow?.scrollY
        });
        root.style.left = `${position.left}px`;
        root.style.top = `${position.top}px`;
    }
    getPreferredPosition(input, rect) {
        if (this.options.placement !== "target" || !input.target) {
            return { left: input.x ?? 0, top: input.y ?? 0 };
        }
        const targetRect = input.target.getBoundingClientRect();
        const sideOffset = this.options.sideOffset;
        const alignOffset = this.options.alignOffset;
        const side = this.options.side;
        const align = this.options.align;
        const dir = this.options.dir ?? input.target.ownerDocument?.dir ?? "ltr";
        const resolvedAlign = dir === "rtl" && (align === "start" || align === "end") ? (align === "start" ? "end" : "start") : align;
        const crossStart = side === "left" || side === "right" ? targetRect.top : targetRect.left;
        const crossSize = side === "left" || side === "right" ? targetRect.height : targetRect.width;
        const menuCrossSize = side === "left" || side === "right" ? rect.height : rect.width;
        let cross = crossStart;
        if (resolvedAlign === "center") {
            cross += (crossSize - menuCrossSize) / 2;
        }
        else if (resolvedAlign === "end") {
            cross += crossSize - menuCrossSize;
        }
        cross += alignOffset;
        if (side === "top") {
            return {
                left: cross,
                top: targetRect.top - rect.height - sideOffset,
                fallbackTop: targetRect.bottom + sideOffset
            };
        }
        if (side === "right") {
            return {
                left: targetRect.right + sideOffset,
                top: cross,
                fallbackLeft: targetRect.left - rect.width - sideOffset
            };
        }
        if (side === "left") {
            return {
                left: targetRect.left - rect.width - sideOffset,
                top: cross,
                fallbackLeft: targetRect.right + sideOffset
            };
        }
        return {
            left: cross,
            top: targetRect.bottom + sideOffset,
            fallbackTop: targetRect.top - rect.height - sideOffset
        };
    }
    get isOpen() {
        return this.root !== null;
    }
    attachTargets() {
        if (this.options.trigger === "manual") {
            return;
        }
        for (const target of this.targets) {
            const type = this.options.trigger;
            const listener = (event) => {
                if (type === "contextmenu") {
                    event.preventDefault();
                }
                const pointerEvent = event;
                this.requestOpen({
                    x: pointerEvent.clientX ?? 0,
                    y: pointerEvent.clientY ?? 0,
                    target: event.currentTarget instanceof Element ? event.currentTarget : undefined,
                    triggerEvent: event,
                    context: typeof this.options.context === "function" ? this.options.context(event) : this.options.context
                });
            };
            target.addEventListener(type, listener);
            this.targetCleanups.push(() => target.removeEventListener(type, listener));
        }
    }
    attachGlobalListeners() {
        if (!this.root) {
            return;
        }
        const root = this.root;
        const ownerDocument = root.ownerDocument;
        const pointerListener = (event) => {
            if (this.options.closeOnBlur &&
                this.root &&
                !this.root.contains(event.target) &&
                !this.childMenu?.containsRoot(event.target)) {
                this.close("outside-pointer", event);
            }
        };
        const blurListener = (event) => {
            if (!this.options.closeOnBlur || !this.root) {
                return;
            }
            const next = event.relatedTarget;
            if (next instanceof Node && (this.root.contains(next) || this.childMenu?.containsRoot(next))) {
                return;
            }
            this.close("blur", event);
        };
        const scrollListener = (event) => {
            if (this.options.closeOnScroll) {
                /**
                 * Scroll invalidates the pointer/target geometry that opened the menu.
                 * Closing is deliberately simpler and less surprising than chasing a
                 * moving target across scroll containers.
                 */
                this.close("scroll", event);
            }
        };
        const resizeListener = (event) => {
            if (this.options.closeOnResize) {
                this.close("resize", event);
            }
        };
        ownerDocument.addEventListener("pointerdown", pointerListener, true);
        root.addEventListener("focusout", blurListener);
        ownerDocument.addEventListener("scroll", scrollListener, true);
        root.ownerDocument.defaultView?.addEventListener("resize", resizeListener);
        this.globalCleanups.push(() => ownerDocument.removeEventListener("pointerdown", pointerListener, true));
        this.globalCleanups.push(() => root.removeEventListener("focusout", blurListener));
        this.globalCleanups.push(() => ownerDocument.removeEventListener("scroll", scrollListener, true));
        this.globalCleanups.push(() => root.ownerDocument.defaultView?.removeEventListener("resize", resizeListener));
    }
    onPointerMove(event) {
        const item = event.target?.closest?.("[data-popright-item]");
        if (!item || !this.root?.contains(item)) {
            return;
        }
        const index = Number(item.dataset.index);
        if (isSelectable(this.items[index])) {
            this.activeIndex = index;
            this.updateActiveDom();
            if (this.items[index]?.type === "submenu") {
                this.openSubmenu(index);
            }
            else {
                this.closeChild("manual");
            }
        }
    }
    onClick(event) {
        const item = event.target?.closest?.("[data-popright-item]");
        if (!item || !this.root?.contains(item)) {
            return;
        }
        this.selectIndex(Number(item.dataset.index), event);
    }
    onKeyDown(event) {
        if (event.key === "Escape" && this.options.closeOnEscape) {
            event.preventDefault();
            this.close("escape", event);
            return;
        }
        if (event.key === "ArrowDown") {
            event.preventDefault();
            this.moveActive(1);
            return;
        }
        if (event.key === "ArrowUp") {
            event.preventDefault();
            this.moveActive(-1);
            return;
        }
        if (event.key === "Home") {
            event.preventDefault();
            this.activeIndex = firstSelectableIndex(this.items);
            this.updateActiveDom();
            return;
        }
        if (event.key === "End") {
            event.preventDefault();
            this.activeIndex = lastSelectableIndex(this.items);
            this.updateActiveDom();
            return;
        }
        if (event.key === "Enter" || event.key === " ") {
            event.preventDefault();
            this.selectIndex(this.activeIndex, event);
            return;
        }
        const dir = this.options.dir ?? this.root?.dir ?? "ltr";
        const openSubmenuKey = dir === "rtl" ? "ArrowLeft" : "ArrowRight";
        const closeSubmenuKey = dir === "rtl" ? "ArrowRight" : "ArrowLeft";
        if (event.key === openSubmenuKey) {
            event.preventDefault();
            this.openSubmenu(this.activeIndex);
            return;
        }
        if (event.key === closeSubmenuKey && this.parent) {
            event.preventDefault();
            this.parent.closeChild("manual", event);
            this.parent.root?.focus({ preventScroll: true });
        }
    }
    moveActive(delta) {
        const selectable = selectableIndexes(this.items);
        if (selectable.length === 0) {
            this.activeIndex = -1;
            this.updateActiveDom();
            return;
        }
        const currentPosition = selectable.indexOf(this.activeIndex);
        const nextPosition = currentPosition === -1
            ? delta > 0
                ? 0
                : selectable.length - 1
            : (currentPosition + delta + selectable.length) % selectable.length;
        this.activeIndex = selectable[nextPosition];
        this.updateActiveDom();
    }
    selectIndex(index, nativeEvent) {
        const item = this.items[index];
        if (!isSelectable(item)) {
            return;
        }
        if (item.type === "submenu") {
            this.openSubmenu(index);
            return;
        }
        let closePrevented = false;
        const event = {
            id: item.id,
            item,
            nativeEvent,
            context: this.currentContext ?? createMenuContext({ triggerEvent: nativeEvent }),
            close: () => this.close("select", nativeEvent),
            preventClose: () => {
                closePrevented = true;
            }
        };
        if ("onSelect" in item) {
            item.onSelect?.(event);
        }
        this.options.onSelect?.(event);
        if (this.parent && this.options.closeOnSelect && !closePrevented) {
            this.parent.close("select", nativeEvent);
            return;
        }
        if (this.options.closeOnSelect && !closePrevented) {
            this.close("select", nativeEvent);
        }
    }
    updateActiveDom() {
        if (!this.root) {
            return;
        }
        updateActiveDom(this.root, this.items, this.activeIndex, this.options);
    }
    containsRoot(target) {
        return (!!this.root &&
            target instanceof Node &&
            (this.root.contains(target) || Boolean(this.childMenu?.containsRoot(target))));
    }
    openSubmenu(index) {
        if (!this.root || !canUseDom()) {
            return;
        }
        const item = this.items[index];
        if (!item || item.type !== "submenu" || item.disabled) {
            return;
        }
        const trigger = this.root.querySelector(`[data-popright-item][data-index="${index}"]`);
        if (!trigger) {
            return;
        }
        if (this.childMenu?.itemsSource === item.items) {
            return;
        }
        /** Sibling submenus are mutually exclusive within one menu level. */
        this.closeChild("manual");
        const childItems = normalizeItems(resolveItems(item.items, this.currentContext ?? createMenuContext({}))).filter(isChildMenuItem);
        if (!hasSelectableOrLabelContent(childItems)) {
            return;
        }
        const childMenu = new ContextMenu(this.controller, null, {
            ...this.options,
            trigger: "manual",
            placement: "target",
            side: (this.options.dir ?? this.root?.dir) === "rtl" ? "left" : "right",
            align: "start",
            sideOffset: -2,
            items: childItems,
            onBeforeOpen: undefined,
            onOpen: undefined,
            onClose: undefined,
            onSelect: (event) => this.options.onSelect?.(event)
        }, { parent: this, register: false });
        childMenu.itemsSource = item.items;
        this.childMenu = childMenu;
        childMenu.openNow({
            target: trigger,
            triggerEvent: this.currentContext?.triggerEvent,
            context: this.currentContext?.data
        });
        trigger.setAttribute("aria-expanded", String(childMenu.isOpen));
        if (childMenu.root) {
            childMenu.root.dataset.poprightSubmenu = "";
            childMenu.root.classList.add(this.options.classes?.submenu ?? "popright-submenu");
            childMenu.root.focus({ preventScroll: true });
        }
    }
    closeChild(reason = "manual", nativeEvent) {
        const child = this.childMenu;
        if (!child) {
            return;
        }
        this.childMenu = null;
        child.destroy();
        const trigger = this.root?.querySelector("[data-popright-submenu-trigger][aria-expanded='true']");
        trigger?.setAttribute("aria-expanded", "false");
    }
}
