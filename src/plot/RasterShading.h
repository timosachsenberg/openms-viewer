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

  // Datashader-compatible dynspread radius. For candidate radii 1..maxRadius,
  // measure the fraction of occupied pixels that have another occupied pixel
  // within twice that radius. Stop just before the fraction exceeds `threshold`.
  [[nodiscard]] int dynspreadRadius(const std::vector<float>& values,
                                    std::size_t dimA, std::size_t dimB,
                                    double threshold = 0.5, int maxRadius = 4);

  // In-place max-dilation of every occupied cell through Datashader's default
  // circular mask (distance <= radius + 0.5), rather than a blocky square.
  // Layout is row-major `values[a * dimB + b]`, which fits both the RT/m-z grid
  // (a = m/z, b = RT) and the m-z/mobility grid (a = m/z, b = mobility).
  void dilateMaxCircular(std::vector<float>& values, std::size_t dimA,
                         std::size_t dimB, int radius);
}
