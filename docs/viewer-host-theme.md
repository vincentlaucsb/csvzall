# Embedded viewer theme protocol

Both the native and WASM viewer import `src/viewer/modules/host-theme.mjs`.
When embedded, it sends its parent:

```js
{ source: 'csvzall-viewer', type: 'theme-ready', version: 1 }
```

The parent can reply and later send updates with:

```js
{ source: 'obsidian-csvzall', type: 'theme', version: 1,
  mode: 'light', variables: { '--background-primary': '#fff', /* ... */ } }
```

`HOST_THEME_VARIABLES` defines the allowed CSS properties. Send resolved
computed values, not references to other properties or font resources in the
host document. The receiver checks `event.source === window.parent`, rejects
unsupported protocol versions, modes, and malformed values, and ignores
unrecognized properties. Each valid message replaces the previous snapshot;
omitted properties are removed. It never accepts a stylesheet or resource URL.

The shared `modules/viewer-theme.css` maps host colors, fonts, UI spacing and input height to standalone defaults. CSV cells and JSON code use the same `--font-text-size`; toolbar and status text follow the UI sizes. Explicit light/dark grid
classes and `color-scheme` override system mode only after a valid message.
The receiver does not recreate the grid or touch file state, editor focus,
scroll position, or keyboard layout. A standalone viewer installs no receiver
and retains its system-aware styles. Embedded viewers without a cooperating
host also retain their existing styles.

The native asset generator includes the receiver module, and Vite bundles the
same source for WASM. Older Obsidian mobile bundles can vendor this source as
a separate module script until they are refreshed from a new WASM release.

## Dialog dismissal

The native and WASM viewers use `modules/dialog.mjs` for both the shared shell
and dialogs created by individual features. Escape and a primary click that
starts and finishes on the backdrop close an ordinary dialog as Cancel.
Backdrop clicks respect cancel handlers. Loading and save progress stay open
until their operation finishes.
