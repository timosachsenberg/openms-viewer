# Portable-packaging CI scripts

Each script here is one stage of the portable-package pipelines
(`.github/workflows/unix-portable.yml` and `windows-portable.yml`). The
workflows keep only GitHub-specific orchestration — triggers, matrices, tool
setup, checkout/caching actions, secrets, and artifact upload — and invoke these
scripts for the deterministic build, staging, deployment, and verification work.
That keeps the logic reviewable, lintable, and runnable outside GitHub Actions.

The scripts are behaviour-preserving extractions: the portable layout (bundled
`share/OpenMS`, Qt plugins, OpenMS/contrib dependencies, Thermo/.NET support,
platform runtime-path handling) is unchanged, and every verification stays
fail-closed. Cross-platform dependency mechanics still live in the reusable CMake
helpers under [`../../cmake`](../../cmake): `DeployRuntimeDependencies.cmake` and
`FixupMacBundle.cmake`.

## Stages (Linux / macOS)

Run in this order; Bash, `set -eo pipefail` (matching the runner's default shell
options). Each script documents its required environment variables in a header
comment.

| Script | Stage |
| --- | --- |
| `configure-openms-unix.sh` | Configure OpenMS core (core only, `WITH_GUI=OFF`) |
| `build-openms-unix.sh` | Build and install OpenMS core |
| `test-viewer-unix.sh` | Build and run the viewer Qt Test suites (Linux) |
| `build-viewer-unix.sh` | Configure, build, and install the viewer + Thermo smoke probe |
| `bundle-dotnet-unix.sh` | Stage the app-local .NET 8 runtime and hostfxr |
| `stage-data-unix.sh` | Stage OpenMS data, managed bridge, licenses, provenance |
| `deploy-linux.sh` | Deploy Linux dependencies and repair RPATHs (Linux) |
| `deploy-macos.sh` | `macdeployqt` + bundle fixup + .NET wiring (macOS) |
| `verify-linux.sh` | Fail-closed Linux portable-folder verification (Linux) |
| `audit-linux.sh` | Reject dependencies satisfied only by the runner (Linux) |
| `verify-macos.sh` | Fail-closed macOS verification + ad-hoc signing (macOS) |
| `audit-macos.sh` | Reject bundle dependencies outside the macOS baseline (macOS) |
| `archive-unix.sh` | Create the portable ZIP and SHA-256 checksum |

## Stages (Windows)

Invoked from `windows-portable.yml`. The cmake build stages are Bash (they run
under the runner's Git Bash, `set -eo pipefail`); the filesystem/deployment and
verification stages are PowerShell (`Set-StrictMode -Version Latest` and
`$ErrorActionPreference = "Stop"` for terminating errors). Each script documents
its required environment in a header comment.

| Script | Stage |
| --- | --- |
| `configure-openms-windows.sh` | Configure OpenMS core (core only, `WITH_GUI=OFF`) |
| `build-openms-windows.sh` | Build and install OpenMS core |
| `build-viewer-windows.sh` | Configure, build, and install the viewer + Thermo smoke probe |
| `deploy-windows.ps1` | Deploy the DLL closure, run windeployqt, copy the CRT/OpenMP runtimes |
| `bundle-dotnet-windows.ps1` | Stage the app-local .NET 8 runtime and hostfxr |
| `stage-data-windows.ps1` | Stage OpenMS data, managed bridge, licenses, provenance |
| `verify-windows.ps1` | Fail-closed verification against a restricted PATH |
| `archive-windows.ps1` | Create the portable ZIP and SHA-256 checksum |

## Running locally

The scripts read their inputs from the environment (the same variables the
workflow exports). To reproduce a stage locally, export the variables listed in
the script header and run it, e.g.:

```bash
export RUNNER_OS=Linux
export OPENMS_SOURCE=... OPENMS_BUILD=... OPENMS_INSTALL=... OPENMS_CONTRIB=...
export QT_ROOT_DIR=...
ci/portable/configure-openms-unix.sh
```

Variables that the workflow passes between steps through `$GITHUB_ENV`
(`QT_ROOT_DIR`, `QT_PLUGIN_DIR`, `DOTNET_RUNTIME_VERSION`, …) or via
`$GITHUB_ENV`/`$GITHUB_OUTPUT` file writes must be set in your shell (or the
files pointed at) when running out of context.
