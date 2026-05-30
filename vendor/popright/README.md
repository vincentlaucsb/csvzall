# popright

Tiny, typed, data-driven context menus for modern web apps.

```ts
import { createContextMenu } from "popright";
import "popright/styles.css";

const menu = createContextMenu(document.querySelector("#file-row")!, {
  items: [
    { id: "open", label: "Open" },
    { id: "rename", label: "Rename", shortcut: "F2" },
    { type: "separator" },
    { id: "delete", label: "Delete", variant: "danger" }
  ],
  onSelect({ id }) {
    console.log(id);
  }
});

menu.destroy();
```

Popright is early-stage software. The public API is small, typed, and focused on dynamic context menu data.
