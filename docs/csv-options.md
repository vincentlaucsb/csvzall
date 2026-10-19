# CSV options

Native and WASM viewers share the Options dialog beside Insert. It shows the
current delimiter, quoting character, BOM, and column names. Ordinary saves retain
the dialect and BOM, including the source record line ending. Conversion happens
only after selecting an output dialect and saving. The comma/double-quote preset
changes the output controls; it does not write the file immediately.

Headers are enabled initially. Turning them off exposes the first row as data
and assigns A through Z, AA, AB, etc. display labels. Those labels never become a
saved header row. Turning headers back on consumes the current first row as names;
empty or duplicate names are rejected without changing the table. Pending cell
edits survive header toggles. Headerless insert/delete operations relabel columns,
and synthetic labels cannot be renamed. Header mode is a session interpretation,
since plain CSV has no header-mode metadata.

## Input interpretation

csv-parser supplies configurable delimiter, quoting, and header support. The
viewer infers a delimiter and recognizes common single-quoted fields only when a
bounded sample has structural evidence using that detected delimiter. Ambiguous
apostrophe text keeps the default double-quote interpretation. No inference can
distinguish every literal apostrophe from a custom quote dialect.

Use **Correct input dialect** to select explicit characters and **Read using these
characters** to reread existing bytes, including ambiguous single quotes and other
ASCII quote characters. This action changes the view without writing the file.
Save or reset pending edits first; both backends reject rereading a modified
document. Input overrides bypass inference. The checkbox also selects whether the
first source row contains headers. The resulting dialect becomes the default for
ordinary saves. A tab delimiter is entered as `\t`; an empty quote disables quoting.

Reset discards edits and output conversion choices, returning to the last read or
saved dialect. Saving without quotes rejects values that require quoting before
replacing the source file. Initially empty files retain the existing actionable
open error; deleting every headerless row can save an empty or BOM-only file and
still permits inserting a new row in the session.

## Validation

`tests/test_view_dialect.cpp` covers shared native file I/O and authenticated HTTP
options. `src/viewer_wasm/web/test/embedded-viewer.test.mjs` runs the compiled engine
through the embedded worker. Both cover dialect and BOM preservation, literal
apostrophes, explicit conversion and input overrides, headerless round trips,
pending edits, empty saves, and protection against failed operations. The broader
viewer tests cover host saves, themes, menus, and mobile editing stability.
