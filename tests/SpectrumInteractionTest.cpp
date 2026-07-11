#include "TestData.h"

#include "widgets/PeakMapWidget.h"
#include "widgets/SpectrumWidget.h"
#include "widgets/TicWidget.h"

#include <QApplication>
#include <QTest>
#include <QWheelEvent>

class SpectrumInteractionTest final : public QObject
{
  Q_OBJECT

private slots:
  void zoomsLabelsAndMeasuresSnappedPeaks()
  {
    auto experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
    OpenMSViewer::SpectrumWidget widget;
    widget.resize(820, 420);
    widget.show();
    widget.setExperiment(experiment);
    widget.setSpectrumIndex(0);

    std::optional<std::pair<double, double>> signaledRange;
    connect(&widget, &OpenMSViewer::SpectrumWidget::mzViewChanged, &widget,
            [&](double minimum, double maximum, bool) { signaledRange = {minimum, maximum}; });
    const QPoint center = widget.rect().center();
    QWheelEvent wheel(center, widget.mapToGlobal(center), {}, {0, 120},
                      Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QApplication::sendEvent(&widget, &wheel);
    QVERIFY(widget.mzView().has_value());
    QVERIFY(widget.mzView()->second - widget.mzView()->first < 100.0);
    QVERIFY(signaledRange.has_value());
    widget.resetMzView();
    QVERIFY(!widget.mzView().has_value());

    widget.setMeasurementMode(true);
    QVERIFY(widget.measurementMode());
    const QRect plot = widget.rect().adjusted(62, 42, -18, -42);
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(plot.left() + 2, plot.center().y()));
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(plot.right() - 2, plot.center().y()));
    QCOMPARE(widget.measurements().size(), std::size_t{1});
    QCOMPARE(widget.measurements().front().firstMz, 400.0);
    QCOMPARE(widget.measurements().front().secondMz, 500.0);
    widget.setShowMzLabels(true);

