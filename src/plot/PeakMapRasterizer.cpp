#include "plot/PeakMapRasterizer.h"

#include <QColor>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace OpenMSViewer
{
  namespace
  {
    struct ColorStop
    {
      double position;
      int red;
      int green;
      int blue;
    };

    QRgb viridis(double value)
    {
      static constexpr std::array<ColorStop, 6> stops{{
        {0.00, 68, 1, 84},
        {0.20, 59, 82, 139},
        {0.40, 33, 145, 140},
        {0.60, 94, 201, 98},
        {0.80, 170, 220, 50},
        {1.00, 253, 231, 37},
      }};

      const double clamped = std::clamp(value, 0.0, 1.0);
      auto upper = std::upper_bound(stops.begin(), stops.end(), clamped,
        [](double needle, const ColorStop& stop) { return needle < stop.position; });
      if (upper == stops.begin()) return qRgb(upper->red, upper->green, upper->blue);
      if (upper == stops.end())
      {
        const auto& stop = stops.back();
        return qRgb(stop.red, stop.green, stop.blue);
      }
      const auto& high = *upper;
      const auto& low = *(upper - 1);
      const double fraction = (clamped - low.position) / (high.position - low.position);
      const auto blend = [fraction](int from, int to)
      {
        return static_cast<int>(std::lround(from + fraction * (to - from)));
      };
      return qRgb(blend(low.red, high.red), blend(low.green, high.green),
                  blend(low.blue, high.blue));
    }

    QRgb colorFor(double value, PeakMapColorMap colorMap)
    {
      const double clamped = std::clamp(value, 0.0, 1.0);
      if (colorMap == PeakMapColorMap::Grayscale)
      {
        const int gray = static_cast<int>(std::lround(clamped * 255.0));
        return qRgb(gray, gray, gray);
      }
      if (colorMap == PeakMapColorMap::Plasma)
      {
        return qRgb(static_cast<int>(255.0 * std::clamp(0.35 + 1.1 * clamped, 0.0, 1.0)),
                    static_cast<int>(255.0 * std::pow(clamped, 1.7)),
                    static_cast<int>(255.0 * std::clamp(0.8 - 0.7 * clamped, 0.0, 1.0)));
      }
      if (colorMap == PeakMapColorMap::Magma)
      {
        return qRgb(static_cast<int>(255.0 * std::clamp(1.5 * clamped, 0.0, 1.0)),
                    static_cast<int>(255.0 * std::pow(clamped, 2.2)),
                    static_cast<int>(255.0 * std::clamp(0.18 + 0.55 * clamped, 0.0, 1.0)));
      }
      return viridis(clamped);
    }

    // Deep-zoom thresholds mirror pyopenms-viewer: below BOTH spans, switch from
    // native RT/m-z rasterization to exact per-peak point rendering.
    constexpr double kDeepZoomRtThreshold = 60.0;   // seconds
    constexpr double kDeepZoomMzThreshold = 50.0;   // m/z

    // Exact rendering (datashader canvas.points() equivalent): place every peak
    // in view at its precise (rt, m/z) pixel, keeping the max intensity per cell.
    void splatPeaks(const OpenMS::MSExperiment& experiment, const PlotRange& range,
                    std::size_t rtBins, std::size_t mzBins, unsigned int msLevel,
                    std::vector<float>& values)
    {
      const double rtSpan = range.rtMax - range.rtMin;
      const double mzSpan = range.mzMax - range.mzMin;
      if (rtSpan <= 0.0 || mzSpan <= 0.0 || rtBins == 0 || mzBins == 0) return;
      const double rtScale = static_cast<double>(rtBins - 1) / rtSpan;
      const double mzScale = static_cast<double>(mzBins - 1) / mzSpan;
      const auto lastRt = static_cast<long long>(rtBins - 1);
      const auto lastMz = static_cast<long long>(mzBins - 1);
      for (const OpenMS::MSSpectrum& spectrum : experiment)
      {
        if (spectrum.getMSLevel() != msLevel) continue;
        const double rt = spectrum.getRT();
        if (rt < range.rtMin || rt > range.rtMax) continue;
        const auto rtPixel = static_cast<std::size_t>(
          std::clamp(std::llround((rt - range.rtMin) * rtScale), 0LL, lastRt));
        for (auto it = spectrum.MZBegin(range.mzMin); it != spectrum.MZEnd(range.mzMax); ++it)
        {
          const auto mzPixel = static_cast<std::size_t>(
            std::clamp(std::llround((it->getMZ() - range.mzMin) * mzScale), 0LL, lastMz));
          float& cell = values[mzPixel * rtBins + rtPixel];
          cell = std::max(cell, static_cast<float>(it->getIntensity()));
        }
      }
    }

    // Shared shading tail: adaptive spread + colormap, used by both render paths.
    QImage shadeGrid(std::vector<float>& values, std::size_t rtBins, std::size_t mzBins,
                     QSize size, bool axesSwapped, PeakMapColorMap colorMap,
                     PeakMapIntensityScale intensityScale)
    {
      float maximum = 0.0F;
      std::size_t occupied = 0;
      for (const float value : values)
      {
        maximum = std::max(maximum, value);
        if (value > 0.0F) ++occupied;
      }

      // Adaptive spread (datashader dynspread equivalent): grow each occupied cell
      // into a small blob when the view is sparse so individual peaks stay visible
      // as crisp points; dense (zoomed-out) views are left untouched.
      const double occupancy = values.empty()
        ? 0.0 : static_cast<double>(occupied) / static_cast<double>(values.size());
      if (maximum > 0.0F && occupied > 0 && occupancy < 0.03)
      {
        constexpr int radius = 2;
        std::vector<float> spread(values);
        for (std::size_t mz = 0; mz < mzBins; ++mz)
          for (std::size_t rt = 0; rt < rtBins; ++rt)
          {
            const float source = values[mz * rtBins + rt];
            if (source <= 0.0F) continue;
            for (int dy = -radius; dy <= radius; ++dy)
            {
              const long ny = static_cast<long>(mz) + dy;
              if (ny < 0 || ny >= static_cast<long>(mzBins)) continue;
              for (int dx = -radius; dx <= radius; ++dx)
              {
                const long nx = static_cast<long>(rt) + dx;
                if (nx < 0 || nx >= static_cast<long>(rtBins)) continue;
                float& target = spread[static_cast<std::size_t>(ny) * rtBins
                                       + static_cast<std::size_t>(nx)];
                target = std::max(target, source);
              }
            }
          }
        values.swap(spread);
      }

      QImage image(size, QImage::Format_RGB32);
      image.fill(QColor(10, 10, 16));
      if (maximum <= 0.0F) return image;

      const double logMaximum = std::log1p(static_cast<double>(maximum));
      for (std::size_t mzIndex = 0; mzIndex < mzBins; ++mzIndex)
        for (std::size_t rtIndex = 0; rtIndex < rtBins; ++rtIndex)
        {
          const float intensity = values[mzIndex * rtBins + rtIndex];
          if (intensity <= 0.0F) continue;
          double normalized = static_cast<double>(intensity) / maximum;
          if (intensityScale == PeakMapIntensityScale::Logarithmic)
            normalized = std::pow(std::log1p(static_cast<double>(intensity)) / logMaximum, 0.72);
          else if (intensityScale == PeakMapIntensityScale::SquareRoot)
            normalized = std::sqrt(normalized);
          const QRgb color = colorFor(normalized, colorMap);
          const int x = axesSwapped ? static_cast<int>(mzIndex) : static_cast<int>(rtIndex);
          const int y = axesSwapped
                          ? size.height() - 1 - static_cast<int>(rtIndex)
                          : size.height() - 1 - static_cast<int>(mzIndex);
          image.setPixel(x, y, color);
        }
      return image;
    }
  }

  QImage PeakMapRasterizer::render(const OpenMS::MSExperiment& experiment,
                                   const PlotRange& range,
                                   QSize size,
                                   bool axesSwapped,
                                   unsigned int msLevel,
                                   PeakMapColorMap colorMap,
                                   PeakMapIntensityScale intensityScale)
  {
    if (!range.isValid() || size.width() <= 0 || size.height() <= 0)
    {
      return {};
    }

    const std::size_t rtBins = static_cast<std::size_t>(axesSwapped ? size.height() : size.width());
    const std::size_t mzBins = static_cast<std::size_t>(axesSwapped ? size.width() : size.height());
    std::vector<float> values(rtBins * mzBins, 0.0F);

    // Deep zoom (both spans below threshold) -> exact per-peak point rendering;
    // wider views -> fast native RT/m-z aggregation. Mirrors pyopenms-viewer.
    const bool exactPoints = range.rtSpan() < kDeepZoomRtThreshold
                          && range.mzSpan() < kDeepZoomMzThreshold;
    if (exactPoints)
      splatPeaks(experiment, range, rtBins, mzBins, msLevel, values);
    else
      experiment.rasterizeRTMZ(values.data(), rtBins, mzBins,
                               range.rtMin, range.rtMax, range.mzMin, range.mzMax,
                               msLevel, OpenMS::MSExperiment::RasterAggregation::SUM);

    return shadeGrid(values, rtBins, mzBins, size, axesSwapped, colorMap, intensityScale);
  }

  QRgb PeakMapRasterizer::color(double normalized, PeakMapColorMap colorMap)
  {
    return colorFor(normalized, colorMap);
  }
}
