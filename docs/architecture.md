# Architecture and GUI framework decision

## Decision

Use **Qt 6 Widgets**, custom `QWidget`/`QPainter` plot canvases, and Qt
Concurrent for background work. Link the application to the OpenMS core target,
not to TOPPView or the OpenMS GUI library.

This keeps the application a separate project while using the same C++ and Qt 6
runtime already required by current OpenMS builds. Qt supplies native file
dialogs, drag and drop, keyboard navigation, accessibility, high-DPI handling,
menus, settings, and dockable panels on Linux, macOS, and Windows. Custom plot
widgets avoid WebEngine, JavaScript bridges, and an additional plotting-library
license. They also leave a direct upgrade path from CPU `QImage` composition to
`QOpenGLWidget` without changing the document or panel APIs.

Alternatives considered:

| Framework | Assessment |
| --- | --- |
| Dear ImGui | Excellent immediate-mode rendering, but weaker native desktop integration, accessibility, text/table widgets, and docking persistence for this application. |
| wxWidgets | Native widgets and permissive licensing, but adds another GUI runtime next to the Qt already used by OpenMS and has a smaller scientific-visualization ecosystem. |
| Qt Quick/QML | Attractive for animated, touch-first interfaces, but data ownership across the QML/C++ boundary and custom scientific plotting add complexity without a clear desktop benefit. |
| Browser/WebView | Rapid UI iteration, but recreates the Python viewer's server/WebView boundary and packaging/runtime costs that the C++ replacement should remove. |

## Layers

```text
Qt application shell (MainWindow, docks, actions)
             |
Interactive widgets (peak map, TIC, spectrum, later tables/IM/image)
             |
ViewerDocument + SelectionController + view state
             |
OpenMS data structures and format readers
```

- `ViewerDocument` owns the current `OpenMS::MSExperiment` and derived summary
  data. A complete load result is built on a worker thread and adopted atomically
  on the GUI thread, so failed/cancelled loads cannot corrupt the visible data.
- FeatureXML is loaded independently into an OpenMS `FeatureMap` and normalized
  into immutable overlay records (centroid, hull geometry, bounds, and table
  metadata). This permits mzML and overlays to load concurrently while keeping
  widgets independent of OpenMS file-reader details.
- idXML is likewise loaded independently and retains every peptide hit, external
  peak annotation, and meta value. Linking is recomputed whenever either side
  changes using the reference tolerances (5 s RT, 0.5 Da precursor m/z), so
  concurrent command-line loads are order-independent. Every matching
  identification retains its spectrum link; each spectrum additionally stores
  the lowest-error identification as its preferred default.
- Spectrum annotation is a UI-independent core service over OpenMS
  `TheoreticalSpectrumGenerator`. The spectrum widget receives a completed
  annotation record and only owns presentation choices such as mirror mode.
- `PeakMapRasterizer` is a UI-independent adapter around OpenMS native
  rasterization. The peak-map widget runs it asynchronously and coalesces rapid
  interaction requests. Its RT raster resolution is capped at the number of
  visible, distinct MS1 scan positions before being scaled to the canvas; sparse
  acquisitions therefore do not show artificial black rows between scans.
- Each visualization is a widget with explicit input and selection signals.
  `SelectionController` is the single source of truth for the selected spectrum,
  feature, identification/hit, and FAIMS channel. The main window applies those
  transitions to panels, while guarded table synchronization prevents recursive
  activation; panels do not reach into one another.
- Qt docks provide panel show/hide, reordering, and layout persistence without a
  custom panel framework.
- The C++ application does not embed a Python interpreter. Reproducible data
  transformation belongs in OpenMS command-line tools or external Python
  scripts; the viewer provides a thread-safe, searchable/saveable diagnostics
  log instead of an in-process arbitrary-code console. This avoids a second ABI,
  environment, and packaging surface in the desktop executable.

## Performance direction

The first implementation keeps the `MSExperiment` in memory and rasterizes only
the visible RT/m/z range at screen resolution. OpenMS performs RT/m/z range
searches and parallel intensity aggregation in native code. Subsequent large-data
work should add `OnDiscMSExperiment`/cached-mzML-backed access behind the document
interface and retain a bounded tile cache. Plot widgets should never depend on
whether spectra are memory- or disk-backed.
