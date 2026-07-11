#include "TestData.h"

#include "plot/PeakMapRasterizer.h"
#include "plot/PlotRange.h"
#include "widgets/PeakMapWidget.h"

#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QTest>
#include <QTimer>

#include <optional>

namespace
{
  // Fill the modal Go-to-Range dialog's four spin boxes (rtMin/rtMax/mzMin/mzMax,
  // in construction order) and accept it, once it appears. Bounded so it can never
  // spin forever if no dialog opens.
  void scheduleGoToRangeFill(double rtMin, double rtMax, double mzMin, double mzMax)
  {
    auto* timer = new QTimer(qApp);
    auto* attempts = new int(0);
    QObject::connect(timer, &QTimer::timeout, timer, [=]()
    {
      for (QWidget* widget : QApplication::topLevelWidgets())
      {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (!dialog || !dialog->isVisible()) continue;
        const auto spins = dialog->findChildren<QDoubleSpinBox*>();
        if (spins.size() < 4) continue;
        spins[0]->setValue(rtMin); spins[1]->setValue(rtMax);
        spins[2]->setValue(mzMin); spins[3]->setValue(mzMax);
        dialog->accept();
        timer->stop(); timer->deleteLater(); delete attempts;
        return;
      }
      if (++(*attempts) > 500) { timer->stop(); timer->deleteLater(); delete attempts; }
    });
    timer->start(1);
  }
}

class PeakMapRasterizerTest final : public QObject
{
  Q_OBJECT

private slots:
  void plotRangeClampsAndZooms()
  {
    const OpenMSViewer::PlotRange outer{0.0, 100.0, 300.0, 900.0};
    const auto zoomed = outer.zoomed(50.0, 600.0, 0.5);
    QCOMPARE(zoomed.rtMin, 25.0);
    QCOMPARE(zoomed.rtMax, 75.0);
    QCOMPARE(zoomed.mzMin, 450.0);
    QCOMPARE(zoomed.mzMax, 750.0);

    const auto clamped = zoomed.translated(-100.0, 500.0).clampedTo(outer);
    QCOMPARE(clamped.rtMin, 0.0);
    QCOMPARE(clamped.rtMax, 50.0);
    QCOMPARE(clamped.mzMin, 600.0);
    QCOMPARE(clamped.mzMax, 900.0);
  }

  void rendersMs1InBothOrientations()
  {
    const auto experiment = OpenMSViewer::TestData::experiment();
    const OpenMSViewer::PlotRange range{9.0, 21.0, 390.0, 610.0};
    const QRgb background =
      OpenMSViewer::PeakMapRasterizer::color(0.0, OpenMSViewer::PeakMapColorMap::Viridis);

    const QImage conventional = OpenMSViewer::PeakMapRasterizer::render(
      experiment, range, QSize(80, 40), false, 1);
    QCOMPARE(conventional.size(), QSize(80, 40));
    QVERIFY(countNonBackground(conventional, background) >= 4);

    const QImage swapped = OpenMSViewer::PeakMapRasterizer::render(
      experiment, range, QSize(80, 40), true, 1);
    QCOMPARE(swapped.size(), QSize(80, 40));
    QVERIFY(countNonBackground(swapped, background) >= 4);
  }

  void filtersByMsLevel()
  {
    const auto experiment = OpenMSViewer::TestData::experiment();
    const OpenMSViewer::PlotRange range{9.0, 21.0, 390.0, 610.0};
    const QRgb background =
      OpenMSViewer::PeakMapRasterizer::color(0.0, OpenMSViewer::PeakMapColorMap::Viridis);
    const QImage ms2 = OpenMSViewer::PeakMapRasterizer::render(
      experiment, range, QSize(80, 40), false, 2);
    // The single MS2 peak renders as a small spread blob (adaptive dynspread),
    // not a lone pixel — but MS-level filtering still yields exactly one cluster.
    const int ms2Pixels = countNonBackground(ms2, background);
    QVERIFY(ms2Pixels >= 1 && ms2Pixels <= 70);  // one peak spread by graduated dynspread
  }

