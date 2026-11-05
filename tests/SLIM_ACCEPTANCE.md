# Slim record acceptance

Run from the repository root. The C++ tests exercise the actual parser, including
an INT32 whose schema length is deliberately two bytes (typed decode throws,
hex fallback succeeds), and an unsupported HEXINT32 (OTHER). Both retain all
other values, name the affected field, and round-trip as one-line JSON without
the type map. Only the test's private schema cache is mutated.

```powershell
& .\x64\Debug\sealighter_tests.exe
python -m unittest discover -s tests -p test_verify_slim_capture.py -v
```

The deterministic A/B C++ test processes the same records in both sessions,
checks values and sequence identifiers, and checks that fewer bytes are emitted.
It does not prove live queue/drop behavior or the workload-specific 37% saving.

## Actual Event Log delivery

Install the repository's provider manifest using the normal installation process,
with its resource/message paths pointing at the built Sealighter executable.
Then run in an elevated shell:

```powershell
$env:SEALIGHTER_EVENT_LOG_TEST = '1'
& .\x64\Debug\sealighter_tests.exe '--gtest_filter=SealighterSession_EventLogIntegration.*'
Remove-Item Env:SEALIGHTER_EVENT_LOG_TEST
```

This test registers the publishing handle, writes a slimmed event with a unique
trace marker, polls `Sealighter/Operational`, and compares the JSON payload read
from the channel with the original. Missing/inaccessible channels and missing
delivery fail the opted-in test. Without the environment variable it is explicitly
skipped. It never clears the log or installs a manifest itself.

## Real workload A/B

1. Preserve the pre-change binary hash and a baseline capture from the sandbox.
   For default byte compatibility, replay identical source records through the
   pre-change build and the new build with all options absent, then compare their
   JSONL bytes (independent live runs cannot establish byte equality).
2. Capture the same workload with the type map and three allowed header fields
   omitted, `record_parse_errors: true`, and `timestamp_precision: "ms"`.
3. Compare full/slim captures:

```powershell
python tests/verify_slim_capture.py full.jsonl slim.jsonl
# For identical-record replay, also verify values and order:
python tests/verify_slim_capture.py full.jsonl slim.jsonl --same-records
```

The script always fails on missing required headers, incorrect timestamp precision,
retained omitted metadata, empty `parse_errors` lists, a `parse_errors` name set that
differs from the baseline ERROR/OTHER fields, differing per-capture diagnostic
coverage, or no size reduction. Record-set comparison depends on how the two captures
were produced: with `--same-records` (replay of identical `EVENT_RECORD`s) the
provider/task/event/trace distribution must match exactly and so must values and
ordering; without it, the run is two independent live captures, where the script
instead fails if any bucket of 5 or more rows vanishes or appears, or if total drift
exceeds 20% of records. Those live tolerances come from measurement, not taste: two
unslied runs of the same sample and window drifted 6.0% across 28 of 55 buckets, and
the largest bucket that vanished between real pairs held 2 rows.

It reports bytes, saving percentage, drift and diagnostic field count; check the
measured capture budget (~12 MB for the reference workload) against that report.
It does not prove live queue/drop behaviour or a workload-specific saving percentage.

4. Export the JSON payloads from the channel for precisely the same run into
   `channel.jsonl`. For the Sealighter channel, these are `Properties[0].Value`
   from `Get-WinEvent`; scope the query to that run's time range and trace names.
   For the reference Dynomon parity check, use that provider's own payload export
   and compare its source PID distribution separately (it is a different schema).
   Check exact Sealighter channel/file parity with:

```powershell
python tests/verify_slim_capture.py full.jsonl slim.jsonl --channel-jsonl channel.jsonl
```

Exact multiset equality checks every payload, including per-source PID counts;
channel retrieval order is allowed to differ. Preserve both collectors' final
`event_queue_drops` and `kernel_events_lost` counters with the report. Queue
capacity remains entry-count bounded, so byte reduction alone does not establish
fewer drops. Re-run downstream analysis/annotation tools on the saved capture.

## Local verification (2026-11-05)

- Rebuilt Debug test executable: 105 C++ tests passed, one opt-in integration test
  skipped in the normal suite. Six Python verifier tests passed.
