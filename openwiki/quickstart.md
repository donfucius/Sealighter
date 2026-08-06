# Sealighter — OpenWiki Quickstart

## What this repository is

**Sealighter** is a config-driven **Event Tracing for Windows (ETW) and WPP tracing tool for security research**.
It lets you subscribe to multiple user-mode, kernel-mode, TraceLogging, and WPP providers at once, automatically
parses every event into **JSON** (no knowledge of the event format needed), and outputs the stream to **stdout,
a file, or the Windows Event Log** (channel `Sealighter/Operational`).

- Built on Microsoft's [KrabsETW](https://github.com/microsoft/krabsetw) C++ library for session control, filtering, and event parsing.
- Written in native **C++20**, **x64 only**, built with **Visual Studio 2022 (v143)** via MSBuild.
- A *research* tool, not a production EDR replacement (see [docs/LIMITATIONS.md](docs/LIMITATIONS.md)).
- The name is a joke on "Seafood Highlighter" (fake crab meat) because it is built on *Krabs* ETW.

This wiki is the OpenWiki knowledge base for the repository. It maps the codebase, the config/event data model,
the build/ops story, and the extension points, and points at the authoritative `docs/` folder for in-depth user docs.

## Wiki layout

| Page | Covers |
| --- | --- |
| [Architecture overview](architecture/overview.md) | Components, runtime flow, sessions, threading, interprocess control, error handling, legacy code |
| [Configuration file](configuration/config-file.md) | `session_properties`, `user_traces`, `kernel_traces`, buffering; key schema gotchas |
| [Filtering](configuration/filtering.md) | `any_of`/`all_of`/`none_of` lists, all filter types, and their C++ implementation |
| [Event format](data-model/event-format.md) | The JSON output shape (`header`, `properties`, `property_types`, `stack_trace`, `raw`) and the parsing engine |
| [Build & run](operations/build-and-run.md) | Requirements, MSBuild/CI, running, Windows Event Log manifest, dependencies/submodules |
| [Extending Sealighter](development/extending.md) | How to add filters, kernel providers, property types; conventions, gotchas, fork history, limitations |

## Repo at a glance

```
sealighter/            # The app (single Visual Studio project)
  sealighter_main.cpp  # entrypoint: argv[1] = config path → run_sealighter()
  sealighter_controller.cpp/h  # config parsing, session setup, run/stop orchestration
  sealighter_handler.cpp/h     # event → JSON pipeline, output, buffering engine
  sealighter_predicates.h      # custom filter predicates (any/all/none_of, discovery filters)
  sealighter_util.cpp/h        # JSON/time/GUID/SID/hex conversion helpers
  sealighter_provider.man      # ETW manifest for the Sealighter/Operational event log channel
  logger.cpp/h                 # spdlog-based logger (console + file)
  util.cpp/h                   # LEGACY dead code, not compiled
exutils/               # local string/path utilities (used by sealighter_util)
docs/                  # authoritative user docs (installation, configuration, filtering, buffering, parsing, scenarios, limitations, SilkETW comparison)
example_config.json    # minimal stdout config (process start/stop)
example_config_event_log.json  # same but writes to Windows Event Log
print_output_file.py   # helper to pretty-print file output
GSL/, json/, krabsetw/, winxx/  # git submodules (see operations/build-and-run.md)
```

## Quick start

1. **Check out with submodules**: `git clone --recurse-submodules <url>`
2. **Build** (Windows, x64): `msbuild.exe /m /p:Configuration=Release sealighter.sln` — requires VS2022 v143 toolset; Debug builds additionally expect an spdlog vcpkg include path (see [Build & run](operations/build-and-run.md)).
3. **Run as Administrator**: `sealighter.exe path\to\config.json`
4. Minimal config (`example_config.json` shape) logs process create/terminate events to stdout:

```json
{
    "session_properties": { "session_name": "My-Process-Trace", "output_format": "stdout" },
    "user_traces": [
        { "trace_name": "proc_trace", "provider_name": "Microsoft-Windows-Kernel-Process", "keywords_any": 16 }
    ]
}
```

5. Press **Ctrl+C** to stop; Sealighter also stops when it receives the `Local\StopSealighter` named event
   (see [Architecture → interprocess control](architecture/overview.md#interprocess-control)).

For event-log output you must first install the provider manifest
(`wevtutil im sealighter_provider.man` after substituting `!!SEALIGHTER_LOCATION!!`).

## Current repo state (init run)

- `HEAD`: `22edfd2` ("update winxx")
- **Uncommitted changes**:
  - `.gitignore` — now ignores `.*/` (all dot-directories incl. `.github/`, `.vscode/`) and the AI-assistant root files
    (`AGENTS.md`, `CLAUDE.md`, `CODEBUDDY.md`, `GEMINI.md`, `QODER.md`, `.cursorrules`, `.mcp.json`, `.windsurfrules`, `opencode.jsonc`).
  - `sealighter/sealighter_controller.cpp` — adds `GUID` as a supported `property_is` filter type
    (docs/FILTERING.md does **not** mention it yet — documentation gap).
- `AGENTS.md`, `CLAUDE.md`, and `.github/workflows/openwiki-update.yml` are **local, untracked, gitignored** OpenWiki
  scaffolding on this machine (the scheduled workflow and the `OPENWIKI:START/END` markers), not part of the committed repo.

## Where to go next

- New to the code? → [Architecture overview](architecture/overview.md)
- Writing a config? → [Configuration file](configuration/config-file.md) and [Filtering](configuration/filtering.md)
- Consuming output? → [Event format](data-model/event-format.md)
- Building or releasing? → [Build & run](operations/build-and-run.md)
- Changing code? → [Extending Sealighter](development/extending.md)
