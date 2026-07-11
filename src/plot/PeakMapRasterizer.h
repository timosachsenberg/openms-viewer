#pragma once

#include "plot/PlotRange.h"

#include <OpenMS/KERNEL/MSExperiment.h>

#include <QImage>
#include <QSize>

namespace OpenMSViewer
{
  enum class PeakMapColorMap
  {
    Viridis,
    Plasma,
    Magma,
    Grayscale
  };

  enum class PeakMapIntensityScale
  {
    Logarithmic,
    SquareRoot,
    Linear
  };

  class PeakMapRasterizer
  {
  public:
    [[nodiscard]] static QImage render(const OpenMS::MSExperiment& experiment,
                                       const PlotRange& range,
                                       QSize size,
                                       bool axesSwapped = true,
                                       unsigned int msLevel = 1,
                                       PeakMapColorMap colorMap = PeakMapColorMap::Viridis,
                                       PeakMapIntensityScale intensityScale = PeakMapIntensityScale::Logarithmic);
    [[nodiscard]] static QRgb color(double normalized, PeakMapColorMap colorMap);
  };
}
