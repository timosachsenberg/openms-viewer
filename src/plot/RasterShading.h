#pragma once

#include "plot/PeakMapRasterizer.h"

#include <QRgb>

#include <array>
#include <cstddef>
#include <vector>

namespace OpenMSViewer::RasterShading
{
  // Sample a colormap at `normalized` in [0,1] through a cached 256-entry LUT
  // (real perceptual ramps for viridis/plasma/inferno/magma, plus jet/hot/grey).
  // Non-finite input is treated as the floor (0.0).
  [[nodiscard]] QRgb sample(PeakMapColorMap colorMap, double normalized);

  // Histogram-equalization CDF over occupied cells (log1p intensity space).
  // Built once from the ORIGINAL grid so a value's colour depends only on the
  // data distribution, never on spread-blob geometry or the peak's position.
  inline constexpr int kEqualizationBins = 1024;
  struct EqualizationCdf
  {
    double invLogMax{0.0};   // 0 => degenerate (no occupied cells); rank() returns 0
    std::array<float, kEqualizationBins + 1> cdf{};
    [[nodiscard]] double rank(float value) const;   // -> normalized rank in [0,1]
  };
  [[nodiscard]] EqualizationCdf buildEqualization(const std::vector<float>& values);

  // Occupancy-driven spread radius (0 = leave dense views untouched), shared by
  // the peak-map and ion-mobility rasterizers so sparse views stay visible.
  [[nodiscard]] int dynspreadRadius(double occupancy);

  // In-place max-dilation of every occupied cell into a (2r+1)² neighbourhood.
  // Layout is row-major `values[a * dimB + b]`, which fits both the RT/m-z grid
  // (a = m/z, b = RT) and the m-z/mobility grid (a = m/z, b = mobility).
  void dilateMax(std::vector<float>& values, std::size_t dimA, std::size_t dimB, int radius);
}
