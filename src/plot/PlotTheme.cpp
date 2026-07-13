#include "plot/PlotTheme.h"

namespace OpenMSViewer::PlotTheme
{
  QColor primaryTrace(const QPalette& palette)
  {
    return isDark(palette) ? QColor(35, 190, 225) : QColor(15, 110, 150);
  }

  QColor rangeHighlight(const QPalette& palette)
  {
    if (isDark(palette)) return QColor(255, 210, 40, 30);
    QColor tint = palette.color(QPalette::Highlight);
    tint.setAlpha(40);
    return tint;
  }
}
