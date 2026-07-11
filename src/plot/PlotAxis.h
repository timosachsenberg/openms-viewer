#pragma once

#include <vector>

namespace OpenMSViewer::PlotAxis
{
  // "Nice number" axis ticks (Heckbert): round values (1/2/2.5/5 × 10^k) covering
  // [minimum, maximum] with roughly `targetCount` ticks, so axes read 0, 500, 1000…
  // instead of arbitrary fractions of the range. Returns an empty list for a
  // degenerate or non-finite range.
  [[nodiscard]] std::vector<double> niceTicks(double minimum, double maximum, int targetCount = 5);
}