  void rendersExactPeaksAtDeepZoom()
  {
    const auto experiment = OpenMSViewer::TestData::experiment();
    // Both spans below threshold (rt 10 s < 60, m/z 20 < 50) -> exact point path.
    const OpenMSViewer::PlotRange range{5.0, 15.0, 395.0, 415.0};
    const QRgb background =
      OpenMSViewer::PeakMapRasterizer::color(0.0, OpenMSViewer::PeakMapColorMap::Viridis);
    const QImage image = OpenMSViewer::PeakMapRasterizer::render(
      experiment, range, QSize(200, 100), false, 1);

    // The one in-range MS1 peak (rt 10, m/z 400) is drawn as a small blob at its
    // exact position — proving per-peak point rendering, not scan aggregation.
    long sumX = 0;
    long sumY = 0;
    long count = 0;
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x)
        if (image.pixel(x, y) != background) { sumX += x; sumY += y; ++count; }
    QVERIFY(count >= 1 && count <= 70);
    const double centroidX = static_cast<double>(sumX) / static_cast<double>(count);
    const double centroidY = static_cast<double>(sumY) / static_cast<double>(count);
    QVERIFY(qAbs(centroidX - 100.0) < 4.0);  // rt 10 maps to mid-width
    QVERIFY(qAbs(centroidY - 74.0) < 4.0);   // m/z 400 maps near the bottom
  }

  void supportsIntensityScales()
  {
    const auto experiment = OpenMSViewer::TestData::experiment();
    const OpenMSViewer::PlotRange range{9.0, 21.0, 390.0, 610.0};
    const QImage logarithmic = OpenMSViewer::PeakMapRasterizer::render(
      experiment, range, QSize(100, 70), true, 1, OpenMSViewer::PeakMapColorMap::Viridis,
      OpenMSViewer::PeakMapIntensityScale::Logarithmic);
    const QImage linear = OpenMSViewer::PeakMapRasterizer::render(
      experiment, range, QSize(100, 70), true, 1, OpenMSViewer::PeakMapColorMap::Viridis,
      OpenMSViewer::PeakMapIntensityScale::Linear);
    QVERIFY(!logarithmic.isNull());
    QVERIFY(!linear.isNull());
    QVERIFY(logarithmic != linear);
  }

  void supportsColormapsAndInteractiveMinimap()
  {
    const auto source = OpenMSViewer::TestData::experiment();
    const OpenMSViewer::PlotRange range{9.0, 21.0, 390.0, 610.0};
    const QImage viridis = OpenMSViewer::PeakMapRasterizer::render(
      source, range, QSize(100, 70), true, 1, OpenMSViewer::PeakMapColorMap::Viridis);
    const QImage grayscale = OpenMSViewer::PeakMapRasterizer::render(
      source, range, QSize(100, 70), true, 1, OpenMSViewer::PeakMapColorMap::Grayscale);
    QVERIFY(!viridis.isNull());
    QVERIFY(!grayscale.isNull());
    QVERIFY(viridis != grayscale);

    OpenMSViewer::PeakMapWidget widget;
    widget.resize(900, 600);
    widget.show();
    widget.setExperiment(std::make_shared<OpenMS::MSExperiment>(source), range);
    QTRY_VERIFY_WITH_TIMEOUT(!widget.minimapImage().isNull(), 3000);
    QCOMPARE(widget.minimapImage().size(), QSize(220, 130));
    widget.zoomIn();
    const double oldRtCenter = (widget.viewRange().rtMin + widget.viewRange().rtMax) / 2.0;
    const double oldMzCenter = (widget.viewRange().mzMin + widget.viewRange().mzMax) / 2.0;
    const QRect mini = widget.minimapRect();
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(mini.right() - 3, mini.top() + 3));
    const double newRtCenter = (widget.viewRange().rtMin + widget.viewRange().rtMax) / 2.0;
    const double newMzCenter = (widget.viewRange().mzMin + widget.viewRange().mzMax) / 2.0;
    QVERIFY(newRtCenter > oldRtCenter);
    QVERIFY(newMzCenter > oldMzCenter);
    widget.setColorMap(3);
    QTRY_VERIFY_WITH_TIMEOUT(!widget.minimapImage().isNull(), 3000);
  }

  void rendersPeakMapAtFullResolution()
  {
    const auto source = OpenMSViewer::TestData::experiment();
    const OpenMSViewer::PlotRange range{9.0, 21.0, 390.0, 610.0};
    OpenMSViewer::PeakMapWidget widget;
    widget.resize(900, 600);
    widget.show();
    widget.setExperiment(std::make_shared<OpenMS::MSExperiment>(source), range);

    QTRY_VERIFY_WITH_TIMEOUT(!widget.rasterImage().isNull(), 3000);
    // The raster is rendered at full canvas resolution on both axes — no longer
    // capped to the number of visible scans — so deep zooms stay crisp.
    QVERIFY(widget.rasterImage().width() > 700);
    QVERIFY(widget.rasterImage().height() > 400);

    // Sparse content is still visible thanks to the adaptive spread.
    const QImage& raster = widget.rasterImage();
    const QRgb floor = OpenMSViewer::PeakMapRasterizer::color(0.0, OpenMSViewer::PeakMapColorMap::Viridis);
    int nonBackground = 0;
    for (int row = 0; row < raster.height() && nonBackground == 0; ++row)
      for (int column = 0; column < raster.width(); ++column)
        if (raster.pixel(column, row) != floor) { ++nonBackground; break; }
    QVERIFY(nonBackground > 0);

    widget.setAxesSwapped(false);
    QTRY_VERIFY_WITH_TIMEOUT(widget.rasterImage().width() > 700, 3000);
    QVERIFY(widget.rasterImage().height() > 400);
  }

  void equalizationRevealsFaintPeaks()
  {
    // One scan with a dominant peak and a 100000x fainter one, both in view.
    OpenMS::MSExperiment experiment;
    experiment.addSpectrum(OpenMSViewer::TestData::spectrum(
      10.0, 1, {{500.0, 1000000.0F}, {520.0, 10.0F}}));
    const OpenMSViewer::PlotRange range{5.0, 15.0, 490.0, 530.0};  // exact-points path

    const QImage linear = OpenMSViewer::PeakMapRasterizer::render(
      experiment, range, QSize(200, 100), false, 1, OpenMSViewer::PeakMapColorMap::Viridis,
      OpenMSViewer::PeakMapIntensityScale::Linear);
    const QImage equalized = OpenMSViewer::PeakMapRasterizer::render(
      experiment, range, QSize(200, 100), false, 1, OpenMSViewer::PeakMapColorMap::Viridis,
      OpenMSViewer::PeakMapIntensityScale::Equalized);
    QVERIFY(!linear.isNull() && !equalized.isNull());

    // Linear leaves the faint peak near the colormap floor (deep purple); rank
    // equalization lifts it into a bright green viridis band, so total greenness
    // rises. The dominant peak renders identically in both, so the delta is the
    // faint peak becoming visible.
    const auto greenSum = [](const QImage& image)
    {
      long total = 0;
      for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) total += qGreen(image.pixel(x, y));
      return total;
    };
    QVERIFY(greenSum(equalized) > greenSum(linear));
  }

  void newColormapsRenderDistinctlyWithRealLuts()
  {
    const auto experiment = OpenMSViewer::TestData::experiment();
    const OpenMSViewer::PlotRange range{9.0, 21.0, 390.0, 610.0};
    const auto render = [&](OpenMSViewer::PeakMapColorMap map)
    {
      return OpenMSViewer::PeakMapRasterizer::render(experiment, range, QSize(100, 70), true, 1, map);
    };
    const QImage inferno = render(OpenMSViewer::PeakMapColorMap::Inferno);
    const QImage jet = render(OpenMSViewer::PeakMapColorMap::Jet);
    const QImage hot = render(OpenMSViewer::PeakMapColorMap::Hot);
    QVERIFY(!inferno.isNull() && !jet.isNull() && !hot.isNull());
    QVERIFY(inferno != jet);
    QVERIFY(jet != hot);

    // Real perceptual LUT endpoints: viridis runs deep-purple -> yellow.
    QCOMPARE(OpenMSViewer::PeakMapRasterizer::color(0.0, OpenMSViewer::PeakMapColorMap::Viridis),
             qRgb(68, 1, 84));
    const QRgb top = OpenMSViewer::PeakMapRasterizer::color(1.0, OpenMSViewer::PeakMapColorMap::Viridis);
    QVERIFY(qRed(top) > 200 && qGreen(top) > 200 && qBlue(top) < 80);
  }

  void goToRangeDialogNavigatesToEnteredRange()
  {
    const auto source = OpenMSViewer::TestData::experiment();
    const OpenMSViewer::PlotRange bounds{0.0, 30.0, 300.0, 700.0};
    OpenMSViewer::PeakMapWidget widget;
    widget.resize(900, 600);
    widget.show();
    widget.setExperiment(std::make_shared<OpenMS::MSExperiment>(source), bounds);
    widget.setFocus();

    scheduleGoToRangeFill(10.0, 20.0, 400.0, 500.0);
    QTest::keyClick(&widget, Qt::Key_G);   // opens the modal dialog; the timer fills+accepts

    QVERIFY(qAbs(widget.viewRange().rtMin - 10.0) < 1e-6);
    QVERIFY(qAbs(widget.viewRange().rtMax - 20.0) < 1e-6);
    QVERIFY(qAbs(widget.viewRange().mzMin - 400.0) < 1e-6);
    QVERIFY(qAbs(widget.viewRange().mzMax - 500.0) < 1e-6);
  }

  void spectrumMarkerDistinguishesMs1AndMs2()
  {
    const auto source = OpenMSViewer::TestData::experiment();
    const OpenMSViewer::PlotRange bounds{9.0, 21.0, 390.0, 610.0};
    OpenMSViewer::PeakMapWidget widget;
    widget.resize(900, 600);
    widget.show();
    widget.setExperiment(std::make_shared<OpenMS::MSExperiment>(source), bounds);
    QTRY_VERIFY_WITH_TIMEOUT(!widget.rasterImage().isNull(), 3000);

    widget.setSpectrumMarker(11.0, 1, std::nullopt);
    const QImage ms1 = widget.grab().toImage();
    // An MS2 selection adds a precursor m/z line + crosshair in a different colour.
    widget.setSpectrumMarker(11.0, 2, std::optional<double>(500.2));
    const QImage ms2 = widget.grab().toImage();
    QVERIFY(!ms1.isNull() && !ms2.isNull());
    QVERIFY(ms1 != ms2);
  }

  void rejectsInvalidRequest()
  {
    const auto experiment = OpenMSViewer::TestData::experiment();
    QVERIFY(OpenMSViewer::PeakMapRasterizer::render(
      experiment, {1.0, 1.0, 2.0, 3.0}, QSize(20, 20)).isNull());
    QVERIFY(OpenMSViewer::PeakMapRasterizer::render(
      experiment, {1.0, 2.0, 2.0, 3.0}, QSize()).isNull());
  }

private:
  static int countNonBackground(const QImage& image, QRgb background)
  {
    int result = 0;
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x)
        if (image.pixel(x, y) != background) ++result;
    return result;
  }
};

int runPeakMapRasterizerTests(int argc, char** argv)
{
  PeakMapRasterizerTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "PeakMapRasterizerTest.moc"
