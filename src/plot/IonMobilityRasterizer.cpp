#include "plot/IonMobilityRasterizer.h"

#include "plot/RasterShading.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace OpenMSViewer
{
  bool IonMobilityRange::isValid() const noexcept
  {
    return mzMax > mzMin && mobilityMax > mobilityMin;
  }

  double IonMobilityRange::mzSpan() const noexcept { return mzMax - mzMin; }
  double IonMobilityRange::mobilitySpan() const noexcept { return mobilityMax - mobilityMin; }

  IonMobilityRange IonMobilityRange::normalized() const noexcept
  {
    IonMobilityRange result = *this;
    if (result.mzMin > result.mzMax) std::swap(result.mzMin, result.mzMax);
    if (result.mobilityMin > result.mobilityMax)
      std::swap(result.mobilityMin, result.mobilityMax);
    return result;
  }

  IonMobilityRange IonMobilityRange::clampedTo(const IonMobilityRange& rawBounds) const noexcept
  {
    const IonMobilityRange bounds = rawBounds.normalized();
    IonMobilityRange result = normalized();
    result.mzMin = std::clamp(result.mzMin, bounds.mzMin, bounds.mzMax);
    result.mzMax = std::clamp(result.mzMax, bounds.mzMin, bounds.mzMax);
    result.mobilityMin = std::clamp(result.mobilityMin, bounds.mobilityMin, bounds.mobilityMax);
    result.mobilityMax = std::clamp(result.mobilityMax, bounds.mobilityMin, bounds.mobilityMax);
    return result;
  }

  IonMobilityRaster IonMobilityRasterizer::render(
    const OpenMS::MSSpectrum& spectrum, const IonMobilityRange& range, QSize size,
    PeakMapColorMap colorMap)
  {
    IonMobilityRaster result;
    if (!spectrum.containsIMData() || !range.isValid()
        || size.width() <= 0 || size.height() <= 0) return result;

    const std::size_t mzBins = static_cast<std::size_t>(size.width());
    const std::size_t mobilityBins = static_cast<std::size_t>(size.height());
    std::vector<float> values(mzBins * mobilityBins);
    spectrum.rasterizeIMFrame(values.data(), mobilityBins, mzBins,
                              range.mobilityMin, range.mobilityMax,
                              range.mzMin, range.mzMax,
                              OpenMS::MSSpectrum::RasterAggregation::SUM);

    // Mobilogram and stats come from the original frame, before any visual spread.
    result.mobilogram.assign(mobilityBins, 0.0F);
    std::size_t occupied = 0;
    for (std::size_t mz = 0; mz < mzBins; ++mz)
    {
      for (std::size_t mobility = 0; mobility < mobilityBins; ++mobility)
      {
        const float intensity = values[mz * mobilityBins + mobility];
        result.maximumIntensity = std::max(result.maximumIntensity, intensity);
        if (intensity > 0.0F) ++occupied;
        result.mobilogram[mobility] += intensity;
      }
    }

    result.image = QImage(size, QImage::Format_RGB32);
    result.image.fill(RasterShading::sample(colorMap, 0.0));   // colormap-floor background
    if (result.maximumIntensity <= 0.0F) return result;

    // Histogram equalization from the original frame (before spread), matching the
    // main peak map, so low-abundance mobility bands get their own colour band and
    // colours stay stable as the frame is panned/zoomed.
    const RasterShading::EqualizationCdf equalization = RasterShading::buildEqualization(values);

    // Adaptive point-spreading so sparse/zoomed TIMS frames are not near-invisible
    // single pixels — the same dynspread the main peak map uses.
    const double occupancy = values.empty()
      ? 0.0 : static_cast<double>(occupied) / static_cast<double>(values.size());
    RasterShading::dilateMax(values, mzBins, mobilityBins, RasterShading::dynspreadRadius(occupancy));

    for (std::size_t mz = 0; mz < mzBins; ++mz)
    {
      for (std::size_t mobility = 0; mobility < mobilityBins; ++mobility)
      {
        const float intensity = values[mz * mobilityBins + mobility];
        if (!(intensity > 0.0F)) continue;   // skips empty and non-finite cells
        const double normalized = std::max(equalization.rank(intensity), 1.0 / 255.0);
        result.image.setPixel(static_cast<int>(mz),
                              size.height() - 1 - static_cast<int>(mobility),
                              RasterShading::sample(colorMap, normalized));
      }
    }
    return result;
  }
}
