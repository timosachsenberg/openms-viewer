#pragma once

#include "plot/PlotRange.h"

#include <OpenMS/KERNEL/MSExperiment.h>

#include <QImage>
#include <QSize>

#include <cstddef>
#include <vector>

namespace OpenMSViewer
{
  enum class PeakMapColorMap
  {
    Viridis,
    Plasma,
    Inferno,
    Magma,
    Jet,
    Hot,
    Grayscale
  };

  enum class PeakMapIntensityScale
  {
    Equalized,      // histogram/rank equalization (datashader eq_hist); default
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
                                       PeakMapIntensityScale intensityScale = PeakMapIntensityScale::Equalized);
    [[nodiscard]] static QRgb color(double normalized, PeakMapColorMap colorMap);

    // Normalized [0,1] intensity grid in native values[mzIndex * rtBins + rtIndex]
    // layout, applying the SAME exact-splat-vs-raster switch, dynspread and intensity
    // scaling as render(). The 3-D surface uses this so its heights and colours track
    // the 2-D peak map exactly (readable on high-dynamic-range data) and inherit the
    // fast-raster-when-zoomed-out / exact-points-when-zoomed-in behaviour for free.
    [[nodiscard]] static std::vector<float> heightGrid(
      const OpenMS::MSExperiment& experiment, const PlotRange& range,
      std::size_t rtBins, std::size_t mzBins, unsigned int msLevel = 1,
      PeakMapIntensityScale intensityScale = PeakMapIntensityScale::Equalized);
  };
}
