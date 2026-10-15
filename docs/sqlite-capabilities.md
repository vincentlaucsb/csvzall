# SQLite capabilities {#sqlite_capabilities}

This reference describes SQL in the native desktop CSV viewer and csvzall's
SQLite-backed CLI workflows. The standard distribution uses SQLiteCpp's bundled
SQLite plus csvzall's regular-expression functions. The mobile/WASM viewer does
not provide the native SQLite query toolbar.

## Built-in SQLite functions

Use SQLite-flavored SQL. See the official
[built-in scalar SQL functions reference](https://www.sqlite.org/lang_corefunc.html)
for string, numeric, conditional, and inspection functions. SQLite documents
[aggregate functions](https://www.sqlite.org/lang_aggfunc.html),
[date and time functions](https://www.sqlite.org/lang_datefunc.html),
[window functions](https://www.sqlite.org/windowfunctions.html), and
[JSON functions](https://www.sqlite.org/json1.html) separately.

Those pages describe the latest SQLite. Functions added after the bundled
version, or requiring optional compile flags, may be unavailable in csvzall.
Check your running helper with the inspection queries below.

| Capability | Standard native distribution |
| --- | --- |
| Core scalar functions | Includes `substr`, `replace`, `lower`, `upper`, `coalesce`, `abs`, and `round` |
| Aggregates and windows | Includes `count`, `sum`, `avg`, `min`, `max`, and `row_number` |
| Dates and times | Includes `date`, `datetime`, and `strftime`; CSV date strings must use a format SQLite understands |
| JSON and JSONB | Includes `json_extract`, `json_valid`, `json_each`, `json_tree`, and `jsonb` |
| Regular expressions | csvzall adds `REGEXP`, `regexp(pattern, value)`, and `regexp_like(value, pattern)` |

These examples are complete queries that need no particular CSV columns:

```sql
SELECT upper('Alice') AS name,
       round(12.345, 2) AS rounded,
       coalesce(NULL, 'fallback') AS fallback;
```

```sql
SELECT strftime('%Y-%m', '2026-10-14') AS month,
       date('2026-10-14', '+7 days') AS next_week;
```

```sql
SELECT json_extract('{"project":"csvzall","count":3}', '$.project') AS project,
       json_extract(jsonb('{"count":3}'), '$.count') AS count;
```

```sql
SELECT key, value
FROM json_each('["coffee","tea"]');
```

JSONB is SQLite's binary JSON representation. Use extraction functions to return
ordinary SQL values for display; the viewer presents results as cell text rather
than a binary-data inspector. JSON support is built into the bundled SQLite;
it is not a separate csvzall extension.

## csvzall regular-expression functions

All three forms below search for a match and return `1` or `0`:

```sql
SELECT 'Alice' REGEXP '^Ali' AS operator_match,
       regexp('^Ali', 'Alice') AS function_match,
       regexp_like('Alice', '^Ali') AS like_match;
```

The direct `regexp` function takes the **pattern first**. `regexp_like` takes
the **value first**. The `REGEXP` operator supplies the argument order for you.

Patterns use C++ `std::regex` with ECMAScript syntax. Matching is case-sensitive
unless the pattern starts with csvzall's supported `(?i)` prefix:

```sql
SELECT regexp_like('ALICE', '(?i)^alice$') AS case_insensitive,
       regexp_like('workout today', '\b(gym|workout|lift)\b') AS word_match;
```

- Matching searches within the value. Use `^` and `$` to require a whole-value
  match.
- A SQL `NULL` value or pattern returns `0`, rather than SQL `NULL`.
- Invalid patterns fail the query with an `invalid regex pattern` error.
- This is not PCRE. PCRE-only constructs and general inline flag syntax are
  unsupported; `(?i)` is specially recognized only at the start of the pattern.
- csvzall does not add `regexp_replace`, `regexp_extract`, or regex aggregates.
- Backslashes in these SQL examples are literal. Shell or programming-language
  quoting may require another escaping layer when passing the SQL to the CLI.

The registration API is csvzall::sqlite::RegisterSqliteRegexFunctions().

## Querying CSV data in the viewer

The native viewer exposes the current CSV as table `data`, including unsaved
cell, row, and column edits. The following examples assume a CSV with `name`
and `value` columns; substitute your own column names:

```sql
SELECT name, value
FROM data
WHERE regexp_like(name, '(?i)^alice')
ORDER BY value DESC
LIMIT 100;
```

```sql
SELECT count(*) AS rows, sum(value) AS total, avg(value) AS average
FROM data;
```

Double-quote identifiers containing spaces or reserved words. Double an embedded
double quote: a column named `say"hello` becomes `"say""hello"`. SQL string
literals use single quotes, with embedded single quotes doubled. SQLite also
accepts backtick-quoted identifiers; **Query column** generates those and doubles
any embedded backtick.

Columns containing numeric values only receive `NUMERIC` affinity. Columns
containing text, booleans, timestamps, or very large integer identifiers receive
`TEXT` affinity to preserve their original CSV representation. Mixed columns
therefore sort and compare as text unless you explicitly cast them; CSV cells
are not automatically SQL booleans or date objects.

### Viewer controls and limits

- **SQLite query** opens a draft. **Query column** opens an escaped column draft.
  Neither action executes a query. In an empty editor, Right Arrow inserts the
  suggested query without executing it.
- Use **Run query** or Ctrl/Cmd+Enter to execute. Results are read-only.
  **Copy Markdown** copies the result table. **Exit query** restores the source
  table and preserves unsaved edits.
- The viewer accepts one read-only statement returning a result set. `SELECT`
  queries and read-only common table expressions work; mutations such as
  `UPDATE`, `DELETE`, and `CREATE TABLE`, and multiple statements do not.
- Every run loads the complete CSV, including its edit overlay, into a fresh
  in-memory SQLite database. Paging in the source grid does not limit that load.
  `LIMIT 100` limits the result, not the memory needed to load the CSV.
- The suggested query limits results to 100 rows. There is no automatic result
  limit on a query you write; use `LIMIT` for large results.
- Viewer results are returned as strings for grid display and Markdown export;
  the grid does not expose SQLite storage types as typed cell values. SQL `NULL`
  results appear as empty cells, so they look the same as empty strings.

These restrictions describe the viewer. CLI command behavior depends on the
command; for example, `csvzall sql query --db <path>` queries an existing database.

## Optional extensions and unavailable functions

In the standard native build:

- FTS3/FTS4/FTS5 full-text search and RTree are not enabled.
- Spatial extensions, ICU, and third-party extension suites such as sqlean are
  not bundled.
- Optional mathematical functions such as `sqrt`, `sin`, and `log` are not
  enabled. Core functions such as `abs` and `round` remain available.
- Statistical extensions such as `median` and `percentile` are not provided.
  Standard aggregates such as `avg` and `sum` remain available.
- Loading external extensions is disabled on csvzall-created connections;
  `load_extension(...)` reports `not authorized`. Seeing that function in the
  function list does not mean loading is permitted.

Custom builds can differ. Consult the running helper rather than assuming that
every function on SQLite's latest website is enabled.

## Inspect the running helper

These read-only queries work in the viewer:

```sql
SELECT sqlite_version() AS sqlite_version;
```

```sql
SELECT name, narg, builtin
FROM pragma_function_list
ORDER BY name, narg;
```

`builtin = 0` includes application-registered functions, but does not by itself
identify a bundled extension package or guarantee that a function is usable.

```sql
SELECT name
FROM pragma_module_list
ORDER BY name;
```

```sql
SELECT compile_options
FROM pragma_compile_options
ORDER BY compile_options;
```

The function list reports registered functions, the module list reports virtual
table modules, and compile options report build flags. Test a function directly
when availability or permissions matter.
