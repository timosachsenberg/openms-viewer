#include "plot/RasterShading.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace OpenMSViewer::RasterShading
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

    bool hasIntensity(float value)
    {
      return value > 0.0F && std::isfinite(value);
    }

    // Datashader's density heuristic: among occupied pixels, measure the fraction
    // that have at least one other occupied pixel inside the square search radius.
    double neighborDensity(const std::vector<float>& values,
                           std::size_t dimA, std::size_t dimB, int radius)
    {
      std::size_t occupied = 0;
      std::size_t withNeighbor = 0;
      for (std::size_t a = 0; a < dimA; ++a)
        for (std::size_t b = 0; b < dimB; ++b)
        {
          if (!hasIntensity(values[a * dimB + b])) continue;
          ++occupied;
          bool found = false;
          const long a0 = std::max(0L, static_cast<long>(a) - radius);
          const long a1 = std::min(static_cast<long>(dimA) - 1,
                                   static_cast<long>(a) + radius);
          const long b0 = std::max(0L, static_cast<long>(b) - radius);
          const long b1 = std::min(static_cast<long>(dimB) - 1,
                                   static_cast<long>(b) + radius);
          for (long na = a0; na <= a1 && !found; ++na)
            for (long nb = b0; nb <= b1; ++nb)
            {
              if (na == static_cast<long>(a) && nb == static_cast<long>(b)) continue;
              if (hasIntensity(values[static_cast<std::size_t>(na) * dimB
                                      + static_cast<std::size_t>(nb)]))
              {
                found = true;
                break;
              }
            }
          if (found) ++withNeighbor;
        }
      return occupied == 0
        ? std::numeric_limits<double>::infinity()
        : static_cast<double>(withNeighbor) / static_cast<double>(occupied);
    }

    // Build a 256-entry LUT by piecewise-linear interpolation of control points.
    std::array<QRgb, 256> buildLut(std::initializer_list<ColorStop> stops)
    {
      const std::vector<ColorStop> table(stops);
      std::array<QRgb, 256> lut{};
      for (int index = 0; index < 256; ++index)
      {
        const double position = index / 255.0;
        auto upper = std::upper_bound(table.begin(), table.end(), position,
          [](double needle, const ColorStop& stop) { return needle < stop.position; });
        if (upper == table.begin())
        {
          lut[static_cast<std::size_t>(index)] = qRgb(upper->red, upper->green, upper->blue);
          continue;
        }
        if (upper == table.end())
        {
          const auto& stop = table.back();
          lut[static_cast<std::size_t>(index)] = qRgb(stop.red, stop.green, stop.blue);
          continue;
        }
        const auto& high = *upper;
        const auto& low = *(upper - 1);
        const double span = high.position - low.position;
        const double fraction = span > 0.0 ? (position - low.position) / span : 0.0;
        const auto blend = [fraction](int from, int to)
        {
          return static_cast<int>(std::lround(from + fraction * (to - from)));
        };
        lut[static_cast<std::size_t>(index)] =
          qRgb(blend(low.red, high.red), blend(low.green, high.green), blend(low.blue, high.blue));
      }
      return lut;
    }

    // Control points sampled from the matplotlib colormaps (plus classic jet/hot).
    // Function-local statics build each LUT once, thread-safely, on first use.
    const std::array<QRgb, 256>& lutFor(PeakMapColorMap colorMap)
    {
      static const std::array<QRgb, 256> viridis = buildLut({
        {0.000, 68, 1, 84}, {0.125, 71, 44, 122}, {0.250, 59, 81, 139},
        {0.375, 44, 113, 142}, {0.500, 33, 144, 141}, {0.625, 39, 173, 129},
        {0.750, 92, 200, 99}, {0.875, 170, 220, 50}, {1.000, 253, 231, 37}});
      static const std::array<QRgb, 256> plasma = buildLut({
        {0.000, 13, 8, 135}, {0.166, 84, 3, 160}, {0.333, 139, 10, 165},
        {0.500, 185, 50, 137}, {0.666, 219, 92, 104}, {0.833, 244, 150, 65},
        {1.000, 240, 249, 33}});
      static const std::array<QRgb, 256> inferno = buildLut({
        {0.000, 0, 0, 4}, {0.125, 31, 12, 72}, {0.250, 85, 15, 109},
        {0.375, 136, 34, 106}, {0.500, 186, 54, 85}, {0.625, 227, 89, 51},
        {0.750, 249, 140, 10}, {0.875, 249, 201, 50}, {1.000, 252, 255, 164}});
      static const std::array<QRgb, 256> magma = buildLut({
        {0.000, 0, 0, 4}, {0.125, 28, 16, 68}, {0.250, 79, 18, 123},
        {0.375, 129, 37, 129}, {0.500, 181, 54, 122}, {0.625, 229, 80, 100},
        {0.750, 251, 135, 97}, {0.875, 254, 194, 135}, {1.000, 252, 253, 191}});
      static const std::array<QRgb, 256> jet = buildLut({
        {0.000, 0, 0, 131}, {0.125, 0, 60, 170}, {0.375, 5, 255, 255},
        {0.625, 255, 255, 0}, {0.875, 250, 0, 0}, {1.000, 128, 0, 0}});
      static const std::array<QRgb, 256> hot = buildLut({
        {0.000, 10, 0, 0}, {0.330, 180, 0, 0}, {0.660, 255, 110, 0},
        {0.880, 255, 235, 60}, {1.000, 255, 255, 255}});
      static const std::array<QRgb, 256> grayscale = buildLut({
        {0.000, 0, 0, 0}, {1.000, 255, 255, 255}});

      switch (colorMap)
      {
        case PeakMapColorMap::Viridis: return viridis;
        case PeakMapColorMap::Plasma: return plasma;
        case PeakMapColorMap::Inferno: return inferno;
        case PeakMapColorMap::Magma: return magma;
        case PeakMapColorMap::Jet: return jet;
        case PeakMapColorMap::Hot: return hot;
        case PeakMapColorMap::Grayscale: return grayscale;
      }
      return viridis;
    }
  }

  QRgb sample(PeakMapColorMap colorMap, double normalized)
  {
    // Sanitize non-finite input (e.g. from intensity overflow) to the floor so
    // lround/index can never go out of bounds.
    if (!std::isfinite(normalized)) normalized = 0.0;
    const double clamped = std::clamp(normalized, 0.0, 1.0);
    const auto index = static_cast<std::size_t>(std::clamp<long>(std::lround(clamped * 255.0), 0, 255));
    return lutFor(colorMap)[index];
  }

  EqualizationCdf buildEqualization(const std::vector<float>& values)
  {
    EqualizationCdf result;
    double maximum = 0.0;
    std::size_t occupied = 0;
    for (const float value : values)
      if (value > 0.0F && std::isfinite(value))
      {
        maximum = std::max(maximum, static_cast<double>(value));
        ++occupied;
      }
    const double logMax = std::log1p(maximum);
    if (occupied == 0 || logMax <= 0.0) return result;   // degenerate; rank() -> 0
    result.invLogMax = 1.0 / logMax;

    std::array<double, kEqualizationBins> counts{};
    for (const float value : values)
    {
      if (!(value > 0.0F) || !std::isfinite(value)) continue;
      const double t = std::log1p(static_cast<double>(value)) * result.invLogMax;
      const int bin = std::clamp(static_cast<int>(t * kEqualizationBins), 0, kEqualizationBins - 1);
      counts[static_cast<std::size_t>(bin)] += 1.0;
    }
    const double total = static_cast<double>(occupied);
    double cumulative = 0.0;
    result.cdf[0] = 0.0F;
    for (int bin = 0; bin < kEqualizationBins; ++bin)
    {
      cumulative += counts[static_cast<std::size_t>(bin)] / total;
      result.cdf[static_cast<std::size_t>(bin) + 1] = static_cast<float>(cumulative);
    }
    result.cdf[kEqualizationBins] = 1.0F;
    return result;
  }

  double EqualizationCdf::rank(float value) const
  {
    if (invLogMax <= 0.0 || !(value > 0.0F) || !std::isfinite(value)) return 0.0;
    double position = std::clamp(std::log1p(static_cast<double>(value)) * invLogMax, 0.0, 1.0)
                      * kEqualizationBins;
    const int bin = std::clamp(static_cast<int>(position), 0, kEqualizationBins - 1);
    const double fraction = position - bin;
    const double low = cdf[static_cast<std::size_t>(bin)];
    return low + fraction * (cdf[static_cast<std::size_t>(bin) + 1] - low);
  }

  int dynspreadRadius(const std::vector<float>& values,
                      std::size_t dimA, std::size_t dimB,
                      double threshold, int maxRadius)
  {
    if (dimA == 0 || dimB == 0 || values.size() != dimA * dimB || maxRadius <= 0)
      return 0;
    threshold = std::clamp(threshold, 0.0, 1.0);
    int result = 0;
    for (int candidate = 1; candidate <= maxRadius; ++candidate)
    {
      result = candidate;
      // This matches datashader.transfer_functions.dynspread: its density probe
      // uses px * 2, then backs off one radius once the threshold is crossed.
      if (neighborDensity(values, dimA, dimB, candidate * 2) > threshold)
      {
        --result;
        break;
      }
    }
    return result;
  }

  void dilateMaxCircular(std::vector<float>& values, std::size_t dimA,
                         std::size_t dimB, int radius)
  {
    if (radius <= 0 || dimA == 0 || dimB == 0 || values.size() != dimA * dimB) return;
    std::vector<float> spread(values);
    const double maskRadius = static_cast<double>(radius) + 0.5;
    const double maskRadiusSquared = maskRadius * maskRadius;
    for (std::size_t a = 0; a < dimA; ++a)
      for (std::size_t b = 0; b < dimB; ++b)
      {
        const float source = values[a * dimB + b];
        if (!hasIntensity(source)) continue;
        for (int da = -radius; da <= radius; ++da)
        {
          const long na = static_cast<long>(a) + da;
          if (na < 0 || na >= static_cast<long>(dimA)) continue;
          for (int db = -radius; db <= radius; ++db)
          {
            if (static_cast<double>(da * da + db * db) > maskRadiusSquared) continue;
            const long nb = static_cast<long>(b) + db;
            if (nb < 0 || nb >= static_cast<long>(dimB)) continue;
            float& target = spread[static_cast<std::size_t>(na) * dimB + static_cast<std::size_t>(nb)];
            target = std::max(target, source);
          }
        }
      }
    values.swap(spread);
  }
}
