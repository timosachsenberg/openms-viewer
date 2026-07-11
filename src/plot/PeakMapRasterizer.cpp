#include "plot/PeakMapRasterizer.h"

#include "plot/RasterShading.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace OpenMSViewer
{
  namespace
  {
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

    // Shared shading tail: adaptive spread + intensity mapping + colormap.
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

      const double occupancy = values.empty()
        ? 0.0 : static_cast<double>(occupied) / static_cast<double>(values.size());

      QImage image(size, QImage::Format_RGB32);
      // Background is the colormap floor so "no data" blends into "lowest data".
      image.fill(RasterShading::sample(colorMap, 0.0));
      if (maximum <= 0.0F) return image;

      // Histogram equalization (datashader eq_hist): build the CDF from the
      // ORIGINAL grid so a value's colour depends only on the data distribution,
      // not on where the peak sits or how its spread blob is clipped.
      RasterShading::EqualizationCdf equalization;
      if (intensityScale == PeakMapIntensityScale::Equalized)
        equalization = RasterShading::buildEqualization(values);

      // Adaptive spread (dynspread) for display only: grow occupied cells into
      // small blobs when the view is sparse; dense views are left untouched.
      // Max-dilation reuses original values, so the CDF above still applies.
      if (occupied > 0)
        RasterShading::dilateMax(values, mzBins, rtBins, RasterShading::dynspreadRadius(occupancy));

      const double logMaximum = std::log1p(static_cast<double>(maximum));
      for (std::size_t mzIndex = 0; mzIndex < mzBins; ++mzIndex)
        for (std::size_t rtIndex = 0; rtIndex < rtBins; ++rtIndex)
        {
          const float intensity = values[mzIndex * rtBins + rtIndex];
          if (!(intensity > 0.0F)) continue;   // skips empty and non-finite cells
          double normalized = static_cast<double>(intensity) / maximum;
          switch (intensityScale)
          {
            case PeakMapIntensityScale::Equalized:
              normalized = equalization.rank(intensity);
              break;
            case PeakMapIntensityScale::Logarithmic:
              normalized = std::pow(std::log1p(static_cast<double>(intensity)) / logMaximum, 0.72);
              break;
            case PeakMapIntensityScale::SquareRoot:
              normalized = std::sqrt(normalized);
              break;
            case PeakMapIntensityScale::Linear:
              break;
          }
          // Keep any occupied cell at least one LUT step above the floor so a lone
          // faint peak never disappears into the colormap-floor background.
          normalized = std::max(normalized, 1.0 / 255.0);
          const QRgb color = RasterShading::sample(colorMap, normalized);
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
    return RasterShading::sample(colorMap, normalized);
  }
}
