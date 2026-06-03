/*!
 * Popright
 * Copyright (c) 2026 Vincent La
 * Released under the MIT License.
 */
/**
 * Computes final root-menu coordinates inside the viewport.
 *
 * Fixed positioning uses viewport coordinates directly. Absolute positioning
 * converts the same resolved viewport position back into document coordinates
 * by adding scroll offsets.
 */
export function computeMenuPosition(input) {
    const padding = Math.max(0, input.padding);
    const left = resolveAxisPosition(input.preferredLeft, input.width, input.viewportWidth, padding, input.fallbackLeft);
    const top = resolveAxisPosition(input.preferredTop, input.height, input.viewportHeight, padding, input.fallbackTop);
    if (input.strategy === "absolute") {
        return {
            left: left + (input.scrollX ?? 0),
            top: top + (input.scrollY ?? 0)
        };
    }
    return { left, top };
}
/**
 * Resolves one axis with simple flip-then-clamp behavior.
 *
 * The menu first tries to open at the requested pointer coordinate. If it would
 * overflow and there is room before the pointer, it flips to the opposite side;
 * otherwise it clamps within the padded viewport.
 */
export function resolveAxisPosition(preferredStart, size, viewportSize, padding, fallbackStart) {
    const min = padding;
    const max = Math.max(padding, viewportSize - padding - size);
    if (fallbackStart !== undefined && preferredStart + size > viewportSize - padding && fallbackStart >= padding) {
        return Math.min(fallbackStart, max);
    }
    if (fallbackStart !== undefined && preferredStart < padding && fallbackStart + size <= viewportSize - padding) {
        return Math.max(fallbackStart, min);
    }
    if (preferredStart + size > viewportSize - padding && preferredStart - size >= padding) {
        return preferredStart - size;
    }
    return Math.min(Math.max(preferredStart, min), max);
}
