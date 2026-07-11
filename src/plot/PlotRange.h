#pragma once

#include <algorithm>
#include <cmath>

namespace OpenMSViewer
{
  struct PlotRange
  {
    double rtMin{0.0};
    double rtMax{1.0};
    double mzMin{0.0};
    double mzMax{1.0};

    [[nodiscard]] double rtSpan() const noexcept { return rtMax - rtMin; }
    [[nodiscard]] double mzSpan() const noexcept { return mzMax - mzMin; }

    [[nodiscard]] bool isValid() const noexcept
    {
      return std::isfinite(rtMin) && std::isfinite(rtMax) && std::isfinite(mzMin)
             && std::isfinite(mzMax) && rtMax > rtMin && mzMax > mzMin;
    }

    [[nodiscard]] PlotRange normalized() const noexcept
    {
      PlotRange result{std::min(rtMin, rtMax), std::max(rtMin, rtMax),
                       std::min(mzMin, mzMax), std::max(mzMin, mzMax)};
      if (result.rtMax <= result.rtMin) result.rtMax = result.rtMin + 1.0;
      if (result.mzMax <= result.mzMin) result.mzMax = result.mzMin + 1.0;
      return result;
    }

    [[nodiscard]] PlotRange zoomed(double rtAnchor, double mzAnchor,
                                   double factor) const noexcept
    {
      const double safeFactor = std::clamp(factor, 0.01, 100.0);
      return {
        rtAnchor + (rtMin - rtAnchor) * safeFactor,
        rtAnchor + (rtMax - rtAnchor) * safeFactor,
        mzAnchor + (mzMin - mzAnchor) * safeFactor,
        mzAnchor + (mzMax - mzAnchor) * safeFactor,
      };
    }

    [[nodiscard]] PlotRange translated(double rtDelta, double mzDelta) const noexcept
    {
      return {rtMin + rtDelta, rtMax + rtDelta, mzMin + mzDelta, mzMax + mzDelta};
    }

    [[nodiscard]] PlotRange clampedTo(const PlotRange& outer) const noexcept
    {
      PlotRange result = normalized();
      const PlotRange limit = outer.normalized();

      if (result.rtSpan() >= limit.rtSpan())
      {
        result.rtMin = limit.rtMin;
        result.rtMax = limit.rtMax;
      }
      else
      {
        if (result.rtMin < limit.rtMin)
        {
          const double shift = limit.rtMin - result.rtMin;
          result.rtMin += shift;
          result.rtMax += shift;
        }
        if (result.rtMax > limit.rtMax)
        {
          const double shift = result.rtMax - limit.rtMax;
          result.rtMin -= shift;
          result.rtMax -= shift;
        }
      }

      if (result.mzSpan() >= limit.mzSpan())
      {
        result.mzMin = limit.mzMin;
        result.mzMax = limit.mzMax;
      }
      else
      {
        if (result.mzMin < limit.mzMin)
        {
          const double shift = limit.mzMin - result.mzMin;
          result.mzMin += shift;
          result.mzMax += shift;
        }
        if (result.mzMax > limit.mzMax)
        {
          const double shift = result.mzMax - limit.mzMax;
          result.mzMin -= shift;
          result.mzMax -= shift;
        }
      }
      return result;
    }
  };
}

