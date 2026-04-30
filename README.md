# csvzall

<img src="assets/csvzall-logo.png" alt="csvzall logo: a reciprocating saw cutting through a spreadsheet" width="760">

A fast, single-binary CSV transformation CLI for Unix-style pipelines. Pipe CSVs through `filter`, `derive`, `summarize`, and `head` — each command reads stdin, writes stdout, logs to stderr.

## Quick example

Compute estimated 1-rep maxes from a [FitNotes](https://www.fitnotesapp.com/) export using the [Epley formula](https://en.wikipedia.org/wiki/One-repetition_maximum#Epley_formula), then show the best set per exercise:

```sh
csvzall filter "Weight > 0 && Reps > 0" FitNotes_Export.csv \
  | csvzall derive "Epley1RM = Weight * (1 + Reps / 30)" - \
  | csvzall summarize --group-by Exercise --max Epley1RM --show Date - \
  | csvzall head -
```

```
+-----------------------+------------+------------------+
| Exercise              | Date       | max_Epley1RM     |
+-----------------------+------------+------------------+
| Overhead Press        | 2025-03-07 | 110.833333333333 |
| Flat Barbell Bench    | 2024-04-20 | 175.666666666667 |
| Deadlift              | 2026-01-18 | 302.5            |
| ...                   | ...        | ...              |
+-----------------------+------------+------------------+
```

## Commands

### `filter <expression> [input]`

Keep rows where the expression evaluates to non-zero. Supports arithmetic, comparisons (`>`, `<`, `>=`, `<=`, `==`, `!=`), and boolean logic (`&&`/`and`, `||`/`or`).

Column matching is case-insensitive by default. Use `--exact` to require exact case-sensitive column names.

```sh
csvzall filter "Salary > 50000 && Department == 1" employees.csv
csvzall filter --exact "Salary > 50000 && Department == 1" employees.csv
csvzall filter "Score >= 90 || Bonus > 0" -          # read from stdin
```

### `derive <assignment> [input]`

Add a new column computed from an expression. The expression can reference any existing numeric column.

Column matching is case-insensitive by default. Use `--exact` to require exact case-sensitive column names.

```sh
csvzall derive "Tax = Salary * 0.22" employees.csv
csvzall derive --exact "Tax = Salary * 0.22" employees.csv
csvzall derive "BMI = Weight / (Height * Height)" -
```

### `summarize [input]`

Group rows and compute per-group aggregates. The row that produced the winning value is retained, so `--show` columns (e.g. a date or label) come from that row.

```
--group-by <col>     Column to group on (required)
--max <col>          Column to compute the maximum of (required)
--show <col>         Additional columns to carry from the winning row (repeatable)
--exact              Use exact case-sensitive column matching
```

```sh
csvzall summarize --group-by Region --max Revenue --show Quarter sales.csv
csvzall summarize --exact --group-by Region --max Revenue --show Quarter sales.csv
```

Output column is named `max_<col>`.

### `head [input]`

Print the header and first N rows as a formatted ASCII table. Useful for inspecting pipeline output.

```
-n, --rows <N>    Number of data rows to show (default: 50)
```

```sh
csvzall head -n 10 data.csv
some-command | csvzall head -
```

### `infer [input]`

Infer the PostgreSQL schema that `postgres` export would use, without connecting to a database or loading rows. This is useful for inspecting type inference and timing inference separately from `COPY`.

```sh
csvzall infer vehicles.csv --table used_cars --verbose
```

### `postgres [input]`

Export CSV rows into PostgreSQL with full-file schema inference followed by `COPY`.

```sh
csvzall postgres vehicles.csv --dbname postgres --user postgres --table used_cars
```

Files ending in `.gz` are read as gzip-compressed CSV automatically:

```sh
csvzall head vehicles.csv.gz
csvzall postgres vehicles.csv.gz --dbname postgres --user postgres --table used_cars
```

Credential storage is optional. Save a PostgreSQL password to the OS keychain:

```sh
csvzall postgres --save --host localhost --port 5432 --dbname postgres --user postgres
```

Normal `postgres` runs try the keychain first, then prompt with masked input if
no credential is found. `--password` remains available for automation but prints
a warning because command-line passwords are visible in shell history and process
lists. `--password-env VARNAME` reads the password from an environment variable.

Remove stored PostgreSQL credentials:

```sh
csvzall forget postgres
csvzall forget postgres --host localhost --port 5432 --dbname postgres --user postgres
```

`--copy-batch-rows <N>` tunes the producer batch size for the COPY pipeline. The default is `10000`, which keeps memory bounded well on large files; larger values may increase peak memory without improving throughput.

`--parallel-copy [N]` runs multiple PostgreSQL COPY workers, each with its own
connection. This can improve throughput when a single COPY stream is the
bottleneck, but physical insertion order is not preserved. The default is `1`
when the flag is omitted. Passing `--parallel-copy` without a value uses
`min(hardware_concurrency / 2, 8)`, and explicit values are capped at hardware
concurrency.

## Pipelines are shell scripts

There is no built-in pipeline format. Save complex chains as PowerShell, Bash, or batch scripts — they're already shareable, version-controllable, and composable with the rest of your toolbox.

## Building

Requires CMake 3.25+ and a C++23 compiler. A local [csv-parser](https://github.com/vincentlaucsb/csv-parser) checkout is preferred for development, but CMake can fetch it when no local checkout is configured.

```sh
# csv-parser can live at ../csv-parser, external/csv-parser, or a custom path.
# When both local paths exist, ../csv-parser is preferred by default.
cmake -S . -B build
cmake --build build --config Release
```

To specify a custom csv-parser location:

```sh
cmake -S . -B build -DCSV_PARSER_ROOT=/path/to/csv-parser
```

The binary is at `build/Release/csvzall.exe` (Windows) or `build/csvzall` (Linux/macOS).

## Dependencies

| Library | Author / maintainer | Role | How it's sourced |
|---|---|---|---|
| [csv-parser](https://github.com/vincentlaucsb/csv-parser) | [Vincent La](https://github.com/vincentlaucsb) | CSV parsing, writing, and scalar type classification | Local checkout preferred; FetchContent fallback |
| [argparse](https://github.com/p-ranav/argparse) v3.1 | [Pranav](https://github.com/p-ranav) | CLI argument parsing | FetchContent |
| [indicators](https://github.com/p-ranav/indicators) v2.3 | [Pranav](https://github.com/p-ranav) | Terminal progress bars for long-running imports | FetchContent |
| [zlib](https://github.com/madler/zlib) v1.3.1 | [Mark Adler](https://github.com/madler) and contributors | gzip decompression for `.csv.gz` inputs | System package if available; FetchContent fallback |
| [keychain](https://github.com/hrantzsch/keychain) v1.3.1 | [hrantzsch](https://github.com/hrantzsch) | Optional OS credential storage for PostgreSQL passwords | System package if available; FetchContent fallback; Linux requires libsecret |
| [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp) v3.3.2 | [Sébastien Rombauts](https://github.com/SRombauts) | SQLite C++ wrapper using bundled SQLite | FetchContent, with a local CMake patch |
| [libpqxx](https://github.com/jtv/libpqxx) v7.10.1 | [Jeroen T. Vermeulen](https://pqxx.org/libpqxx/) | PostgreSQL C++ client API used by the `postgres` command | System package if available; FetchContent fallback |
| [PostgreSQL libpq](https://www.postgresql.org/docs/current/libpq.html) | [PostgreSQL Global Development Group](https://www.postgresql.org/community/) | PostgreSQL client C library required by libpqxx | System PostgreSQL installation |
| [Catch2](https://github.com/catchorg/Catch2) v3.4.0 | [Catch2 contributors](https://github.com/catchorg/Catch2) | Test framework | FetchContent, tests only |

## Design notes

- Data flows on **stdout**, diagnostics on **stderr**. Every command is safe to pipe.
- `-` as the input path means stdin. All commands accept it.
- `filter` and `derive` use SQL syntax — standard `WHERE` clauses and SQL expressions.
- Column matching is case-insensitive by default (SQLite identifier resolution).

## Roadmap

- [x] Case-insensitive column matching by default (`filter`, `derive`, `summarize`)
- [ ] Test suite (Google Test / Catch2 via CTest)
- [ ] Throughput stats on `--verbose` (rows/s, MiB/s)
- [ ] Richer `summarize` aggregations: `--min`, `--mean`, `--sum`, `--count`
