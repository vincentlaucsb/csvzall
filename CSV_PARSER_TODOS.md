# csv-parser Working Log

Notes collected while integrating csv-parser with csvzall. This is a scratchpad
for csv-parser follow-ups, not a committed roadmap.

## ETL type inference

- `csv::csv_data_types()` is useful as the general-purpose helper for common
  type inference workflows. It should stay approachable and opinionated.
- For robust ETL workflows, `csv_data_types()` is better treated as an example
  of the pattern rather than the final abstraction. Pipelines often need richer
  per-column state: null counts, text/numeric conflict counts, sample limits,
  domain-specific coercion, database-specific type collapsing, and diagnostics.
- `csv::chunk_parallel_apply()` is the more important building block for those
  advanced cases. It gives callers bounded chunking and per-column parallelism
  while leaving the inference policy under application control.
- Make `CSVReader`'s default variable-column behavior obvious in the docs:
  rows that are too short or too long are ignored by default. Generic ETL
  callers can rely on that instead of duplicating row-width checks after
  `read_chunk()`, and users who want different behavior should be pointed at
  the variable-columns configuration.
- Document that `CSVField::is_null()` is backed by scalar classification via
  `CSVField::type()`. This is the right default because null semantics stay in
  one place, but it matters in ultra-hot ETL loops: callers that only need the
  current csv-parser null definition (empty string or ASCII-space-only field)
  may choose a narrow predicate after `get_sv()` to avoid invoking full scalar
  inference per cell.
- Documentation should make this split explicit:
  - use `csv_data_types()` when the default SQL-friendly inference is enough
  - copy the `chunk_parallel_apply()` pattern when the workflow needs more
    control over state, sampling, coercion, or error reporting

## infer_scalar

- Consider evolving `data_type()` into a public `infer_scalar` API, separate
  from `internals::data_type()`, for non-CSV sources that still want csv-parser's
  fast scalar parsing.
- Consider extracting `infer_scalar` as its own small library. csv-parser can
  keep integration tests that prove CSV fields use scalar inference correctly,
  while straight `data_type()`/scalar classification cases move into the new
  library's focused test suite.
- Keep the design close to "C with templates": pointer spans, plain structs,
  integer kind ids, and small free functions should be the default shape.
  Avoid C++11-heavy policy ceremony unless it clearly removes complexity.
  Optional C++20 concepts can improve diagnostics when available, but should not
  be required for the core API.
- Trim surrounding ASCII whitespace by default before scalar inference so values
  such as `"    5    "` classify as numeric instead of string. Make trimming
  configurable for callers that need exact byte preservation. Pointer context
  passed to handlers should use the trimmed scalar span, while the original span
  can remain available if exact offsets are needed later.
- Keep the contract bounded even if `infer_scalar` becomes a general scalar
  inference library. It should classify well-formed scalar literals after
  optional boundary trimming; it should not repair arbitrary internal whitespace
  or normalize malformed domain strings. Callers can preprocess data and feed the
  cleaned token back into `infer_scalar`, or install custom recognizers for their
  own permissive grammars.
- A future JSON ingestion path in csvzall would benefit from this: it can pass
  scalar text as `std::string_view` without constructing `CSVField`, while still
  sharing csv-parser's numeric/null classification rules.
- Keep the common case cheap. A `long double*` output remains useful because
  numeric scalar inference is the common path.
- For extensibility, consider an output-pointer struct where the caller provides
  storage for the result types they care about, for example numeric, integer,
  timestamp, currency, or custom parser state. Unprovided pointers mean the
  parser classifies without storing that value.
- Prefer policy templates for compile-time concerns such as decimal symbol and
  optional feature sets. Runtime options can stay minimal.
- Re-evaluate whether a broad policy type is necessary. If the parser already
  knows the caller's output shape at compile time, lookahead/lookbehind handlers
  can be templated on that output struct directly. This may keep customization
  focused on "where do I put parsed values?" instead of requiring a policy object
  to own both parser configuration and result storage conventions.
- Return an integer kind id rather than trying to make `ScalarKind` extensible.
  The built-in enum can provide named constants for the reserved ids, while
  custom policies return ids from a documented custom range. Callers who define
  their own enum must preserve the built-in ids as a superset contract.
- A helper macro may make custom enums less error-prone by spelling the built-in
  ids once. For example, csv-parser could expose a macro that expands to the
  built-in scalar kind enumerators, letting callers write an enum with the
  required prefix and then append project-specific kinds such as timestamp or
  currency.
- A future advanced API might look like:

  ```cpp
  enum ScalarKind {
    CSV_SCALAR_BUILTIN_KINDS,
    SCALAR_CUSTOM_BEGIN = 1024
  };

  enum MyScalarKind {
    CSV_SCALAR_BUILTIN_KINDS,
    MY_TIMESTAMP = SCALAR_CUSTOM_BEGIN,
    MY_CURRENCY = SCALAR_CUSTOM_BEGIN + 1
  };

  struct scalar_outputs {
    long double* number = nullptr;
    std::int64_t* integer = nullptr;
    timestamp_value* timestamp = nullptr;
    void* custom = nullptr;
  };

  auto kind = csv::infer_scalar<policy>(first, last, outputs);
  ```

## Configurable scalar parser tables

- If `data_type.hpp` becomes a more general scalar parsing library, consider
  replacing scattered character `switch` policy checks with a small
  configuration-derived character flag table.
- The table can encode grammar features such as decimal symbol, exponent
  marker, sign characters, hex prefix/digits, timestamp separators, and boolean
  literals. Disabled features simply do not map characters to meaningful flags.
- This keeps optional features like hex or timestamp parsing on fast paths when
  enabled, without polluting the primitive numeric/null parser with repeated
  feature checks.
- Separate final inference from dispatch hints. `ScalarKind` is the final type
  returned to the caller. A second lightweight enum, tentatively
  `WhatThisMightBe`, should describe why the current byte is interesting:
  maybe sign or dash, maybe decimal point, maybe exponent marker, maybe hex
  marker, maybe timestamp separator, maybe custom. These hints are not verdicts;
  specialized handlers inspect pointer position/context and either continue the
  candidate parse or collapse the scalar to string.
- Keep the split clear: the table classifies bytes under a policy; the state
  machine still owns positional validity and parse semantics.
- Push local grammar flags into specialized handlers where possible. Current
  `data_type()`-style state such as `dot_allowed` is really part of deciding
  whether a dot is a float signifier or just text; that belongs in the decimal
  lookahead/parser rather than as global main-loop state. The main parser should
  stay focused on scanning and dispatching interesting bytes.
- C++11 can support custom flags with a simple policy contract instead of
  concepts or `if constexpr`: require each policy to provide `build_table()` and
  `handle_custom()`. `build_table()` maps bytes to built-in or custom flags;
  `handle_custom()` gives custom flags meaning.
- Custom handlers should receive pointer context such as `begin`, `current`,
  and `end`, plus caller-provided output storage. Lookahead/lookback then stays
  inside the callback as ordinary pointer inspection, which works for UTF-8 byte
  sequences without making the core parser Unicode-aware.
