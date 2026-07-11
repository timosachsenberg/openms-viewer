#include "plot/PlotAxis.h"

#include <cmath>

namespace OpenMSViewer::PlotAxis
{
  namespace
  {
    // Round `value` to a "nice" number; `round` picks the nearest nice value,
    // otherwise the smallest nice value >= value.
    double niceNumber(double value, bool round)
    {
      const double exponent = std::floor(std::log10(value));
      const double fraction = value / std::pow(10.0, exponent);
      double niceFraction;
      if (round)
        niceFraction = fraction < 1.5 ? 1.0 : fraction < 3.0 ? 2.0 : fraction < 7.0 ? 5.0 : 10.0;
      else
        niceFraction = fraction <= 1.0 ? 1.0 : fraction <= 2.0 ? 2.0 : fraction <= 5.0 ? 5.0 : 10.0;
      return niceFraction * std::pow(10.0, exponent);
    }
  }

  std::vector<double> niceTicks(double minimum, double maximum, int targetCount)
  {
    std::vector<double> ticks;
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || !(maximum > minimum) || targetCount < 2)
      return ticks;

    const double range = niceNumber(maximum - minimum, false);
    const double step = niceNumber(range / (targetCount - 1), true);
    if (!(step > 0.0) || !std::isfinite(step)) return ticks;
    const double first = std::ceil(minimum / step) * step;
    if (!std::isfinite(first)) return ticks;
    const double epsilon = step * 1e-6;
    double value = first;
    while (std::isfinite(value) && value <= maximum + epsilon && ticks.size() < 1000)
    {
      // Snap values within a step-epsilon of zero to exactly 0 for clean labels.
      ticks.push_back(std::abs(value) < epsilon ? 0.0 : value);
      const double next = value + step;
      if (!(next > value)) break;   // step below the value's ULP -> no progress, stop
      value = next;
    }
    return ticks;
  }
}
