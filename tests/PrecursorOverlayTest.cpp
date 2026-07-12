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
};

int runPrecursorOverlayTests(int argc, char** argv)
{
  PrecursorOverlayTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "PrecursorOverlayTest.moc"
