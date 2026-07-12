#include "model/TraceSmoothing.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

class TraceSmoothingTest final : public QObject
{
  Q_OBJECT

private:
  // Total variation: sum of |y[i+1]-y[i]|. Noise inflates it; smoothing reduces it.
  static double totalVariation(const std::vector<double>& y)
  {
    double tv = 0.0;
    for (std::size_t i = 1; i < y.size(); ++i) tv += std::abs(y[i] - y[i - 1]);
    return tv;
  }

private slots:
  void reducesNoiseKeepsLengthAndNonNegative()
  {
    // Gaussian peak + alternating high-frequency noise.
    std::vector<double> noisy;
    for (int i = 0; i < 60; ++i)
    {
      const double x = (i - 30) / 6.0;
      const double signal = 1000.0 * std::exp(-0.5 * x * x);
      noisy.push_back(signal + (i % 2 == 0 ? 120.0 : -120.0));
    }
    const auto smoothed = OpenMSViewer::TraceSmoothing::savitzkyGolay(noisy);

    QCOMPARE(smoothed.size(), noisy.size());                       // length preserved
    QVERIFY(totalVariation(smoothed) < totalVariation(noisy) * 0.6);  // markedly smoother
    for (const double value : smoothed) QVERIFY(value >= 0.0);     // clamped for display
    // The apex is preserved within a sensible band (not flattened away).
    const double apex = *std::max_element(smoothed.begin(), smoothed.end());
    QVERIFY(apex > 700.0 && apex < 1300.0);
  }

  void shortTracePassesThroughUnchanged()
  {
    const std::vector<double> tiny{3.0, 9.0, 2.0};  // shorter than the default frame
    const auto out = OpenMSViewer::TraceSmoothing::savitzkyGolay(tiny);
    QCOMPARE(out, tiny);
  }

  void emptyTraceIsSafe()
  {
    QVERIFY(OpenMSViewer::TraceSmoothing::savitzkyGolay({}).empty());
  }

  // A single NaN/inf must not spread through the convolution window: the whole
  // trace is returned unchanged so the plot's per-point finite check can skip it.
  void nonFiniteTracePassesThroughUnchanged()
  {
    std::vector<double> trace(40, 100.0);
    trace[20] = std::numeric_limits<double>::quiet_NaN();
    trace[5] = std::numeric_limits<double>::infinity();
    const auto out = OpenMSViewer::TraceSmoothing::savitzkyGolay(trace);
    QCOMPARE(out.size(), trace.size());
    QVERIFY(std::isnan(out[20]));       // untouched
    QVERIFY(std::isinf(out[5]));        // untouched
    QCOMPARE(out[0], 100.0);            // neighbours not contaminated
  }
};

int runTraceSmoothingTests(int argc, char** argv)
{
  TraceSmoothingTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "TraceSmoothingTest.moc"