- Found spdlog in the local vcpkg static include directory; supplied it through
  the compiler environment for the build rather than changing project paths.
- Attempted temporary manifest registration for the real publication test.
  `wevtutil im` failed with Windows error 6801: transaction support within the
  resource manager is not started or was shut down. The channel had previously
  been confirmed absent. No actual channel-delivery pass is claimed.
- The sandbox reference capture, binary replacement, Dynomon parity and reader
  sweep remain environment acceptance work; synthetic tests do not replace them.

## Sandbox verification (2026-11-06)

Run on etwsbx (`hostname` ETWSBX, kernel debug `No`, `hssec`/`filecap` RUNNING, no
stale `Sealighter-Trace` session, C: in shadow mode). Same sample
(`C:\temp\horus_8065c85e.dll`) and the same `--wait 6 --timeout 30` window, four captures:

| tag | build | options | records | bytes | final drops |
|---|---|---|---|---|---|
| sR1 | pre-change `79381331ab…` | none | 13,702 | 18,751,509 | 47,899 |
| sR2 | `b5f37553…` (pre-flip) | none | 14,221 | 19,546,164 | 54,545 |
| sR3 | `b5f37553…` | all four | 14,551 | 12,589,396 | 31,125 |
| sR4 | `7d846047…` (ms default) | none | 14,385 | 19,453,207 | 54,595 |

- `kernel_events_lost=0` in all four, so every loss is the collector's entry-count
  queue. Arrival (records plus drops) swung from 45,676 to 68,766 per window.
- sR2 vs sR3 through the verifier: passed, saved_percent 35.59, drift 11.64,
  166 diagnostic fields. Per record 1,374.5 B to 865.1 B, i.e. 37.1%.
- `parse_errors` on real traffic named only Sysmon `GrantedAccess` (event 10) 162
  rows and `LogonId` (event 1) 4 rows, and the values kept their bare uppercase hex
  with no `0x` prefix. Those are the same two fields the unslied captures label OTHER.
- Default path with the milliseconds build and a config naming none of the options:
  `property_types` on 14,385/14,385 records (293 of them null-valued, which is
  pre-existing for an event whose properties never got a typed label), all 12 header
  keys on every record, `parse_errors` on none, and the fraction present on
  14,385/14,385 header timestamps and 22,579/22,579 date-like property values, a
  count equal to that capture's FILETIME label total.
- Dynomon parity on the slim run: 24 channel events vs 24 file records, grouped
  3692x17 and 3980x7 by recording process, matching the file exactly; the channel Id
  spread mapped one to one onto the parser's by-task spread. Dynomon held 24 records
  with the same per-task split in all four captures, and in-tree Kernel-File Write
  held 204/197/202/204.
- Reader sweep: `parse_dynomon_log.py` produced identical event counts, by-task split
  and function histogram on the slim capture as on the unslied ones; its `base: ?`
  and `114 total: 0` lines appear the same way on the unslied captures, so they are a
  property of this sample, not of slimming. `Sealighter/print_output_file.py` names no
  key and is unaffected.
- Only the collector and its config were deployed. `install_all.cmd` was deliberately
  not run: it deletes and recreates the `hssec` and `filecap` driver services and would
  install Debug `.sys` files, which bugcheck the VM without a kernel debugger, and it
  re-registers every ETW manifest, which shadow mode does not preserve. The binary and
  config were restored afterwards and re-verified to `79381331ab…`.
- Not verified, and now out of scope by decision: a record actually reaching
  `Sealighter/Operational`. The `Sealighter` provider is registered on neither host, and
  the generated write macro returns `ERROR_SUCCESS` without emitting when the provider
  is not enabled, so success from `output_format: event_log` is not evidence of delivery.
- Verifier teeth were checked by injection against the sR2/sR3 pair: deleting all
  Dynomon rows failed the vanished-bucket floor, deleting a quarter of the records failed
  the drift bound at 23.8%, and dropping 80 `parse_errors` entries failed coverage at
  1.0 vs 0.506. The unmodified pair passed. Scratch inputs are in
  `C:\devdrv\src\tmp\slim_ab\inj_*.log`.
