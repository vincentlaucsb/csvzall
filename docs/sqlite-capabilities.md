# SQLite capabilities {#sqlite_capabilities}

This reference describes SQL in the native desktop CSV viewer, chart `sql`
options, and csvzall's SQLite-backed CLI workflows. The standard distribution
uses SQLiteCpp's bundled SQLite with math functions enabled, selected sqlean
modules (including PCRE2 regular expressions), and csvzall's date functions.
The mobile/WASM viewer does not provide the native SQLite query toolbar.

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
| Dates and times | Includes `date`, `datetime`, and `strftime` for ISO text; csvzall adds `parse_date` and `date_precision` for hand-entered dates |
| JSON and JSONB | Includes `json_extract`, `json_valid`, `json_each`, `json_tree`, and `jsonb` |
| Math | `sqrt`, `pow`, `ln`, `log`, `exp`, `floor`, `ceil`, trigonometry |
| Regular expressions | PCRE2: `REGEXP`, `regexp(pattern, value)`, `regexp_like(value, pattern)`, `regexp_substr`, `regexp_capture`, `regexp_replace` |
| sqlean | `fuzzy`, `stats` (`median`, `percentile`), `text`, `crypto`, `uuid` modules |

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

## Regular expressions (PCRE2)

csvzall compiles in [sqlean](https://github.com/nalgeon/sqlean)'s `regexp`
module, which uses [PCRE2](https://www.pcre.org/) in UTF-8 mode with Unicode
properties. These forms search for a match and return `1` or `0`:

```sql
SELECT 'Alice' REGEXP '^Ali' AS operator_match,
       regexp('^Ali', 'Alice') AS function_match,
       regexp_like('Alice', '^Ali') AS like_match;
```

The direct `regexp` function takes the **pattern first**. `regexp_like` takes
the **value first**. The `REGEXP` operator supplies the argument order for you.

Extraction and replacement functions return text:

```sql
SELECT regexp_substr('ride 42 km', '\d+') AS first_number,
       regexp_capture('ride 42 km', '(\d+) km', 1) AS group_1,
       regexp_replace('2026-06-09', '(\d+)-(\d+)-(\d+)', '$3/$2/$1') AS reordered;
```

`regexp_capture(value, pattern [, n])` returns capture group `n` (default `0`,
the whole match). `regexp_replace(value, pattern, replacement)` replaces every
match; `$1`, `${name}`, and other PCRE2 extended substitutions are available.

Inline flags such as `(?i)` (case-insensitive), `(?m)` (multi-line anchors),
and `(?x)` (free spacing) apply from where they appear to the end of the
enclosing group, so put `(?i)` first to make the whole pattern
case-insensitive; `regexp_like('Ab', 'a(?i)b')` is `0`:

```sql
SELECT regexp_like('ALICE', '(?i)^alice$') AS case_insensitive,
       regexp_like('workout today', '\b(gym|workout|lift)\b') AS word_match,
       regexp_like('Café', '^Caf.$') AS one_character,
       regexp_like('naïve', '^\w+$') AS unicode_word;
```

- Matching searches within the value. Use `^` and `$` to require a whole-value
  match.
- `.` matches one Unicode character, `\w`, `\d`, and `\b` follow Unicode
  properties, and `(?i)` folds case for non-ASCII letters such as `É`.
- A SQL `NULL` value returns `0` from `regexp_like`/`REGEXP` and `NULL` from the
  extraction functions. A `NULL` pattern fails the query with
  `missing regexp pattern`. (csvzall 0.7.0 and earlier returned `0`.)
- Invalid patterns fail the query with PCRE2's error message and offset, for
  example `missing terminating ] for character class (offset 1)`.
- Values that are not valid UTF-8, such as Latin-1 CSV text, still match on
  their valid parts; the invalid bytes themselves never match.
- Long values work: PCRE2 backtracks on the heap rather than the call stack.
- Backslashes in these SQL examples are literal. Shell or programming-language
  quoting may require another escaping layer when passing the SQL to the CLI.

The registration API is csvzall::sqlite::RegisterCsvzallSqliteFunctions(),
which also registers the date and sqlean functions below.

## Dates written by hand

SQLite's `date()` only reads ISO-style text. csvzall adds two functions built on
the same parser as `csvzall heatmap`:

| Function | Result |
| --- | --- |
| `parse_date(value [, order])` | ISO `YYYY-MM-DD`, or `NULL` when the value is not a date |
| `date_precision(value [, order])` | `'day'`, `'month'`, `'year'`, or `NULL` |

Accepted values:

- Full dates: `YYYY-MM-DD`, `M/D/YYYY`, `M-D-YYYY`, `D/M/YYYY`, and `D-M-YYYY`.
  A year-first value may carry a time suffix (`2026-05-26T10:00:00`).
- Month precision: `6/2026`, `2026-06`, `4/??/2026`, `2026-04-??`.
- Year precision: `2026`, `??/??/2026`, `2026-??`.

A run of `?` marks an unknown part. Partial dates return the first day of the
period, so `parse_date('4/??/2026')` is `2026-04-01`. `order` is `'mdy'`
(default) or `'dmy'`, case-insensitive; any other value, including `NULL`,
fails the query. It only decides values such as `6/9/2026` where both
leading parts are 12 or less; `26/5/2026` is always read day-first and
`5/26/2026` month-first. Unlike heatmap, a scalar SQL function cannot infer the
order from the rest of the column, so pass `'dmy'` for day-first data.

How to treat a partial date stays a choice in SQL:

```sql
SELECT Item,
       CASE date_precision(Date)
         WHEN 'day'   THEN parse_date(Date)
         WHEN 'month' THEN date(parse_date(Date), '+14 days')
       END AS service_date
FROM data;
```

## Math functions

The bundled SQLite is built with `SQLITE_ENABLE_MATH_FUNCTIONS`: `sqrt`, `pow`,
`exp`, `ln`, `log`, `log2`, `log10`, `ceil`, `floor`, `trunc`, `mod`, `pi`,
`degrees`, `radians`, and the trigonometric and hyperbolic functions.

```sql
SELECT sqrt(16) AS root, pow(2, 10) AS kib, round(pi(), 4) AS pi;
```

## sqlean modules

These [sqlean](https://github.com/nalgeon/sqlean) 0.28.5 modules are compiled in
and need no flag. `sqlean_version()` reports the bundled release. Module
documentation lives in sqlean's `docs/` directory.

| Module | Examples |
| --- | --- |
| `fuzzy` | `fuzzy_leven`, `fuzzy_damlev`, `fuzzy_jarowin`, `fuzzy_hamming`, `fuzzy_osadist`, `fuzzy_editdist`, `fuzzy_soundex`, `fuzzy_rsoundex`, `fuzzy_phonetic`, `fuzzy_caver`, `fuzzy_translit` |
| `stats` | `median`, `percentile(x, p)`, `percentile_90`, `stddev`, `stddev_pop`, `variance`, `var_pop` |
| `text` | `text_substring`, `text_split`, `text_join`, `text_upper`, `text_lower`, `text_title`, `text_casefold`, `text_lpad`, `text_replace`, `text_reverse`, `split_part`, `left`, `right`, `reverse`, `repeat` |
| `crypto` | `md5`, `sha1`, `sha256`, `sha512`, `blake3`, `xxh64`, `encode(blob, 'hex')`, `decode` |
| `uuid` | `uuid4`, `uuid7`, `uuid_str`, `uuid_blob` |
| `regexp` | See [Regular expressions](#regular-expressions-pcre2) |

```sql
SELECT fuzzy_leven('kitten', 'sitting') AS edits,
       fuzzy_soundex('Robert') AS soundex,
       encode(sha256('csvzall'), 'hex') AS digest;
```

```sql
SELECT median(value) AS median, percentile(value, 90) AS p90
FROM data;
```

SQLite built-ins keep their standard meaning. sqlean's `text` module also
defines `ltrim`, `rtrim`, `concat`, `concat_ws`, and `octet_length`, which would
silently replace the SQLite built-ins of the same name; csvzall leaves those
aliases out. Use the `text_ltrim`, `text_rtrim`, `text_concat`, `text_join`, and
`text_size` spellings for sqlean's behavior.

Deliberately not compiled in: `fileio` and `vsv` (read or write arbitrary files,
which a synced `charts.json` could trigger), `unicode` (replaces `upper`,
`lower`, and `LIKE`), `define` (lets SQL define functions), `math` (SQLite's
built-in math is enabled instead), `time` (ISO-only parsing; see
[Dates written by hand](#dates-written-by-hand)), and `ipaddr` (not built on
Windows). Load any of them yourself with `--load-extension` when you need them.

## Several CSVs in one query

`csvzall sql query` loads each `--csv <name>=<path>` as table `<name>` in one
database, so queries can `JOIN` them without managing a `.db` file. The
database is in memory unless the CSVs together exceed the SQLite size threshold
(256 MB), in which case a self-deleting temporary file is used:

```sh
csvzall sql query --csv rides=strava.csv --csv log="Bike Maintenance Log.csv" \
  --sql "SELECT log.Item, SUM(rides.distance) AS km
         FROM log JOIN rides ON rides.date >= parse_date(log.Date)
         GROUP BY log.Item"
```

One `--csv` may omit the name and uses `--table` (default `data`); passing
`--table` when every `--csv` is named is an error. At most one table can read
stdin (`name=-`). `--delimiter` and `--zip-entry` apply to every table. Table names are case-insensitive and must be
unique. A value is treated as `name=path` only when the text before `=` is an
identifier and the whole value is not an existing file, so paths containing `=`
keep working.

To build a reusable database instead, `csvzall sql load` writes one table per
run. The first load creates the file; pass `--add-table` to add further tables
(it fails if a table, view, or index already uses the name) or
`--replace-table` to replace an existing table. Each load creates its table
inside one transaction, so a failed or interrupted load leaves no partial table
behind. Replacement loads into a staging table first, so a failed load leaves
the old table intact; a successful replacement drops the old table's indexes
and triggers, while views that name the table keep working. Loads into an
existing database keep SQLite journaling on and never change its journal mode.
If a load is killed mid-way, SQLite rolls it back the next time the database is
opened for writing (for example by the next `sql load`); until then,
`sql query --db` may report `attempt to write a readonly database`.

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
- Spatial extensions, ICU, and the sqlean modules listed above as not compiled
  in are not bundled.
- SQL cannot load extensions: `load_extension(...)` reports `not authorized` on
  every csvzall connection. Seeing that function in the function list does not
  mean loading is permitted.

### Loading a native extension from the CLI

`csvzall sql query --load-extension <path>` (repeatable) loads a native SQLite
extension into that query's database, with `--csv` or `--db`:

```sh
csvzall sql query --csv log.csv --load-extension ./unicode --sql "SELECT unicode_upper(Item) FROM data"
```

csvzall enables loading only through SQLite's C API
(`SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION`), loads the named files, and disables
it again before your SQL runs, so the query itself still cannot load anything.
Chart configs (`charts.json`) and the viewer have no way to request an
extension, so a synced or shared vault cannot make csvzall run native code. An
extension runs with your user's full permissions; load only files you trust.

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
