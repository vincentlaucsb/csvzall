# Test TODOs

## CSV Viewer

- Add an end-to-end browser test for AG Grid infinite-row editing:
  - launch `csvzall view <file.csv> --edit --viewer-assets src/viewer`;
  - verify the grid loads through the infinite row model;
  - edit a visible cell and verify dirty state/save controls update;
  - insert a row, delete a row, rename a column, and save;
  - reload or reopen the viewer and verify the CSV file contents persisted.

Current coverage is strong for the C++ view model, HTTP edit endpoints, and small
JavaScript helper contracts, but the interactive AG Grid layer still needs a true
browser-driven regression test.
