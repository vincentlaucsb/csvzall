"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.MenuController = void 0;
/**
 * Coordinates all root context menus that share a document-level interaction space.
 *
 * The controller owns the "only one active root menu" invariant. Individual
 * `ContextMenu` instances own rendering and local state, but every public open
 * request must pass through this object so nested or overlapping targets do not
 * flicker competing menus open and closed during the same native event.
 */
class MenuController {
    /** Live root menus registered with this controller; submenus are intentionally excluded. */
    #menus = new Set();
    /** The one root menu currently allowed to own global listeners and focus restoration. */
    #activeMenu = null;
    /** Monotonic registration order used as the tie-breaker for equally specific targets. */
    #registeredCounter = 0;
    /**
     * Per-native-event open candidates.
     *
     * Multiple registered targets can observe the same bubbling `contextmenu`
     * event. We collect candidates until the current event turn finishes, then
     * choose the most specific target once all listeners have had a chance to run.
     */
    #eventCandidates = new WeakMap();
    register(menu) {
        this.#menus.add(menu);
        menu.registeredAt = ++this.#registeredCounter;
    }
    unregister(menu) {
        this.#menus.delete(menu);
        if (this.#activeMenu === menu) {
            this.#activeMenu = null;
        }
    }
    requestOpen(menu, input) {
        const event = input.triggerEvent;
        if (!event) {
            this.#open(menu, input);
            return;
        }
        const targetDepth = menu.getTargetDepth(event.target);
        if (targetDepth === -1 && menu.hasTargets()) {
            return;
        }
        let group = this.#eventCandidates.get(event);
        if (!group) {
            group = [];
            this.#eventCandidates.set(event, group);
            /**
             * The microtask boundary is the arbitration window for one native event.
             * It lets parent and child listeners both request an open before we pick
             * a winner, which avoids requiring users to stop propagation manually.
             */
            queueMicrotask(() => {
                const candidates = this.#eventCandidates.get(event);
                this.#eventCandidates.delete(event);
                if (!candidates || candidates.length === 0) {
                    return;
                }
                const winner = chooseCandidate(this.#expandNativeEventCandidates(candidates, event));
                this.#open(winner.menu, winner.input);
            });
        }
        group.push({
            menu,
            input,
            targetDepth,
            registeredAt: menu.registeredAt
        });
    }
    #expandNativeEventCandidates(candidates, event) {
        const expanded = [...candidates];
        const seen = new Set(candidates.map((candidate) => candidate.menu));
        const source = candidates[0]?.input;
        if (!source) {
            return expanded;
        }
        for (const menu of this.#menus) {
            if (seen.has(menu) || !menu.canOpenFromNativeEvent(event)) {
                continue;
            }
            const targetDepth = menu.getTargetDepth(event.target);
            if (targetDepth === -1 && menu.hasTargets()) {
                continue;
            }
            expanded.push({
                menu,
                input: {
                    ...source,
                    /**
                     * Broad menus such as a body-level fallback can request first. When a
                     * more specific registered target contains the event target, the
                     * eventual open input must point at that specific target so dynamic
                     * item resolvers receive the context users expect.
                     */
                    target: menu.getClosestTarget(event.target) ?? source.target,
                    triggerEvent: event
                },
                targetDepth,
                registeredAt: menu.registeredAt
            });
        }
        return expanded;
    }
    closeActive(reason = "manual", nativeEvent) {
        this.#activeMenu?.close(reason, nativeEvent);
    }
    setActive(menu) {
        if (this.#activeMenu && this.#activeMenu !== menu) {
            this.#activeMenu.close("reopen");
        }
        this.#activeMenu = menu;
    }
    clearActive(menu) {
        if (this.#activeMenu === menu) {
            this.#activeMenu = null;
        }
    }
    get activeMenu() {
        return this.#activeMenu;
    }
    #open(menu, input) {
        this.setActive(menu);
        menu.openNow(input);
    }
}
exports.MenuController = MenuController;
function chooseCandidate(candidates) {
    return [...candidates].sort((a, b) => {
        if (a.targetDepth !== b.targetDepth) {
            return a.targetDepth - b.targetDepth;
        }
        return b.registeredAt - a.registeredAt;
    })[0];
}
