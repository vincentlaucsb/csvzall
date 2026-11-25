import { createEditorToolbar } from './editor-toolbar.mjs';
import { decorateButton } from './icons.mjs';

export function createViewerToolbar(doc) {
  const bar = createEditorToolbar(doc, decorateButton);
  bar.filename.id = 'file-name';
  bar.edits.id = 'edit-toolbar';
  bar.reset.id = 'reset';
  bar.save.id = 'save';
  return bar;
}