    widget.setMeasurementMode(false);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(plot.left() + plot.width() / 4, plot.center().y()));
    QTest::mouseMove(&widget,
                     QPoint(plot.left() + plot.width() * 3 / 4, plot.center().y()));
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier,
                        QPoint(plot.left() + plot.width() * 3 / 4, plot.center().y()));
    QVERIFY(widget.mzView().has_value());
    QVERIFY(widget.mzView()->first > 420.0 && widget.mzView()->first < 430.0);
    QVERIFY(widget.mzView()->second > 470.0 && widget.mzView()->second < 480.0);
    QCOMPARE(widget.measurements().size(), std::size_t{1});
    widget.clearMeasurements();
    QCOMPARE(widget.measurements().size(), std::size_t{0});
  }

  void snapsToPeakNearestCursorIn2D()
  {
    // Two sticks a few pixels apart in m/z: a tall base peak at 500.0 and a short
    // neighbour at 500.15. The flanking peaks widen the range so the pair sits
    // inside the 10 px horizontal snap window. Old m/z-only snapping always chose
    // the tall peak; 2D snapping must follow the cursor's vertical aim.
    auto experiment = std::make_shared<OpenMS::MSExperiment>();
    experiment->addSpectrum(OpenMSViewer::TestData::spectrum(
      10.0, 1, {{490.0, 3.0F}, {500.0, 100.0F}, {500.15, 20.0F}, {510.0, 3.0F}}));

    OpenMSViewer::SpectrumWidget widget;
    widget.resize(820, 420);
    widget.show();
    widget.setExperiment(experiment);
    widget.setSpectrumIndex(0);
    widget.grab();  // force a paint so peakAt() sees the current on-screen scaling

    const QRect plot = widget.rect().adjusted(62, 42, -18, -42);
    const int tallX = plot.left() + static_cast<int>((500.0 - 490.0) / 20.0 * plot.width());

    widget.setMeasurementMode(true);
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(tallX, plot.top() + 20));
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(tallX, plot.bottom() - 6));

    QCOMPARE(widget.measurements().size(), std::size_t{1});
    // Aiming high selected the tall base peak; aiming low selected the short neighbour.
    QCOMPARE(widget.measurements().front().firstMz, 500.0);
    QVERIFY(std::abs(widget.measurements().front().secondMz - 500.15) < 1e-6);
  }

  void rendersAnnotatedMirrorAndLegendWithoutCrash()
  {
    auto experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
    OpenMSViewer::SpectrumWidget widget;
    widget.resize(820, 420);
    widget.show();
    widget.setExperiment(experiment);
    widget.setSpectrumIndex(1);  // the MS2 spectrum (peaks 200/450/700, precursor 500.2 z2)

    OpenMSViewer::SpectrumAnnotation annotation;
    annotation.sequence = QStringLiteral("PEPTIDE");
    annotation.charge = 2;
    annotation.toleranceDa = 0.05;
    annotation.theoreticalCount = 3;
    annotation.coverage = 2.0 / 3.0;
    annotation.matched.push_back({0, 200.0, 50.0, 200.01, 40.0, -0.01,
                                  QStringLiteral("y1"), OpenMSViewer::IonType::Y, false});
    annotation.matched.push_back({1, 450.0, 500.0, 450.02, 480.0, -0.02,
                                  QStringLiteral("b4"), OpenMSViewer::IonType::B, false});
    annotation.unmatched.push_back({700.5, 30.0, QStringLiteral("y6"), OpenMSViewer::IonType::Y});
    widget.setAnnotation(annotation);

    QVERIFY(!widget.grab().isNull());   // annotated overlay + ion-type legend
    widget.setMirrorMode(true);
    QVERIFY(!widget.grab().isNull());   // butterfly view: coloured top + labelled halves
    widget.setMirrorMode(false);
    widget.setAutoYScale(false);        // base-peak hold scaling path
    widget.setRelativeIntensity(true);
    QVERIFY(!widget.grab().isNull());

    // Hover the matched base peak: exercises the annotation-aware tooltip (ppm) path.
    const QRect plot = widget.rect().adjusted(62, 42, -18, -42);
    const int baseX = plot.left() + static_cast<int>((450.0 - 200.0) / 500.0 * plot.width());
    QTest::mouseMove(&widget, QPoint(baseX, plot.top() + 20));
    QVERIFY(!widget.grab().isNull());
  }

  void ticClickAndDragDriveSelectionAndPeakMapRange()
  {
    OpenMSViewer::TicWidget tic;
    tic.resize(700, 260);
    tic.show();
    tic.setTrace({{10.0, 10.0, 0}, {15.0, 100.0, 1}, {20.0, 20.0, 2}},
                 QStringLiteral("MS1 TIC"));
    tic.setSelectedSpectrum(1);
    QCOMPARE(tic.selectedRt().value(), 15.0);
    tic.setSelectedSpectrum(99);
    tic.setSelectedRt(17.25);
    QCOMPARE(tic.selectedRt().value(), 17.25);
    const QRect plot = tic.rect().adjusted(54, 30, -16, -35);
    std::optional<std::size_t> selected;
    connect(&tic, &OpenMSViewer::TicWidget::spectrumActivated, &tic,
            [&](std::size_t index) { selected = index; });
    QTest::mouseClick(&tic, Qt::LeftButton, Qt::NoModifier, plot.center());
    QCOMPARE(selected.value(), std::size_t{1});

    auto experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
    OpenMSViewer::PeakMapWidget peakMap;
    peakMap.resize(800, 500);
    peakMap.setExperiment(experiment, {10.0, 20.0, 400.0, 600.0});
    connect(&tic, &OpenMSViewer::TicWidget::rtRangeSelected,
            &peakMap, &OpenMSViewer::PeakMapWidget::setRtRange);
    QTest::mousePress(&tic, Qt::LeftButton, Qt::NoModifier,
                      QPoint(plot.left() + plot.width() / 4, plot.center().y()));
    QTest::mouseMove(&tic, QPoint(plot.left() + plot.width() * 3 / 4, plot.center().y()));
    QTest::mouseRelease(&tic, Qt::LeftButton, Qt::NoModifier,
                        QPoint(plot.left() + plot.width() * 3 / 4, plot.center().y()));
    QVERIFY(peakMap.viewRange().rtMin > 12.0 && peakMap.viewRange().rtMin < 13.0);
    QVERIFY(peakMap.viewRange().rtMax > 17.0 && peakMap.viewRange().rtMax < 18.0);
  }
};

int runSpectrumInteractionTests(int argc, char** argv)
{
  SpectrumInteractionTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "SpectrumInteractionTest.moc"
