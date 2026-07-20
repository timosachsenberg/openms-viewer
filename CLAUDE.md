# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

OpenMS Viewer is a standalone C++/Qt 6 desktop application for interactive
mass-spectrometry data inspection, intended to replace TOPPView. It links
against the **OpenMS core library** (not TOPPView or the OpenMS GUI library) and
ships as a separate project.

## Build, test, run

The build requires an OpenMS build/install (targets OpenMS 3.6 dev) and Qt 6.4+.
Point CMake at OpenMS via `-DOpenMS_DIR`:

```bash
cmake -S . -B build -DOpenMS_DIR=/path/to/OpenMS-build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

During development, OpenMS is the sibling checkout at
`$HOME/Development/OpenMS/OpenMS-build`.

Run the app (all file arguments optional):

```bash
./build/openms-viewer [sample.mzML] [features.featureXML] [ids.idXML]
```

Default build type is `RelWithDebInfo`. `CMAKE_CXX_STANDARD` is 23. Qt AUTOMOC,
AUTOUIC, and AUTORCC are on.

### Tests

Tests are Qt Test suites compiled into a **single** `openms_viewer_tests`
executable and driven by `tests/TestMain.cpp`, which calls each suite's
`run*Tests(argc, argv)` in sequence. Registered via one `add_test` in
`CMakeLists.txt`; ctest runs them with `QT_QPA_PLATFORM=offscreen`.

- Full suite via ctest: `ctest --test-dir build --output-on-failure`
- Run the binary directly (offscreen needed for GUI tests):
  `QT_QPA_PLATFORM=offscreen ./build/openms_viewer_tests`
- To iterate on one suite, comment out the other `status |= run*Tests(...)`
  lines in `tests/TestMain.cpp` (there is no per-suite ctest target or built-in
  name filter, since every suite shares one `argv`).

Adding a test suite requires three edits: create `tests/FooTest.cpp` exposing
`int runFooTests(int, char**)`, add the file to `openms_viewer_tests` in
`CMakeLists.txt`, and wire the `run`/`status |=` calls into `tests/TestMain.cpp`.

## Architecture

Layering (see `docs/architecture.md` for the full rationale):

```
Qt shell (MainWindow, docks, actions)
  → interactive widgets (peak map, TIC, spectrum, tables, IM, imaging)
    → ViewerDocument + SelectionController + view state
      → OpenMS data structures and format readers
```

CMake splits the code into two static libraries plus a thin executable:
`openms_viewer_core` (UI-independent: model, plot rasterizers, annotation,
export logic, logging) and `openms_viewer_ui` (widgets, dialogs, `MainWindow`).
`main.cpp` links only the UI library. Keep this boundary: core must not depend
on Qt Widgets, so headless logic stays testable and reusable.

### Key invariants — read these before changing document/selection code

- **Atomic document adoption.** `ViewerDocument` owns the current
  `OpenMS::MSExperiment` and derived summaries. Loads run on a worker thread and
  build a complete `LoadResult`; the GUI thread then calls `adopt(...)` to swap
  it in atomically. A failed or cancelled load must never mutate visible state.
  The parallel pattern applies to `adoptFeatures`/`adoptIdentifications`.
- **Independent, order-independent loaders.** mzML, FeatureXML, and idXML load
  concurrently and independently. idXML↔spectrum linking is *recomputed* whenever
  either side changes (reference tolerances: 5 s RT, 0.5 Da precursor m/z), so
  command-line loads in any order converge. Every matching identification keeps
  its link; each spectrum also stores the lowest-error id as its preferred match.
- **One selection source of truth.** `SelectionController` holds the selected
  spectrum / feature / identification+hit / FAIMS channel. Mouse, keyboard,
  table, plot, and programmatic selections all go through it; `MainWindow` fans
  the resulting `*Changed` signals out to panels. Panels do **not** reach into
  one another, and table synchronization is guarded to prevent recursive
  activation. Add new cross-panel selection state here, not in widgets.
- **UI-independent core services.** `PeakMapRasterizer` and
  `IonMobilityRasterizer` are adapters over OpenMS native rasterization
  (`rasterizeRTMZ` / `rasterizeIMFrame`); widgets run them async and coalesce
  rapid interaction requests. `SpectrumAnnotation` wraps OpenMS
  `TheoreticalSpectrumGenerator`; the spectrum widget only owns presentation
  (e.g. mirror mode). Keep rasterization/annotation logic out of widgets.

### Async loading pattern

Background work uses `QtConcurrent::run` returning a typed result struct, watched
by a `QFutureWatcher<T>` member in `MainWindow` (`loadWatcher_`,
`featureLoadWatcher_`, `identificationLoadWatcher_`, `imagingLoadWatcher_`,
`mzMLExportWatcher_`). Progress and cancellation are passed as
`std::function` callbacks (`ViewerDocument::ProgressCallback` /
`CancellationCheck`). Follow this shape for new long-running operations rather
than blocking the GUI thread.

### Custom plot widgets

Plots are custom `QWidget`/`QPainter` canvases (no WebEngine, no external
plotting library). This preserves a direct upgrade path to GPU rendering without
changing document or panel APIs — do not couple plot widgets to whether spectra
are memory- or disk-backed.

The 3-D peak surface (`PeakSurface3DWidget`) already takes that path: on Qt 6.7+
it renders through `QRhiWidget` (QRhi picks Vulkan/Metal/D3D/OpenGL at runtime;
shaders live in `src/widgets/shaders/*.{vert,frag}`, compiled to `.qsb` by
`qt6_add_shaders` and embedded under `:/shaders`). On older Qt it compiles a CPU
`QPainter` painter's-algorithm fallback — same public API either way, gated by
`OPENMS_VIEWER_SURFACE3D_RHI` (see `CMakeLists.txt` `OPENMS_VIEWER_HAVE_RHI`).
Both surface backends and the 2-D peak map share
`PeakMapRasterizer::heightGrid` for the normalized intensity grid, so heights and
colours track the 2-D map (readable on high-dynamic-range data).

## Constraints

- **No embedded Python / scripting console.** Reproducible data transformation
  belongs in OpenMS command-line tools or external scripts. The viewer instead
  provides a thread-safe, searchable/saveable diagnostics log (`ApplicationLog` /
  `LogWidget`). Do not add an in-process interpreter.
- Performance direction: currently the full `MSExperiment` is held in memory and
  only the visible RT/m/z range is rasterized at screen resolution. Future
  large-data work should add on-disc/cached-mzML access *behind* the document
  interface. Imaging (imzML/IBD) already uses on-disc, lazy spectrum decoding —
  see `ImagingDocument`.

## Reference docs

- `docs/architecture.md` — GUI framework decision and layer contract
- `docs/feature-matrix.md` — parity status vs. `pyopenms-viewer`
- `docs/performance.md` — 127M-peak large-file smoke test and memory footprint
- `docs/packaging.md` — CPack generators and deployment boundaries
