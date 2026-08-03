# Changelog

All notable changes to OpenMS Viewer are documented in this file.

## [0.2.0] — 2026-08-03

First public release of OpenMS Viewer — a standalone C++/Qt 6 desktop application
for interactive mass-spectrometry data inspection, intended to replace TOPPView.

### Highlights

- **Full-resolution 2-D peak map** with native OpenMS rasterization, seven
  colormaps, histogram-equalized/log/sqrt/linear intensity scales, wheel/box
  zoom, pan, measurement (with snap-to-peak), axis swap, zoom history, go-to-range
  dialog, and a cached clickable minimap.
- **Asynchronous, atomic file loading.** mzML, featureXML, idXML, consensusXML,
  OpenSWATH `.osw`, imzML, Parquet bundles, mzIdentML, mzXML, sqMass, Thermo
  `.raw`, and Bruker timsTOF `.d` — each on a worker thread with progress,
  cancellation, and atomic adoption into the view.
- **One selection, everywhere.** A single `SelectionController` keeps peak map,
  spectrum, tables, TIC, and detail panels in lock-step.
- **MS/MS annotation** with OpenMS `TheoreticalSpectrumGenerator` (b/y/a ions),
  idXML annotation priority, mirror view, and coverage display.
- **Feature & identification overlays** with centroids, convex hulls, boxes,
  diamond/sequence precursor markers, and synchronized sortable/filterable tables.
- **Chromatogram panel** with native OpenMS extraction, multi-selection transition
  plot, Savitzky–Golay smoothing overlay, and TIC/type/Q1/Q3 metadata.
- **Ion mobility** support: frame navigation, asynchronous IM rasterization,
  mobilogram, MS2 isolation-window overlay, and diaPASEF window-group visualization.
- **FAIMS** support: per-CV TIC comparison, channel-constrained navigation,
  synchronized peak-map filtering, and async per-CV small multiples.
- **imzML imaging** with on-disc lazy decoding, TIC/m·z±ppm ion images,
  log-spaced ppm mean/skyline aggregation, multi-ion RGB overlays, pixel↔spectrum
  selection, colorbar, and PNG export.
- **ConsensusXML / consensusparquet** quantification view with per-map chart and
  source-scan drill-down.
- **OpenSWATH `.osw`** peak-group view with lazy `.xic`/`.sqMass` chromatogram
  loading and standalone `.xic` file support.
- **3-D peak-surface view** of the zoomed RT × m/z region.
- **Pinned m/z annotation and hover peak highlight** on the peak map.
- **Data export:** filtered mzML (RT/m·z/MS-level/FAIMS), TSV for all tables,
  PNG for all plots.
- **Native write-back:** save features, identifications, and consensus maps.
- **Rich Help dialog** and session-persistent display UX.
- **Dark/light theme** with theme-aware plots and light-theme plot exports.
- **Configurable row-stack layout** replacing QDockWidget docking.
- **Cross-platform portable packages** (Linux, macOS, Windows) built in CI with
  full runtime dependency auditing and a release workflow that publishes draft
  GitHub Releases on `v*` tags.

### Supported formats

| Format | Mode |
| --- | --- |
| mzML / mzXML / mzData | Full in-memory load |
| featureXML / featureparquet | Async load with overlays |
| idXML / mzIdentML / idparquet | Async load with spectrum linking |
| consensusXML / consensusparquet | Quantification view |
| OpenSWATH `.osw` (+`.xic`/`.sqMass`) | Peak-group view |
| imzML / IBD | On-disc imaging |
| Thermo `.raw` | Via OpenMS reader |
| Bruker timsTOF `.d` / `.tdf` | Via OpenMS reader |
| Parquet bundles | Feature/ID/consensus |

### Build requirements

- OpenMS 3.6 (development branch)
- Qt 6.4+
- C++23 compiler
- CMake 3.24+
