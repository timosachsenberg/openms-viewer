#include "model/TraceSmoothing.h"

#include <OpenMS/PROCESSING/SMOOTHING/SavitzkyGolayFilter.h>
#include <OpenMS/KERNEL/MSChromatogram.h>

#include <algorithm>
#include <cmath>

namespace OpenMSViewer::TraceSmoothing
{
  std::vector<double> savitzkyGolay(const std::vector<double>& intensity,
                                    int frameLength, int polynomialOrder)
  {
    const int n = static_cast<int>(intensity.size());
    // Coerce to a valid odd frame that fits the trace and exceeds the poly order.
    int frame = std::max(3, frameLength);
    if (frame % 2 == 0) ++frame;
    if (n < frame) return intensity;  // too short to smooth meaningfully
    const int order = std::clamp(polynomialOrder, 2, frame - 1);

    // A NaN/inf sample would spread through the whole convolution window (and does
    // not throw), poisoning good neighbours, so leave a trace with any non-finite
    // sample unsmoothed — the plot's per-point finite check still skips the bad one.
    if (!std::all_of(intensity.begin(), intensity.end(),
                     [](double value) { return std::isfinite(value); }))
      return intensity;

    try
    {
      OpenMS::MSChromatogram chromatogram;
      chromatogram.reserve(static_cast<OpenMS::Size>(n));
      for (int i = 0; i < n; ++i)
      {
        OpenMS::ChromatogramPeak peak;
        peak.setRT(static_cast<double>(i));  // uniform grid: SG smooths the sequence
        peak.setIntensity(static_cast<OpenMS::ChromatogramPeak::IntensityType>(intensity[i]));
        chromatogram.push_back(peak);
      }

      OpenMS::SavitzkyGolayFilter filter;
      OpenMS::Param params = filter.getParameters();
      params.setValue("frame_length", frame);
      params.setValue("polynomial_order", order);
      filter.setParameters(params);
      filter.filter(chromatogram);

      std::vector<double> smoothed(static_cast<std::size_t>(n));
      for (int i = 0; i < n; ++i)  // SG can undershoot below zero; clamp for display
        smoothed[static_cast<std::size_t>(i)] =
          std::max(0.0, static_cast<double>(chromatogram[static_cast<OpenMS::Size>(i)].getIntensity()));
      return smoothed;
    }
    catch (...)
    {
      return intensity;  // never let smoothing break the plot
    }
  }
}
