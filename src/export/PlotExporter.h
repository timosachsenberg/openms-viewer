#pragma once

#include <QString>

class QWidget;

namespace OpenMSViewer
{
  class PlotExporter
  {
  public:
    // Raster capture of the widget's current appearance.
    [[nodiscard]] static QString writePng(QWidget& widget, const QString& path);
    // Vector capture: re-runs the widget's painting into an SVG canvas, so axes,
    // gridlines, peak sticks, feature overlays and labels stay scalable. A 2D
    // density raster (peak map / ion image) embeds as an image, as expected.
    // Both return an empty string on success, or a human-readable error.
    [[nodiscard]] static QString writeSvg(QWidget& widget, const QString& path);
  };
}
