# Architecture Overview

## Component map

| Component | Files | Role |
| --- | --- | --- |
| Entrypoint | `sealighter/sealighter_main.cpp` | Validates argv, loads config file to string, calls `run_sealighter()`. Initializes the spdlog logger to `c:/notouchme/sealighter/sealighter.log` + console. |
| Controller | `sealighter/sealighter_controller.cpp` / `.h` | Parses config JSON, builds KrabsETW sessions/providers/filters, runs/stops traces, owns the Ctrl+C and named-event stop paths. Public API: `run_sealighter(config_string)`, `stop_sealighter()`. |
| Handler | `sealighter/sealighter_handler.cpp` / `.h` | Event callback pipeline: parses an `EVENT_RECORD` into JSON, buffers events, and outputs to stdout/file/event log. |
| Predicates | `sealighter/sealighter_predicates.h` | Sealighter's custom filter predicates (any/all/none-of combinators + discovery filters). |
| Providers (manifest) | `sealighter/sealighter_provider.man`, `.h`, `.rc`, `MSG00001.bin`, `resource.h` | Message-Compiler (mc.exe) generated ETW provider `Sealighter` (GUID `{CDD5F0CC-AB0C-4ABE-97B2-CC82B7E68F30}`) that backs the `Sealighter/Operational` event-log output. |
| Utilities | `sealighter/sealighter_util.cpp` / `.h` | JSON/time/GUID/SID/hex/lowercase conversion helpers used across the pipeline. |
| Local utils | `exutils/exutils.cpp` / `.h` | Small string/path utilities (WString↔String via code page, case-insensitive compare/search). Used by `sealighter_util.cpp`. |
| Logger | `sealighter/logger.cpp` / `.h` | spdlog singleton (`logger::Logger`) with console sink (info, `%v`) + file sink (trace). |
| Legacy (dead) | `sealighter/util.cpp` / `.h` | Older utility module with a duplicate `Output_format` enum and `etw_handler.h` include. **Not referenced by the .vcxproj and not compiled.** |

## Runtime flow

```
main(argv[1])
 └─ run_sealighter(config_string)                       [controller]
     ├─ EventRegisterSealighter()                       register ETW provider (event log output)
     ├─ std::jthread(WaitForStopEvent)                  background waiter on Local\StopSealighter
     ├─ SetConsoleCtrlHandler(crl_c_handler)            Ctrl+C → stop_sealighter()
     ├─ parse_config(config_string)
     │    ├─ json::parse + session_properties defaults  BufferSize=256, MinBuffers=12, MaxBuffers=48,
     │    │                                            FlushTimer=1, REAL_TIME + INDEPENDENT_SESSION mode
     │    ├─ add_user_traces(...)                       user_trace + user providers (keywords/level/flags,
     │    │                                            filters, buffers, dump_raw_event)
     │    └─ add_kernel_traces(...)                     kernel_trace + NT Kernel Trace sub-providers
     ├─ start_bufferring()                              buffering flush thread (only if buffers configured)
     ├─ run_trace(...)                                  single-threaded if only one session type, else
     │                                                std::thread per session (user + kernel)
     │    └─ trace->start()
     └─ stop_bufferring(); teardown_logger_file(); EventUnregisterSealighter()
```

Ordering caveat: in the current code `SetSealighterStartedEvent()` is called **before** `run_trace(...)` in each
branch, i.e. before `trace->start()` actually runs. History: commit `4617231` originally moved the event-set
*inside* `run_trace` so it fired only after `start()` succeeded, but the `f7d6c57` logger refactor moved it back out
into a helper called pre-start. If external coordination depends on "trace is really running", this ordering is a
known inconsistency to watch.

## Session & provider model

- **User sessions**: one `krabs::user_trace` for all user-mode/TraceLogging/WPP providers. Provider GUIDs are used
  directly; names are passed to Krabs to resolve (`add_user_traces`, `sealighter_controller.cpp`).
- **Kernel sessions**: one `krabs::kernel_trace` for NT Kernel Trace sub-providers. `add_kernel_traces` maps the
  config `provider_name` string ("process", "image_load", "registry", … 26 supported) onto
  `krabs::kernel::*_provider` classes.
