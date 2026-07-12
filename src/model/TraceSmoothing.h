#pragma once

#include <vector>

namespace OpenMSViewer
{
  // Presentation-layer smoothing for chromatogram / mobilogram traces (a MassDash
  // parity affordance: noisy extracted XICs are hard to read raw). UI-independent
  // and pure so it stays testable and reusable across plot widgets.
  namespace TraceSmoothing
  {
    // Savitzky-Golay lowpass of an intensity trace, wrapping OpenMS
    // SavitzkyGolayFilter (uniform-grid assumption — XIC RT grids are ~uniform).
    // Returns a copy the same length as the input. If the trace is shorter than the
    // frame, the params are invalid, or the filter throws, the input is returned
    // unchanged so smoothing can never break a plot. frameLength is coerced to odd.
    [[nodiscard]] std::vector<double> savitzkyGolay(const std::vector<double>& intensity,
                                                    int frameLength = 11,
                                                    int polynomialOrder = 3);
  }
}
