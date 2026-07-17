---
status: accepted
---

# Row-stack layout instead of QDockWidget

The viewer arranges its panels in a **Layout**: a vertical stack of **Rows**, each holding
one or two **Panels**, with user-draggable dividers (see `CONTEXT.md`). We implement this
ourselves — a pure model in `openms_viewer_core` plus a `QSplitter`-based renderer in
`openms_viewer_ui` — instead of using `QMainWindow` + `QDockWidget`, because Qt docking
cannot express "at most two panels per row": it allows arbitrary nesting in four dock
areas and offers no hook to constrain where a user drags a panel. We wanted the
constraint more than we wanted the free functionality.

## Considered options

- **Keep `QDockWidget` and fix the drag.** Deleting `FloatingDockTitleBar` would restore
  Qt's native drag-to-dock in a few lines. Rejected: it leaves the layout unconstrained,
  so users can still reach the deeply-nested, half-width, full-height-column states that
  motivated this work.
- **A third-party dock library** (Qt-Advanced-Docking-System, KDDockWidgets). Better
  drag-and-drop UX than stock Qt, but neither enforces a per-row panel limit either, so
  we would still be layering our constraint onto a system that does not want it — while
  adding a dependency.

## Consequences

- **No floating panels, no tear-off.** A panel is in the Layout or hidden; there is no
  third state. This is the deliberate cost: second-monitor workflows are not supported.
  In exchange, drag is plain child-widget mouse handling, so the WSLg mouse-grab bug
  (microsoft/wslg#1153) that forced the `restrictDockFloating()` platform gate and the
  custom title bar simply does not arise. One drag behaviour on every platform.
- **No tabbing.** Panels never stack behind one another. The default Layout must
  therefore be opinionated — it shows a curated few panels and hides the rest, which the
  user opens on demand. The old `arrangeDocksDefault()` tabbed eleven panels into one row
  because it had nowhere else to put them; that dumping ground is gone. Concretely: the
  peak map (with Data & layers beside it), the TIC and the spectrum, plus the plot panels
  that *are* the view for their input (imaging, OSW, consensus, ion mobility, FAIMS,
  chromatograms) and are gated on data most files lack. The result tables are opt-in even
  when their data is loaded — a change from the tabbed default, where they cost nothing.
- **The Layout is the user's intent, not the current document's shape.** `MainWindow`
  keeps the arranged Layout and shows it *filtered* by availability. Persisting only what
  is on screen would forget a two-panel row on every launch that starts before its data
  is loaded, and re-add the panel at the bottom when it arrived — losing exactly the
  arrangement this feature exists to let users build.
- **The layout can overflow and scroll.** Panels keep their minimum heights rather than
  compressing past legibility, so the Layout scrolls vertically when the rows do not fit.
- **Panel minimum widths are re-derived from the widgets.** The old per-dock
  `setMinimumWidth(380..720)` calls were props for the tabbed full-width row, not real
  requirements — kept, they would make some two-panel rows impossible on a 1366px screen.
  Minimums now come from each widget's own `minimumSizeHint()`.
- **Saved layouts reset once.** Old `main/state` blobs are opaque `QMainWindow` dock state
  and carry no information recoverable into a Row/Panel tree. They are discarded under a
  new versioned settings key.
- **The invariant is testable.** Because the model is a plain value tree in core with no
  Qt Widgets dependency, "no row has three panels" and the drop-target rules are headless
  unit tests. Qt's `saveState()` `QByteArray` offered nothing to assert against.
