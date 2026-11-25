import { EditorState } from '@codemirror/state';
import { EditorView, keymap, lineNumbers } from '@codemirror/view';
import { defaultKeymap, history, historyKeymap } from '@codemirror/commands';
import { HighlightStyle, indentUnit, syntaxHighlighting } from '@codemirror/language';
import { json } from '@codemirror/lang-json';
import { tags } from '@lezer/highlight';

export function createJsonEditor(parent, format, editable, changed, save) {
  return new EditorView({ parent, doc: format.display, extensions: [
    json(), history(), lineNumbers(), EditorView.lineWrapping, indentUnit.of(format.indent),
    EditorState.readOnly.of(!editable), EditorView.editable.of(editable),
    syntaxHighlighting(HighlightStyle.define([
      { tag: tags.propertyName, class: 'json-key' }, { tag: tags.string, class: 'json-string' },
      { tag: [tags.number, tags.bool, tags.null], class: 'json-value' },
    ])),
    keymap.of([{ key: 'Mod-s', run: () => { save(); return true; } }, ...historyKeymap, ...defaultKeymap]),
    EditorView.contentAttributes.of({ 'aria-label': 'JSON editor', spellcheck: 'false' }),
    EditorView.updateListener.of(update => { if (update.docChanged) changed(update.state.doc); }),
  ] });
}
