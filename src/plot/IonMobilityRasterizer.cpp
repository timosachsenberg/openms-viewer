#include "plot/IonMobilityRasterizer.h"

#include <QColor>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

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

    QRgb plasma(double value)
    {
      static constexpr std::array<ColorStop, 7> stops{{
        {0.00, 13, 8, 135}, {0.17, 84, 3, 160}, {0.34, 139, 10, 165},
        {0.50, 185, 50, 137}, {0.67, 219, 92, 104}, {0.84, 244, 150, 65},
        {1.00, 240, 249, 33},
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
  }

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
    const OpenMS::MSSpectrum& spectrum, const IonMobilityRange& range, QSize size)
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

    result.mobilogram.assign(mobilityBins, 0.0F);
    for (std::size_t mz = 0; mz < mzBins; ++mz)
    {
      for (std::size_t mobility = 0; mobility < mobilityBins; ++mobility)
      {
        const float intensity = values[mz * mobilityBins + mobility];
        result.maximumIntensity = std::max(result.maximumIntensity, intensity);
        result.mobilogram[mobility] += intensity;
      }
    }

    result.image = QImage(size, QImage::Format_RGB32);
    result.image.fill(QColor(9, 8, 18));
    if (result.maximumIntensity <= 0.0F) return result;
    const double logMaximum = std::log1p(static_cast<double>(result.maximumIntensity));
    for (std::size_t mz = 0; mz < mzBins; ++mz)
    {
      for (std::size_t mobility = 0; mobility < mobilityBins; ++mobility)
      {
        const float intensity = values[mz * mobilityBins + mobility];
        if (intensity <= 0.0F) continue;
        const double normalized = std::log1p(static_cast<double>(intensity)) / logMaximum;
        result.image.setPixel(static_cast<int>(mz),
                              size.height() - 1 - static_cast<int>(mobility),
                              plasma(std::pow(normalized, 0.7)));
      }
    }
    return result;
  }
}
