# OpenMS Viewer

OpenMS Viewer is a standalone C++ desktop application intended to replace
TOPPView for interactive mass-spectrometry data inspection. It links against
OpenMS, but it is developed and released as a separate project.

The current vertical slice supports:

- asynchronous, cancellable mzML loading through `OpenMS::MzMLFile`, with a
  central phase/percent/elapsed-time card that preserves the previous run until
  the replacement is ready;
- asynchronous FeatureXML loading through `OpenMS::FeatureXMLFile`, including
  centroid, bounding-box, and convex-hull overlays;
- asynchronous idXML loading through `OpenMS::IdXMLFile`, with precursor
  markers, sequence labels, all peptide hits, metadata, and deterministic
  one-to-many MS/MS-spectrum linking with a preferred default match;
- a screen-resolution MS1 peak map rendered by
  `OpenMS::MSExperiment::rasterizeRTMZ`, with histogram-equalized (default),
  log, square-root, and linear intensity scales, seven colormaps
  (viridis/plasma/inferno/magma/jet/hot/grayscale), a Go-to-Range dialog, and a
  clickable full-run minimap/current-viewport indicator;
- wheel zoom, rectangle zoom, Alt-drag panning, Shift-drag measurement, zoom
  history, and RT/m/z axis swapping;
- a clickable MS1 TIC with an RT marker for every selected scan (including
  MS2 scans) and peak-map recentering that preserves the current RT span;
- an MS1/MS2 spectrum viewer with first/previous/next/last and level-specific
  navigation, persistent wheel/box m/z zoom, snapped peak hover, automatic m/z
  labels, and per-spectrum two-peak Δm/z measurements;
- a TIC/BPC view with click selection and drag/wheel RT zoom synchronized back
  to the peak map;
- a sortable feature table with intensity, quality, and charge filters, TSV
  export, table-to-map zoom, and bidirectional selection synchronization;
- an identification table with linked/unlinked and sequence/score filters,
  all-hit inspection, metadata details, and TSV export;
- a synchronized spectra table with MS-level, RT, identification, sequence,
  and score filters, optional cached statistics/metadata, rows for every linked
  identification and peptide hit, and TSV export;
- native OpenMS chromatogram extraction with sortable transition metadata,
  multi-trace comparison, peak-map RT-range indication, RT activation, and TSV
  export, including chromatogram-only mzML files;
- native ion-mobility frame detection and asynchronous `rasterizeIMFrame`
  rendering with frame navigation, mobilograms, zooming, and MS2 isolation
  windows;
- multi-CV FAIMS detection with per-CV TIC traces, scan navigation, and a
  compensation-voltage filter shared by the peak map and TIC, plus synchronized
  per-CV peak-map small multiples; table selection and spectrum navigation keep
  the active channel consistent;
- asynchronous filtered-mzML export by RT, m/z, MS level, and active FAIMS CV,
  plus PNG capture for every native plot and TSV export for all data tables;
- on-disc OpenMS imzML/IBD imaging with TIC and ppm-window ion images,
  pixel-to-spectrum navigation, lazy spectrum decoding, and additive multi-ion
  RGB overlays without retaining the full imaging experiment in memory;
- b/y/a fragment-ion annotation using OpenMS theoretical spectra, external
  idXML fragment annotations, configurable Da tolerance, coverage statistics,
  and an optional mirror view with unmatched theoretical ions;
- a focused welcome page with recent files, reload and close-data actions;
- visible peak-map Zoom/Pan/Measure modes, log/square-root/linear intensity
  scaling, color scale and overlay legends, persistent cursor/run/selection
  context, plot context menus, and collision-aware labels;
- compact MS-level-aware spectrum navigation with scan-number, native-ID, and
  RT lookup;
- native drag-and-drop, file dialogs, dark/light themes, keyboard shortcuts,
  accessible plot descriptions, movable/closable Qt dock panels whose user
  visibility choices survive reloads, draggable floating-panel headers with
  explicit dock/float controls and on-screen recovery, a reset-layout action, and a
  searchable/saveable diagnostics log.

Panel selection is coordinated by one shared selection controller. Mouse,
keyboard, table, plot, and programmatic selections therefore update the same
spectrum/feature/identification/hit/FAIMS state without recursive feedback.

See [docs/architecture.md](docs/architecture.md) for the GUI decision and
[docs/feature-matrix.md](docs/feature-matrix.md) for parity status against
`pyopenms-viewer`.
The [large-file validation](docs/performance.md) records a reproducible
127-million-peak smoke test and its memory footprint.
Packaging generators and deployment boundaries are described in
[docs/packaging.md](docs/packaging.md).

## Build

OpenMS Viewer currently targets OpenMS 3.6 development builds and Qt 6.4 or
newer. Point CMake at a built or installed OpenMS package:

```bash
cmake -S . -B build \
  -DOpenMS_DIR=/path/to/OpenMS-build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the application with optional mzML, FeatureXML, and idXML files:

```bash
./build/openms-viewer [sample.mzML] [features.featureXML] [ids.idXML]
```

During development against the sibling OpenMS checkout in this workspace:

```bash
cmake -S . -B build \
  -DOpenMS_DIR="$HOME/Development/OpenMS/OpenMS-build"
```

## License

BSD-3-Clause. See [LICENSE](LICENSE).
