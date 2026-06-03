export interface MenuPositionInput {
    preferredLeft: number;
    preferredTop: number;
    fallbackLeft?: number;
    fallbackTop?: number;
    width: number;
    height: number;
    viewportWidth: number;
    viewportHeight: number;
    padding: number;
    strategy: "fixed" | "absolute";
    scrollX?: number;
    scrollY?: number;
}
export interface MenuPosition {
    left: number;
    top: number;
}
/**
 * Computes final root-menu coordinates inside the viewport.
 *
 * Fixed positioning uses viewport coordinates directly. Absolute positioning
 * converts the same resolved viewport position back into document coordinates
 * by adding scroll offsets.
 */
export declare function computeMenuPosition(input: MenuPositionInput): MenuPosition;
/**
 * Resolves one axis with simple flip-then-clamp behavior.
 *
 * The menu first tries to open at the requested pointer coordinate. If it would
 * overflow and there is room before the pointer, it flips to the opposite side;
 * otherwise it clamps within the padded viewport.
 */
export declare function resolveAxisPosition(preferredStart: number, size: number, viewportSize: number, padding: number, fallbackStart?: number): number;
//# sourceMappingURL=positioning.d.ts.map