# Configuration File

The Sealighter config file is **JSON** and has three top-level parts. It is the only user input besides the binary.
Full upstream documentation lives in `docs/CONFIGURATION.md` (and `docs/BUFFERING.md`, `docs/FILTERING.md`,
`docs/SCENARIOS.md` for worked examples). This page is the map between the config schema and the parsing code in
`sealighter/sealighter_controller.cpp`.

```json
{
    "session_properties": { "session_name": "My-Process-Trace", "output_format": "stdout", "buffering_timout_seconds": 10 },
    "user_traces": [ { "trace_name": "proc_trace", "provider_name": "Microsoft-Windows-Kernel-Process", "keywords_any": 16 } ],
    "kernel_traces": [ { "trace_name": "kernel_proc_trace", "provider_name": "process" } ]
}
```

## session_properties

Parsed in `parse_config` (`sealighter_controller.cpp`). Defaults come from the code, not the docs.

| Key | Meaning | Code default |
| --- | --- | --- |
| `session_name` | Name of the ETW session | `Sealighter-Trace` |
| `output_format` | `stdout` \| `event_log` \| `file` | unset (stdout path used by default enum value) |
| `output_filename` | Required when `output_format == "file"`; missing → `SEALIGHTER_ERROR_OUTPUT_FILE` | — |
| `buffer_size` | In-memory buffer size (KB) | 256 |
| `minimum_buffers` | Min buffers | 12 |
| `maximum_buffers` | Max buffers | 48 |
| `flush_timer` | Buffer flush timer (seconds) | 1 |
| `buffering_timout_seconds` | Buffering flush period (note the **typo "timout"** is part of the key, in code and docs) | code: **5 s**; docs/CONFIGURATION.md claims 30 s — **docs are stale vs code** (`g_buffer_lists_timeout_seconds = 5` in `sealighter_handler.cpp`) |

The session always runs in `EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_INDEPENDENT_SESSION_MODE`.

## user_traces

Array of user-mode / TraceLogging / WPP providers. Only `trace_name` and `provider_name` are required
(`add_user_traces`). Options, in code order:

| Key | Notes |
| --- | --- |
| `trace_name` | Label embedded in every output event's `header.trace_name`; distinguishes multiple traces over the same provider. |
| `provider_name` | Provider **name** or **GUID** (`{...}`). GUIDs are used directly; names are resolved by Krabs (failure → `SEALIGHTER_ERROR_NO_PROVIDER`). TraceLogging/WPP must be GUIDs. |
| `keywords_any` / `keywords_all` | `uint64` keyword masks applied in the kernel (via `provider->any()/all()`). If neither is set, all events pass to filters. More efficient than userland filters — prefer them. |
| `level` | Minimum ETW level; `provider->level(data)`; defaults to `0xff` (all) when absent. |
| `trace_flags` | Advanced provider flags. |
| `report_stacktrace` | `true` adds `EVENT_ENABLE_PROPERTY_STACK_TRACE` to trace flags; events then include a `stack_trace` array. |
| `filters` | See [Filtering](filtering.md). |
| `buffers` | Per-event-id buffering; see below. |
| `dump_raw_event` | `true` → skip parsing; emit hex-encoded raw bytes under `raw`. For WPP traces with unknown format. |

## kernel_traces

Array of NT Kernel Trace sub-providers (`add_kernel_traces`). `trace_name` + `provider_name` required.
`provider_name` must be one of the 26 strings mapped in code: `process`, `thread`, `image_load`, `process_counter`,
`context_switch`, `dpc`, `debug_print`, `interrupt`, `system_call`, `disk_io`, `disk_file_io`, `disk_init_io`,
`thread_dispatch`, `memory_page_fault`, `memory_hard_fault`, `virtual_alloc`, `network_tcpip`, `registry`, `alpc`,
`split_io`, `driver`, `profile`, `file_io`, `file_init_io`, `vamap`, `object_manager`.
(Note: code accepts `vamap`, while `docs/CONFIGURATION.md` lists `vamap_provider` — the code string wins.)
Unknown values → `SEALIGHTER_ERROR_PARSE_KERNEL_PROVIDER`.

## Buffering

Buffering collapses many similar events in a time window into one event with a `header.buffered_count`.

- **Global**: `session_properties.buffering_timout_seconds` controls the flush period (default 5 s in code).
- **Per provider**: `buffers` array, each entry:
  - `event_id` — event ID to buffer
  - `max_before_buffering` — number of matching events to report as-is before buffering (0 = buffer all)
  - `properties_to_match` — property names; events equal on all these fields are "the same" and merged

Implementation: `handle_event_context` → `handle_event_context` buffer logic in `sealighter_handler.cpp`; the flush
thread (`bufferring_thread`) emits all buffered events each timeout and resets counts.

### Schema gotchas (verified against code)

1. `buffering_timout_seconds` — misspelled "timout" in code and docs; use exactly this key.
2. Buffers use `properties_to_match` — but the example in `docs/CONFIGURATION.md` shows `"fields": ["ImageName"]`,
   which the code **does not read**. `docs/BUFFERING.md` correctly says `properties_to_match`.
3. Buffer default timeout: code 5 s vs docs 30 s.
4. `output_format == "file"` requires `output_filename`, else exit code 13.
5. If `user_traces` and `kernel_traces` are both absent → `SEALIGHTER_ERROR_PARSE_NO_PROVIDERS`.

## Example configs in the repo

- `example_config.json` — process start/stop (`event_id_is: [1,2]` on `Microsoft-Windows-Kernel-Process`) → stdout.
- `example_config_event_log.json` — same but `output_format: "event_log"`.

See `docs/SCENARIOS.md` for realistic research scenarios (DNS/WMI/PowerShell tracking, any-field search, stack traces, WPP).
