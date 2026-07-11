# Feature parity matrix

This matrix is derived from the checked-out `~/Development/pyopenms-viewer`
implementation, especially `FEATURES.md`, `core/config.py`, its panel modules,
and its tests. It is the working scope for TOPPView replacement parity.

| Area | Reference behavior | C++ status |
| --- | --- | --- |
| mzML loading | CLI, file picker, drag/drop; large-file progress | **Complete:** CLI, picker, drop, worker-thread atomic load, native spectra/chromatogram percent progress, elapsed time, cancellation, and previous-run preservation |
| 2D peak map | Native aggregation, colormaps, zoom/pan/measure, history, axis swap, exact ranges, minimap | **Complete:** native OpenMS density-aware raster, histogram-equalized (default)/log/sqrt/linear scales, seven real-LUT colormaps (viridis/plasma/inferno/magma/jet/hot/grayscale) with a colormap-floor background, graduated dynspread, a reprojected preview that tracks pan/zoom, wheel/box zoom, pan, measurement, history, axis swap, exact ranges, Go-to-Range (G) dialog, MS-level-coloured selected-spectrum marker with precursor crosshair, and cached clickable full-run minimap and viewport indicator |
| Spectrum view | MS-level navigation, relative/absolute intensity, labels, measurement, precursor marker | **Complete:** level-specific/edge navigation, scalable stick rendering, relative/absolute intensity, persistent wheel/box m/z zoom, snapped hover labels, automatic prominent-peak labels, stored Δm/z brackets, precursor marker, IM m/z linking |
| TIC/BPC | Click selection, view/selection markers, TIC/BPC selection | **Complete:** MS1 TIC/MSn BPC, click selection with peak-map recentering, RT-based markers that remain visible for MS2 scans, and drag/wheel RT zoom synchronized to the peak map |
| FeatureXML | Centroids, boxes, hulls, selection and filtered/exportable table | **Complete:** asynchronous OpenMS load, hover/click selection, all overlay modes, synchronized sortable/filterable table, table zoom, TSV export |
| idXML | Precursor overlay, sequence labels, spectrum linking, metadata and hits | **Complete:** asynchronous OpenMS load, diamond/sequence overlays, one-to-many 5 s/0.5 Da spectrum linking with preferred defaults, all-hit table, filters, metadata details, TSV export, synchronized selection |
| MS/MS annotation | OpenMS theoretical ions, external annotations, tolerance, mirror view and coverage | **Complete:** b/y/a generation, idXML annotation priority, ion coloring/labels, Da tolerance, coverage, precursor marker, mirror/unmatched view |
| Spectra table | Filtering, optional metadata/stat columns, TSV export | **Complete:** cached per-scan summaries, MS2/identified/RT/sequence/score filters, every linked identification/hit in all-hit mode, optional statistics and metadata, mouse/keyboard synchronized selection, TSV export |
| Chromatograms | Chromatogram panels and linked interaction | **Complete:** native OpenMS extraction including chromatogram-only mzML, sortable transition summary, multi-selection plot, TIC/type/Q1/Q3 metadata, peak-map RT range, plot-to-spectrum RT activation, seconds/minutes display, TSV export |
| Ion mobility | Frame selection, m/z-IM map, mobilogram, MS2 isolation window | **Complete:** native frame/unit detection, synchronized MS1/MS2 frame navigation, asynchronous `rasterizeIMFrame`, rectangle/wheel zoom, mobilogram, and MS2 isolation-window overlay |
| FAIMS | CV detection, per-CV TIC and peak maps | **Complete:** native CV detection, per-CV TIC comparison and scan activation, channel-constrained navigation, automatic channel switching on spectrum selection, synchronized main peak-map/TIC filtering, asynchronous per-CV peak-map small multiples, and small-map filter activation |
| imzML imaging | Ion image lifecycle and spatial interaction | **Complete:** OpenMS on-disc imzML/IBD loading, geometry and physical-pixel metadata, TIC/ppm-window ion images, lazy pixel-spectrum decoding, bidirectional pixel selection, additive multi-ion RGB overlays, PNG export |
| Data export | Filtered mzML, table TSV, plot PNG | **Complete:** asynchronous RT/m/z/MS-level/active-FAIMS mzML filtering with aligned arrays preserved, TSV export for feature/ID/spectra/chromatogram tables, PNG capture for all native plots |
| Scripting/log | Python snippets and algorithm log panels | **Complete (adapted):** searchable/severity-filtered/saveable thread-safe diagnostics dock; embedded Python is intentionally excluded, with transformations delegated to OpenMS CLI or external scripts |
| UI shell | Theme, fullscreen, configurable/reorderable panels, shortcuts | **Complete:** focused welcome/recent-file flow, compact contextual toolbars, dark/light native theme, fullscreen, persistent movable/tabbed/closable docks with explicit float/dock buttons, compositor-backed floating-window dragging and on-screen recovery, reset layout, persistent run/selection/cursor context, accessible plot names, context menus, and keyboard interaction shortcuts |
| Scale | 50M+ peaks, interaction throttling, out-of-core/cache options | **Complete (adapted):** native view rasterization, 45 ms request coalescing, stale-request rejection, cached minimap, and no duplicate tabular peak store; the reference 127.1M-peak mzML loaded in 11.2 s at 2.58 GiB peak RSS; imzML remains on-disc |
| Packaging | Linux, macOS, Windows installers | **Complete (baseline):** install rules plus CPack TGZ/DEB, DMG, and NSIS/ZIP generators; platform signing/notarization credentials remain external release infrastructure |

“Adapted” marks a deliberate native-desktop equivalent rather than a literal
copy of the web/Python implementation. Complete items have focused automated
coverage and a runtime check using representative OpenMS data.
