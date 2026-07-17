# Context

Glossary for OpenMS Viewer. Terms only — no implementation detail, no decisions.
Architectural decisions live in `docs/adr/`; design rationale in `docs/architecture.md`.

## Panel layout

**Panel**
: One view of the document — peak map, TIC, spectrum, chromatograms, ion mobility,
imaging, OSW, consensus, FAIMS, the feature/identification/spectrum tables, the
metadata browser, the data layers list, or the log. All panels are equal citizens:
none is privileged, none is a container for the others. A panel is either placed in
the **Layout** or hidden; it is never both.

**Row**
: A horizontal band of the **Layout** holding **one or two Panels**, never more.
A one-panel row spans the full width. A two-panel row places its panels side by
side, separated by a divider the user can drag. "Docking left/right" is scoped to a
single row — it never produces a column spanning multiple rows.

**Layout**
: The ordered vertical stack of **Rows**, top to bottom. The Layout is the entire
document-viewing surface. Its shape is exactly two levels deep: a Layout has Rows,
a Row has Panels. There is no third level — a Panel never contains another Panel or
another Row.
: Rows are never created or destroyed directly; they exist only because Panels are in
them. A Row that loses one of its two Panels becomes a full-width one-Panel Row; a Row
that loses its last Panel ceases to exist. A Row is never empty.
: When the Rows' combined minimum heights exceed the window, the Layout scrolls
vertically. Panels keep the height they need rather than compressing past legibility.

**Hidden panel**
: A Panel the user has closed, or has not opened. Hidden is the resting state for
most Panels: the Layout shows a curated few by default, and the rest are opened on
demand, each arriving as a new Row at the bottom.

**Available panel**
: A Panel whose underlying data exists in the current document (e.g. the imaging
panel is unavailable until an imzML is loaded). Availability is a property of the
document; hiding is a property of the user's intent. A Panel is shown only when it
is both available and not hidden — neither alone decides.
: The Layout outlives availability. It keeps a place for a Panel whose data is not
loaded right now, so a Panel that returns when its data arrives returns to where the
user put it rather than to the bottom. Only what the user does — moving, opening or
closing a Panel — changes the Layout; a document coming and going does not.

**Preset**
: A named Layout for a task — Overview, Identification, Imaging, DIA. Applying a
Preset replaces the Layout wholesale: it decides which Panels are shown and how
their Rows are ordered.
