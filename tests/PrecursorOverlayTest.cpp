#include "TestData.h"

#include "widgets/PeakMapWidget.h"

#include <OpenMS/KERNEL/MSExperiment.h>

#include <QTest>

#include <memory>
#include <optional>

class PrecursorOverlayTest final : public QObject
{
  Q_OBJECT

private slots:
  void rendersAndClickSelectsTheScan()
  {
    OpenMSViewer::PeakMapWidget peakMap;
    peakMap.resize(820, 520);
    peakMap.show();
    auto experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
    peakMap.setExperiment(experiment, OpenMSViewer::PlotRange{10.0, 20.0, 200.0, 700.0});

    // The MS2 scan (index 1) at RT 11 with precursor m/z 500.2 and a 2 Th window.
    OpenMSViewer::PrecursorMarker marker;
    marker.spectrumIndex = 1;
    marker.rt = 11.0;
    marker.mz = 500.2;
    marker.lowerMz = 499.2;
    marker.upperMz = 501.2;
    marker.charge = 2;
    marker.msLevel = 2;
    peakMap.setPrecursorMarkers({marker});
    peakMap.setShowPrecursors(true);
    peakMap.resetView();
    QVERIFY(!peakMap.grab().isNull());  // the marker + dashed isolation window paint

    std::optional<std::size_t> activated;
    connect(&peakMap, &OpenMSViewer::PeakMapWidget::precursorActivated, &peakMap,
            [&](std::size_t index) { activated = index; });
    const QPoint at = peakMap.mapDataToWidget(11.0, 500.2).toPoint();
    QTest::mouseClick(&peakMap, Qt::LeftButton, Qt::NoModifier, at);
    QTRY_VERIFY_WITH_TIMEOUT(activated.has_value(), 2000);
    QCOMPARE(*activated, std::size_t{1});
  }

  void toggledOnWithoutMarkersIsSafe()
  {
    OpenMSViewer::PeakMapWidget peakMap;
    peakMap.resize(400, 300);
    peakMap.setShowPrecursors(true);  // no markers set
    QVERIFY(!peakMap.grab().isNull());
  }

  // A zoomed-in raster click commits the exact snapped peak m/z; a click off any
  // peak clears it. Both axis orientations must agree.
  void clickCommitsSnappedMzAndClearsOffPeak()
  {
    for (const bool swapped : {true, false})
    {
      OpenMSViewer::PeakMapWidget peakMap;
      peakMap.setAxesSwapped(swapped);
      peakMap.resize(820, 520);
      peakMap.show();
      auto experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
      // A narrow m/z window (< 10 Da span) so the 500.0 peak is individually resolvable.
      peakMap.setExperiment(experiment, OpenMSViewer::PlotRange{9.0, 12.0, 496.0, 504.0});
      peakMap.resetView();
      peakMap.grab();  // force layout so mapDataToWidget() is valid

      std::optional<double> committed;
      bool cleared = false;
      connect(&peakMap, &OpenMSViewer::PeakMapWidget::mzActivated, &peakMap,
              [&](double mz) { committed = mz; });
      connect(&peakMap, &OpenMSViewer::PeakMapWidget::mzCleared, &peakMap,
              [&] { cleared = true; });

      // Click on the base peak (RT 10, m/z 500) -> commits its exact m/z.
      const QPoint onPeak = peakMap.mapDataToWidget(10.0, 500.0).toPoint();
      QTest::mouseClick(&peakMap, Qt::LeftButton, Qt::NoModifier, onPeak);
      QTRY_VERIFY_WITH_TIMEOUT(committed.has_value(), 2000);
      QVERIFY(std::abs(*committed - 500.0) < 1e-6);
      QVERIFY(!cleared);

      // Click well away from any peak (same scan, m/z 502) -> clears.
      committed.reset();
      const QPoint offPeak = peakMap.mapDataToWidget(10.0, 502.0).toPoint();
      QTest::mouseClick(&peakMap, Qt::LeftButton, Qt::NoModifier, offPeak);
      QTRY_VERIFY_WITH_TIMEOUT(cleared, 2000);
      QVERIFY(!committed.has_value());
    }
  }

