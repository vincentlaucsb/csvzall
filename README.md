# csvzall

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

```sh
csvzall filter "Salary > 50000 && Department == 1" employees.csv
csvzall filter "Score >= 90 || Bonus > 0" -          # read from stdin
```

### `derive <assignment> [input]`

Add a new column computed from an expression. The expression can reference any existing numeric column.

```sh
csvzall derive "Tax = Salary * 0.22" employees.csv
csvzall derive "BMI = Weight / (Height * Height)" -
```

### `summarize [input]`

Group rows and compute per-group aggregates. The row that produced the winning value is retained, so `--show` columns (e.g. a date or label) come from that row.

```
--group-by <col>     Column to group on (required)
--max <col>          Column to compute the maximum of (required)
--show <col>         Additional columns to carry from the winning row (repeatable)
```

```sh
csvzall summarize --group-by Region --max Revenue --show Quarter sales.csv
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

## Pipelines are shell scripts

There is no built-in pipeline format. Save complex chains as PowerShell, Bash, or batch scripts — they're already shareable, version-controllable, and composable with the rest of your toolbox.

## Building

Requires CMake 3.20+, a C++20 compiler, and a checkout of [csv-parser](https://github.com/vincentlaucsb/csv-parser).

```sh
# csv-parser can live at ../csv-parser, external/csv-parser, or a custom path
cmake -S . -B build
cmake --build build --config Release
```

To specify a custom csv-parser location:

```sh
cmake -S . -B build -DCSV_PARSER_ROOT=/path/to/csv-parser
```

The binary is at `build/Release/csvzall.exe` (Windows) or `build/csvzall` (Linux/macOS).

## Dependencies

| Library | Role | How it's sourced |
|---|---|---|
| [csv-parser](https://github.com/vincentlaucsb/csv-parser) v3 | CSV parsing and writing | Local checkout |
| [argparse](https://github.com/p-ranav/argparse) v3.1 | CLI argument parsing | FetchContent |
| [ExprTk](https://github.com/ArashPartow/exprtk) | Expression evaluation | FetchContent |

## Design notes

- Data flows on **stdout**, diagnostics on **stderr**. Every command is safe to pipe.
- `-` as the input path means stdin. All commands accept it.
- Expressions support both C-style `&&`/`||` and ExprTk's `and`/`or` keywords interchangeably.
- Column matching will be **case-insensitive** (planned — see roadmap).

## Roadmap

- [ ] Case-insensitive column matching across all commands
- [ ] Test suite (Google Test / Catch2 via CTest)
- [ ] Throughput stats on `--verbose` (rows/s, MiB/s)
- [ ] Richer `summarize` aggregations: `--min`, `--mean`, `--sum`, `--count`
