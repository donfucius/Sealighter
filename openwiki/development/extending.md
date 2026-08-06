# Extending Sealighter

This page is for anyone (human or agent) changing Sealighter: where features plug in, what conventions to follow,
and what to watch out for. There is no test suite — changes are validated by building and running traces.

## Extension points

### 1. Add a new filter type

1. Define the predicate class in `sealighter/sealighter_predicates.h`, deriving from
   `predicates::details::predicate_base` and implementing `operator()(const EVENT_RECORD&, const trace_context&)`.
   Follow the existing patterns: stateful predicates (`sealighter_max_events_id`, `sealighter_max_events_total`)
   use `mutable` counters; schema-based predicates build `krabs::schema`/`krabs::parser` per event.
2. Register the JSON key in `add_filters_to_vector` (`sealighter/sealighter_controller.cpp`) using
   `add_filter_to_vector_basic`, `add_filter_to_vector_basic_pair`, or a custom parser — the key then works in
   `any_of`/`all_of`/`none_of` with scalar-or-array semantics for free.
3. Document the key in `docs/FILTERING.md` and the wiki `configuration/filtering.md`.

**Example already in the working tree**: the uncommitted `GUID` branch in `add_filter_to_vector_property_is_item`
adds a `GUID` type to `property_is` (`convert_str_guid` + `sealighter_property_is<krabs::guid>`). Docs
(`docs/FILTERING.md`) have not caught up — when landing such a change, update the docs too.

### 2. Add a new kernel provider

In `add_kernel_traces` (`sealighter_controller.cpp`) add an `else if (provider_name == "x")` branch mapping to the
corresponding `krabs::kernel::*_provider` class, and update the list in `docs/CONFIGURATION.md`. Unknown names
return `SEALIGHTER_ERROR_PARSE_KERNEL_PROVIDER`.

### 3. Support a new property INTYPE in event parsing

1. Add a `case TDH_INTYPE_*` in the switch in `parse_event_to_json` (`sealighter_handler.cpp`) that parses the
   property and records its `property_types` label.
2. Add any conversion helper to `sealighter/sealighter_util.{h,cpp}` (see `convert_filetime_string`,
   `convert_bytes_sidstring`, etc.).
3. If the type should be filterable via `property_is`, add the matching branch in
   `add_filter_to_vector_property_is_item` and document the label.

### 4. Add an output sink

`sealighter_handler.h` owns the `Output_format` enum (`output_stdout`, `output_event_log`, `output_file`).
Changes touch: the enum, `output_json_event`'s switch, `set_output_format`, and the `output_format` string mapping
in `parse_config` (`sealighter_controller.cpp`). Remember the legacy duplicate enum in `sealighter/util.h` is dead
code — do not edit it.

### 5. Change interprocess behavior

Named events live in `sealighter_controller.cpp` (`EVENT_SEALIGHTER_STARTED`, `EVENT_STOP_SEALIGHTER`) using
`winxx::NamedEvent<wchar_t>` (fork submodule `winxx`). Testing note: `Local\SealighterStarted` is currently set
*before* `trace->start()` runs (commit `4617231` intended it to fire only after a successful start, but the later
`f7d6c57` refactor moved it earlier) — verify this ordering still matches whatever external process consumes the
event; `Local\StopSealighter` should stop Sealighter cleanly.

## Conventions & code notes

- **Logging**: use `log_messageA` / `log_messageW` macros (→ `logger::Logger::GetInstance().logger()` spdlog).
  They also write to `c:/notouchme/sealighter/sealighter.log`.
- **JSON**: `sealighter_json.h` aliases `nlohmann::json`; config is parsed with try/catch on
  `nlohmann::detail::exception`, mapped to error codes in `sealighter_errors.h`.
- **Strings**: `convert_wstr_str` uses `exutils::WStringToString(CP_UTF8, ...)`; many other helpers in
  `sealighter_util.h`. Wide strings are pervasive because ETW is wide.
- **Style**: the codebase is C-heavy C++ ("I mainly work in C" — see `docs/COMPARISION.md` and
  `docs/LIMITATIONS.md`). Prefer matching surrounding style over modernizing everything at once.
- **Threading**: event callbacks fire on trace threads; guard shared output/buffering state with the existing
  mutexes (`g_print_mutex`, `g_buffer_lists_mutex`). Don't add unbounded per-event allocations before filtering.
- **Non-ASCII**: explicitly unsupported/untested in config, filtering, and events (`docs/LIMITATIONS.md`).

## Gotchas checklist

- Config key is `buffering_timout_seconds` (typo is load-bearing).
- Buffers config uses `properties_to_match`, not `fields` (the example in `docs/CONFIGURATION.md` is wrong).
- Code default buffer timeout = 5 s; `docs/CONFIGURATION.md` claims 30 s — docs stale.
- `vamap` is the accepted kernel provider string in code (docs list `vamap_provider`).
- Debug builds need the machine-specific vcpkg spdlog include path (see [Build & run](../operations/build-and-run.md)).
- `sealighter_provider.h` is **generated** (mc.exe) from `sealighter_provider.man`; edit the manifest, not the header.
- `sealighter/util.cpp` + `util.h` are dead (not compiled); ignore them.
- When both user and kernel sessions run, they run on separate threads; output must stay mutex-safe.

## Known limitations (from docs/LIMITATIONS.md)

- Non-ASCII strings may break filtering/config/output.
- 32-bit unsupported (x64 only).
- Not production-ready / not an EDR replacement.
- WPP traces cannot be auto-parsed — use `dump_raw_event` and parse the hex afterwards.
- No pretty-print option for file output; no Related Activity ID parsing (future work).

## Fork history (why the code looks this way)

The repo is a fork of [pathtofile/Sealighter](https://github.com/pathtofile/Sealighter) (upstream base visible at
merge `654306d`), maintained by `donfucius`. Post-fork evolution visible in git log:

- `558d4c4`/`af2f93f` — added named-event interprocess control (`Local\SealighterStarted` / `Local\StopSealighter`).
- `e40b5c8` — removed a "sealighter stopped" event; `17eac6d` — force-exit on stop event.
- `4617231` — fixed `SealighterStarted` to be set only after trace start (later moved earlier again by `f7d6c57`); added `exutils/`; fixed wstring→string bug.
- `f7d6c57` — spdlog-based `logger.cpp/h` replacing printf-based logging; `5de663c` — minor fixes.
- `bc0df7a`/`1314c0a` — project file updates; `e502386` — dropped the O365 .NET ETW project from the solution.
- `22edfd2` — updated the `winxx` fork submodule.
- Uncommitted — `.gitignore` cleanup (ignores `.*/` and AI tooling files) + `GUID` support in `property_is`.

Use `git log -p -- <file>` on `sealighter_controller.cpp`/`sealighter_handler.cpp` when a change's intent is unclear.