  // Snapping off: every click commits the raw cursor m/z (never clears), so an m/z
  // can be pinned where no peak exists.
  void snapOffCommitsRawMz()
  {
    OpenMSViewer::PeakMapWidget peakMap;
    peakMap.resize(820, 520);
    peakMap.show();
    auto experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
    peakMap.setExperiment(experiment, OpenMSViewer::PlotRange{9.0, 12.0, 496.0, 504.0});
    peakMap.setSnapToPeak(false);
    peakMap.resetView();
    peakMap.grab();

    std::optional<double> committed;
    bool cleared = false;
    connect(&peakMap, &OpenMSViewer::PeakMapWidget::mzActivated, &peakMap,
            [&](double mz) { committed = mz; });
    connect(&peakMap, &OpenMSViewer::PeakMapWidget::mzCleared, &peakMap,
            [&] { cleared = true; });

    const QPoint offPeak = peakMap.mapDataToWidget(10.0, 502.0).toPoint();
    QTest::mouseClick(&peakMap, Qt::LeftButton, Qt::NoModifier, offPeak);
    QTRY_VERIFY_WITH_TIMEOUT(committed.has_value(), 2000);
    QVERIFY(std::abs(*committed - 502.0) < 0.5);  // raw cursor, ~pixel accurate
    QVERIFY(!cleared);
  }

  // Clicking a precursor overlay activates the scan but must NOT commit an m/z:
  // an overlay activation is not a raster peak pick.
  void precursorClickDoesNotCommitMz()
  {
    OpenMSViewer::PeakMapWidget peakMap;
    peakMap.resize(820, 520);
    peakMap.show();
    auto experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
    peakMap.setExperiment(experiment, OpenMSViewer::PlotRange{10.0, 20.0, 200.0, 700.0});
    OpenMSViewer::PrecursorMarker marker;
    marker.spectrumIndex = 1;
    marker.rt = 11.0;
    marker.mz = 500.2;
    marker.lowerMz = 499.2;
    marker.upperMz = 501.2;
    marker.charge = 2;
    marker.msLevel = 2;
    peakMap.setPrecursorMarkers({marker});
    peakMap.setShowPrecursors(true);
    peakMap.resetView();
    peakMap.grab();

    bool precursorActivated = false;
    bool mzTouched = false;
    connect(&peakMap, &OpenMSViewer::PeakMapWidget::precursorActivated, &peakMap,
            [&](std::size_t) { precursorActivated = true; });
    connect(&peakMap, &OpenMSViewer::PeakMapWidget::mzActivated, &peakMap,
            [&](double) { mzTouched = true; });
    connect(&peakMap, &OpenMSViewer::PeakMapWidget::mzCleared, &peakMap,
            [&] { mzTouched = true; });

    const QPoint at = peakMap.mapDataToWidget(11.0, 500.2).toPoint();
    QTest::mouseClick(&peakMap, Qt::LeftButton, Qt::NoModifier, at);
    QTRY_VERIFY_WITH_TIMEOUT(precursorActivated, 2000);
    QVERIFY(!mzTouched);
  }

  // The pinned m/z draws without crashing in both orientations and hides when cleared.
  void selectedMzLineRenders()
  {
    OpenMSViewer::PeakMapWidget peakMap;
    peakMap.resize(820, 520);
    peakMap.show();
    auto experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
    peakMap.setExperiment(experiment, OpenMSViewer::PlotRange{10.0, 20.0, 400.0, 600.0});
    peakMap.setSelectedMz(500.0);
    QVERIFY(!peakMap.grab().isNull());
    peakMap.setAxesSwapped(false);
    QVERIFY(!peakMap.grab().isNull());
    peakMap.setSelectedMz(std::nullopt);
    QVERIFY(!peakMap.grab().isNull());
  }
};

int runPrecursorOverlayTests(int argc, char** argv)
{
  PrecursorOverlayTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "PrecursorOverlayTest.moc"