- If both user and kernel traces are configured, each session runs on its own thread; otherwise a single thread is used.
- A default event callback (`handle_event`) is attached to the user session; per-provider callbacks
  (`handle_event_context` with a `sealighter_context_t` carrying `trace_name` + `dump_raw_event`) are attached via
  filters or directly when no filters are configured.

## Event pipeline

1. KrabsETW delivers an `EVENT_RECORD` + `trace_context` to a callback.
2. `handle_event_context` builds a `krabs::schema`, calls `parse_event_to_json`, then either buffers or outputs.
3. `parse_event_to_json` (`sealighter_handler.cpp`) produces `header` + `properties` + `property_types` from the
   schema/parser (TDH-driven), optionally `stack_trace` (STACK_TRACE64 extended data) or `raw` (hex dump when
   `dump_raw_event`), then `output_json_event` writes it per `g_output_format`.
4. Output is serialized under a global mutex (`g_print_mutex`) so multi-threaded traces emit whole events.

Full shape of the JSON and the parsing rules: [Event format](../data-model/event-format.md).

## Threading & synchronization

| Mechanism | Purpose |
| --- | --- |
| `g_print_mutex` (handler) | Serialize whole-line output to stdout/file from multiple trace threads. |
| `g_buffer_lists_mutex` + condition variable | Protect buffering state; the buffer flush thread waits on `wait_until(timeout)` and flushes buffered events on each timeout (`bufferring_thread`, `flush_buffered_lists`). |
| `std::jthread(WaitForStopEvent)` | Blocks on `Local\StopSealighter` named event; on signal → `stop_sealighter()` + `exit(0)`. |
| `crl_c_handler` | Ctrl+C → `stop_sealighter()` (stops both sessions if present). |

## Interprocess control

The fork this repo tracks added **named-event IPC** so an external process can coordinate with Sealighter:

- `Local\SealighterStarted` — set by `SetSealighterStartedEvent()` (winxx `NamedEvent<wchar_t>`,
  `EVENT_MODIFY_STATE`). In the current code it is signaled *before* the trace starts (see ordering caveat above);
  commit `4617231` had moved it after a successful `start()` but the later `f7d6c57` refactor regressed that.
- `Local\StopSealighter` — waiting on this event causes Sealighter to stop and exit; combined with the Ctrl+C path it
  guarantees traces are torn down on shutdown.

History: interprocess events added in `558d4c4`/`af2f93f`, a "stopped" event removed in `e40b5c8`, forced-exit-on-stop
added in `17eac6d`, and the start-event timing fixed in `4617231`. These are the main reason the `winxx` fork
submodule (`https://github.com/donfucius/winxx.git`) is used.

## Error handling

- Error codes are centralized in `sealighter/sealighter_errors.h` (1–14, e.g. `SEALIGHTER_ERROR_NOCONFIG`,
  `SEALIGHTER_ERROR_PARSE_FILTER`, `SEALIGHTER_ERROR_NO_PROVIDER`, `SEALIGHTER_ERROR_OUTPUT_FILE`).
- Config parsing wraps each phase in `try/catch (nlohmann::detail::exception)` and maps failures to the matching
  error code with `log_messageA` diagnostics.
- `run_trace` wraps `trace->start()` and always calls `trace->stop()` on exception before rethrowing, so a failing
  start can't leak a session.
- Provider resolution failure (unknown provider name) → `SEALIGHTER_ERROR_NO_PROVIDER` with the Krabs exception message.

## Legacy / dead code

- `sealighter/util.cpp` + `util.h` are not in `sealighter.vcxproj` (only `exutils.cpp`, `logger.cpp`,
  `sealighter_controller.cpp`, `sealighter_main.cpp`, `sealighter_handler.cpp`, `sealighter_util.cpp` are compiled).
  They contain an older `Output_format` enum (`output_all`) and an include of a non-existent `etw_handler.h`.
- `event_buffer_t` in `sealighter_handler.h` is declared but unused (buffering uses `event_buffer_list_t`).
- The duplicate `Output_format` enum in `util.h` conflicts conceptually with the authoritative one in `sealighter_handler.h`.
