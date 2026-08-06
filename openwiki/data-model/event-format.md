# Event Format

Every reported event is a JSON object with three always-present sections (`header`, `properties`,
`property_types`), plus optional `stack_trace` (when `report_stacktrace` is set) or `raw` (when `dump_raw_event`
is set). Authoritative user docs: `docs/PARSING_DATA.md`. The parser is `parse_event_to_json` in
`sealighter/sealighter_handler.cpp`; conversions live in `sealighter/sealighter_util.cpp`.

## Example (Microsoft-Windows-Kernel-Process, event 1)

```json
{
    "header": {
        "activity_id": "{00000000-0000-0000-0000-000000000000}",
        "event_flags": 576,
        "event_id": 1,
        "event_name": "",
        "event_opcode": 1,
        "event_version": 3,
        "process_id": 17964,
        "provider_name": "Microsoft-Windows-Kernel-Process",
        "task_name": "ProcessStart",
        "thread_id": 25932,
        "timestamp": "2020-05-17 11:54:24Z",
        "trace_name": "proc_trace",
        "buffered_count": 2
    },
    "properties": {
        "CreateTime": "2020-05-17 11:54:24Z",
        "ImageName": "\\Device\\HarddiskVolume4\\Windows\\System32\\notepad.exe",
        "ProcessID": 25752,
        "SessionID": 4
    },
    "property_types": {
        "CreateTime": "FILETIME",
        "ImageName": "STRINGW",
        "ProcessID": "UINT32",
        "SessionID": "UINT32"
    },
    "stack_trace": [ "0x7FFA18BAB944", "0x7FFA1868902A" ]
}
```

## header

Always present, from the ETW event header via `krabs::schema`:

`activity_id`, `event_flags`, `event_id`, `event_name`, `event_opcode`, `event_version`, `process_id`,
`provider_name`, `task_name`, `thread_id`, `timestamp`, `trace_name`.

- `timestamp` = `convert_timestamp_string(schema.timestamp())` → `"YYYY-MM-DD HH:MM:SSZ"` (UTC, from the
  100-ns-since-1601 `LARGE_INTEGER` via `FILETIME`/`SYSTEMTIME`).
- `trace_name` = the config `trace_name`, letting you distinguish multiple traces over the same provider.
- `buffered_count` is added only when buffering merges events (see [Configuration → buffering](config-file.md#buffering)).

## properties & property_types

`properties` holds parsed event fields; `property_types` holds the `TDH_INTYPE` label of each field, which is what
you use as `type` in [property filters](filtering.md#property-filters).

Parsing (`parse_event_to_json`): for each `krabs::property`, switch on `prop.type()`:

| TDH_INTYPE | JSON output | property_types label |
| --- | --- | --- |
| ANSISTRING / UNICODESTRING | string | `STRINGA` / `STRINGW` |
| INT8…UINT64 | number | `INT8`…`UINT64` |
| FLOAT / DOUBLE | number | `FLOAT` / `DOUBLE` |
| BOOLEAN | bool | `BOOLEAN` |
| BINARY | uppercase hex string | `BINARY` |
| GUID | `{...}` string (`convert_guid_str`) | `GUID` |
| FILETIME / SYSTEMTIME | `"YYYY-MM-DD HH:MM:SSZ"` | `FILETIME` / `SYSTEMTIME` |
| SID | `DOMAIN\user` via `LookupAccountSidA`, else hex | `SID` |
| WBEMSID | hex | `WBEMSID` |
| POINTER | `0x`-prefixed hex (`convert_ulong64_hexstring`) | `POINTER` |
| everything else / unknown | hex bytes | `OTHER` |
| parse failure fallback | hex bytes | `ERROR` |

Notes:

- Unknown INTYPEs (HEXINT32/64, counted/character strings, HEXDUMP, NULL, etc.) fall through to hex + `OTHER`.
- If a property parse throws, the code tries a binary parse and labels it `ERROR` (else the field is dropped).
- Binary/hex strings are uppercase with no `0x` prefix (except POINTER).

## stack_trace (optional)

When `report_stacktrace: true`, extended data is scanned for `EVENT_HEADER_EXT_TYPE_STACK_TRACE64`
(always TRACE64, even for 32-bit producers). Emitted as an array of `0x`-prefixed addresses from the event's
`ExtendedData`. The stack's `MatchId` is ignored — stacks split across events are not stitched together.

## raw (optional)

When `dump_raw_event: true` (for WPP/unknown-format providers), the parser is skipped and `json_event["raw"]`
holds the hex-encoded `record.UserData` bytes. Parse the bytes yourself afterwards
(see `docs/LIMITATIONS.md` — WPP formats can't be auto-parsed).

## Output sink behavior

| `output_format` | Behavior |
| --- | --- |
| `stdout` | Pretty-printed (4-space indent) via the logger (`log_messageA`). |
| `file` | **One compact JSON object per line** (no pretty print); `print_output_file.py` pretty-prints such a file for reading. |
| `event_log` | Written as a single `Sealighter/Operational` event with the JSON as the `json` field plus denormalized header fields (see `write_event_log` and `sealighter_provider.man`). |

## Consumers

Events are meant to be parsed from JSON in Python/PowerShell, or shipped to Splunk/ELK
(`docs/PARSING_DATA.md` has worked parsing examples). `print_output_file.py` (repo root) is the bundled helper:

```bash
python print_output_file.py path/to/output.json
```
