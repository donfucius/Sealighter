# Build & Run

## Requirements

- **Windows 7+ (x64)**. The project is x64-only by design (`sealighter.vcxproj` has only `Debug|x64` /
  `Release|x64`; see `docs/LIMITATIONS.md`).
- **Visual Studio 2022** with the **v143** toolset (`WindowsTargetPlatformVersion 10.0`, C++20
  `LanguageStandard stdcpp20`, `/utf-8` in Debug).
- **Submodules** — clone with `--recurse-submodules`:

| Submodule | Upstream | Used for |
| --- | --- | --- |
| `krabsetw` | https://github.com/microsoft/krabsetw | ETW session control, providers, schema/parser, base predicates |
| `json` | https://github.com/nlohmann/json | all JSON (via `sealighter/sealighter_json.h`) |
| `GSL` | https://github.com/microsoft/GSL | include path only (`GSL\include`) |
| `winxx` | https://github.com/donfucius/winxx.git (**fork**) | `NamedEvent` interprocess control (`Local\SealighterStarted` / `Local\StopSealighter`) |

- **spdlog** for the logger (`sealighter/logger.h`). Debug config resolves spdlog headers from a **machine-specific
  vcpkg path**: `C:\devdrv\scoop\apps\vcpkg\current\packages\spdlog_x64-windows\include`. The Release config does
  not reference spdlog in the include paths shown in the vcxproj, and defines `SPDLOG_USE_STD_FORMAT` in Debug.
  Building on another machine will require adjusting that path or installing spdlog accordingly.
- **MSBuild** (also available as `msbuild.exe` on the CLI after installing VS2022 build tools).

## Build

```bat
msbuild.exe /nologo /m /t:Rebuild /p:Configuration=Debug sealighter.sln
msbuild.exe /nologo /m /t:Rebuild /p:Configuration=Release sealighter.sln
```

Output goes to `$(SolutionDir)$(Platform)\$(Configuration)\` → `x64\Debug\sealighter.exe` /
`x64\Release\sealighter.exe`. The solution contains a single project (`sealighter`). There is no test suite;
verification is manual (run against a config, Ctrl+C / named event to stop).

CI is GitHub Actions (runs on `windows-2019`, builds Debug + Release):

- `.github/workflows/main.yml` — CI on push/PR to `main`.
- `.github/workflows/release.yml` — on `v*` tags, builds and publishes `sealighter.exe`,
  `sealighter.debug.exe`, and `sealighter_provider.man` as release assets.

## Run

```bat
sealighter.exe path\to\config.json
```

- Must run **as Administrator** (ETW sessions, kernel traces, `OpenProcess` in `process_name_contains` filter).
- Config = JSON; see [Configuration file](../configuration/config-file.md). Start with `example_config.json`.
- Exit codes are defined in `sealighter/sealighter_errors.h` (0 = success).
- Stop via **Ctrl+C**, or by signaling the `Local\StopSealighter` named event
  (see [Architecture → interprocess control](../architecture/overview.md#interprocess-control)).
- Startup/shutdown logging goes to console + `c:/notouchme/sealighter/sealighter.log`
  (`LOG_FILE_PATH` in `sealighter_main.cpp`) via the spdlog-based `logger::Logger`.

## Windows Event Log output

For `output_format: "event_log"`:

1. Grab `sealighter/sealighter_provider.man` (or the release asset).
2. Replace `!!SEALIGHTER_LOCATION!!` with the full path to `sealighter.exe` (the manifest's
   `resourceFileName`/`messageFileName`).
3. Install: `wevtutil im path\to\sealighter_provider.man`
4. Events land in **Sealighter/Operational**; verify with
   `(Get-WinEvent -LogName "Sealighter/Operational").Length`.
5. Uninstall: `wevtutil um path\to\sealighter_provider.man`

The manifest defines provider `Sealighter` (GUID `{CDD5F0CC-AB0C-4ABE-97B2-CC82B7E68F30}`) with a single event
`SEALIGHTER_REPORT_EVENT` whose template carries the JSON blob plus denormalized header fields.
`sealighter_provider.h` is Message-Compiler (mc.exe) output regenerated from the manifest; `MSG00001.bin` and
`sealighter_provider.rc` support that build.

## Output file handling

With `output_format: "file"`, each event is one compact JSON line. Use the repo helper to read it:

```bash
python print_output_file.py path\to\output.json
```

## OpenWiki automation (local, uncommitted)

On this machine there are gitignored/untracked OpenWiki scaffolding files:

- `.github/workflows/openwiki-update.yml` — scheduled (daily 08:00 UTC) + manual workflow that runs
  `openwiki code --update --print` and opens a PR with the regenerated `openwiki/` content.
- `AGENTS.md` / `CLAUDE.md` etc. — AI-assistant instruction files carrying the `OPENWIKI:START/END` marker block.

These are ignored by the current uncommitted `.gitignore` (`.*/` + explicit AI files) and are **not** part of the
committed repository. If they are wanted in the repo, the `.gitignore` change must be adjusted.
