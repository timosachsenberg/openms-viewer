#pragma once

#include <QColor>
#include <QPalette>

// Small, stateless helper for theme-aware plot colours. Widgets pass their live
// palette() (which Qt keeps current across light/dark switches) and get back
// semantic colours, so accent colours stay consistent across every custom plot
// and legible on both a light and a dark canvas. Chrome (background, axis, text,
// grid) should be read from the palette directly (Window/Base/Mid/Text); this
// helper only covers the accents that need a hand-picked variant per theme.
namespace OpenMSViewer::PlotTheme
{
  /// True when the widget palette is a dark theme (dark window background).
  [[nodiscard]] inline bool isDark(const QPalette& palette)
  {
    return palette.color(QPalette::Window).lightnessF() < 0.5;
  }

  /// The canonical single-signal trace colour (mobilogram, TIC, aggregate
  /// spectrum): a bright cyan on dark, a deep teal on light.
  [[nodiscard]] QColor primaryTrace(const QPalette& palette);

  /// A subtle range/selection wash for line plots (e.g. the peak-map RT range on
  /// a chromatogram): warm and faint on dark, a light-blue selection tint on
  /// light so it never reads as an opaque cream block.
  [[nodiscard]] QColor rangeHighlight(const QPalette& palette);
}
