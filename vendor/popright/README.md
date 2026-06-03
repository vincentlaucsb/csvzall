# popright

Tiny, typed, data-driven context menus and dropdown menus for modern web apps.

```ts
import { createContextMenu, createDropdownMenu } from "popright";
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

```ts
const dropdown = createDropdownMenu(document.querySelector("#file-button")!, {
  items: [
    { id: "new", label: "New" },
    { id: "open", label: "Open" }
  ]
});
```

Popright is early-stage software. The public API is small, typed, and focused on dynamic menu data. Context menus and dropdown menus share one controller, so only one root menu is visible at a time.
