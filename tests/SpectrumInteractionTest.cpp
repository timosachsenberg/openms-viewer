#include "TestData.h"

#include "plot/PlotAxis.h"
#include "widgets/PeakMapWidget.h"
#include "widgets/SpectrumWidget.h"
#include "widgets/TicWidget.h"

#include <QApplication>
#include <QInputDialog>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>

namespace
{
  // Auto-answer the next QInputDialog (from the peak-label tool) once it becomes
  // modal, so a click that opens it never blocks the test. Bounded so it can
  // never spin forever if no dialog appears.
  void scheduleDialogResponse(const QString& text)
  {
    auto* timer = new QTimer(qApp);
    auto* attempts = new int(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, attempts, text]()
    {
      for (QWidget* widget : QApplication::topLevelWidgets())
      {
        if (auto* dialog = qobject_cast<QInputDialog*>(widget))
        {
          dialog->setTextValue(text);
          dialog->accept();
          timer->stop();
          timer->deleteLater();
          delete attempts;
          return;
        }
      }
      if (++(*attempts) > 500) { timer->stop(); timer->deleteLater(); delete attempts; }
    });
    timer->start(1);
  }
}

class SpectrumInteractionTest final : public QObject
{
  Q_OBJECT

private slots:
  void zoomsLabelsAndMeasuresSnappedPeaks()
  {
    auto experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
    OpenMSViewer::SpectrumWidget widget;
    QVERIFY(widget.showMzLabels());
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

  void normalClickPicksMzAndClearsOffPeak()
  {
    auto experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
    OpenMSViewer::SpectrumWidget widget;
    widget.resize(820, 420);
    widget.show();
    widget.setExperiment(experiment);
    widget.setSpectrumIndex(0);  // peaks 400 (left edge) and 500 (right edge)
    widget.grab();               // populate peakAt() scaling

    std::optional<double> committed;
    bool cleared = false;
    connect(&widget, &OpenMSViewer::SpectrumWidget::mzActivated, &widget,
            [&](double mz) { committed = mz; });
    connect(&widget, &OpenMSViewer::SpectrumWidget::mzCleared, &widget,
            [&] { cleared = true; });

    const QRect plot = widget.rect().adjusted(62, 42, -18, -42);

    // Normal-mode click on the base peak (right edge) commits its exact m/z.
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(plot.right() - 2, plot.center().y()));
    QVERIFY(committed.has_value());
    QVERIFY(std::abs(*committed - 500.0) < 1e-6);
    QVERIFY(!cleared);

    // A click in the empty middle (no peak near m/z ~450) clears the pin.
    committed.reset();
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(plot.center().x(), plot.center().y()));
    QVERIFY(cleared);
    QVERIFY(!committed.has_value());

    // Measurement mode owns the click: it must NOT commit an m/z.
    committed.reset();
    cleared = false;
    widget.setMeasurementMode(true);
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(plot.right() - 2, plot.center().y()));
    QVERIFY(!committed.has_value());
    QVERIFY(!cleared);
  }

  void selectedMzLineRenders()
  {
    auto experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
    OpenMSViewer::SpectrumWidget widget;
    widget.resize(820, 420);
    widget.show();
    widget.setExperiment(experiment);
    widget.setSpectrumIndex(0);
    widget.setSelectedMz(500.0);
    QVERIFY(!widget.grab().isNull());     // teal m/z line paints
    widget.setSelectedMz(std::nullopt);
    QVERIFY(!widget.grab().isNull());     // hidden again
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

  void addsEditsAndRemovesPeakLabels()
  {
    auto experiment = std::make_shared<OpenMS::MSExperiment>();
    experiment->addSpectrum(OpenMSViewer::TestData::spectrum(
      10.0, 1, {{400.0, 10.0F}, {500.0, 100.0F}, {600.0, 5.0F}}));
    OpenMSViewer::SpectrumWidget widget;
    widget.resize(820, 420);
    widget.show();
    widget.setExperiment(experiment);
    widget.setSpectrumIndex(0);
    widget.grab();  // populate the hit-test scaling used by peakAt()
    widget.setLabelMode(true);
    QVERIFY(widget.labelMode());
    QVERIFY(!widget.measurementMode());  // the two click tools are mutually exclusive
    widget.grab();

    const QRect plot = widget.rect().adjusted(62, 42, -18, -42);
    const int x500 = plot.left() + static_cast<int>((500.0 - 400.0) / 200.0 * plot.width());

    // Click the base peak and add a label via the (auto-answered) dialog.
    scheduleDialogResponse(QStringLiteral("contaminant"));
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(x500, plot.top() + 30));
    QCOMPARE(widget.labels().size(), std::size_t{1});
    QCOMPARE(widget.labels().front().text, QStringLiteral("contaminant"));
    QVERIFY(std::abs(widget.labels().front().mz - 500.0) < 1e-6);
    QVERIFY(!widget.grab().isNull());  // the arrowed label renders without crashing

    // Click the same peak and clear the text to remove the label.
    scheduleDialogResponse(QString());
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(x500, plot.top() + 30));
    QCOMPARE(widget.labels().size(), std::size_t{0});
  }

  void selectsAndDeletesMeasurements()
  {
    auto experiment = std::make_shared<OpenMS::MSExperiment>();
    experiment->addSpectrum(OpenMSViewer::TestData::spectrum(10.0, 1,
      {{300.0, 40.0F}, {400.0, 80.0F}, {500.0, 100.0F}, {600.0, 60.0F}, {650.0, 5.0F}}));
    OpenMSViewer::SpectrumWidget widget;
    widget.resize(820, 420);
    widget.show();
    widget.setExperiment(experiment);
    widget.setSpectrumIndex(0);
    widget.grab();

    const QRect plot = widget.rect().adjusted(62, 42, -18, -42);
    const auto xOf = [&](double mz) {
      return plot.left() + static_cast<int>((mz - 300.0) / 350.0 * plot.width());
    };

    // Two measurements: 300<->400 and 500<->600.
    widget.setMeasurementMode(true);
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(xOf(300.0), plot.bottom() - 6));
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(xOf(400.0), plot.bottom() - 6));
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(xOf(500.0), plot.bottom() - 6));
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(xOf(600.0), plot.bottom() - 6));
    QCOMPARE(widget.measurements().size(), std::size_t{2});
    widget.grab();  // ensure bracket geometry is painted before hit-testing

    // Back in zoom mode, click the first bracket to select it (geometry mirrors paint).
    widget.setMeasurementMode(false);
    const int baseline = plot.bottom();
    const auto yOf = [&](double intensity) {
      return baseline - static_cast<int>(intensity / 100.0 * plot.height() * 0.95);
    };
    const int bracketY = std::max(plot.top() + 22,
      std::min(yOf(40.0), yOf(80.0)) - std::max(18, plot.height() / 14));
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint((xOf(300.0) + xOf(400.0)) / 2, bracketY));
    QCOMPARE(widget.measurements().size(), std::size_t{2});  // selected, not yet removed

    QTest::keyClick(&widget, Qt::Key_Delete);
    QCOMPARE(widget.measurements().size(), std::size_t{1});
    QVERIFY(std::abs(widget.measurements().front().firstMz - 500.0) < 1e-6);  // the other one survives
  }

  void niceTicksAreRoundAndCoverRange()
  {
    using OpenMSViewer::PlotAxis::niceTicks;
    const auto ticks = niceTicks(0.0, 1000.0, 5);
    QVERIFY(ticks.size() >= 4);
    QCOMPARE(ticks.front(), 0.0);
    QVERIFY(ticks.back() <= 1000.0 + 1e-9);
    const double step = ticks[1] - ticks[0];
    QVERIFY(qAbs(step - 200.0) < 1e-6);   // "nice" step for [0,1000] with ~5 ticks
    for (std::size_t i = 1; i < ticks.size(); ++i)
      QVERIFY(qAbs((ticks[i] - ticks[i - 1]) - step) < 1e-6);   // evenly spaced

    // A non-zero-based range still yields round values inside the range.
    const auto rtTicks = niceTicks(1501.0, 1999.0, 6);
    QVERIFY(!rtTicks.empty());
    for (const double tick : rtTicks)
    {
      QVERIFY(tick >= 1501.0 - 1e-9 && tick <= 1999.0 + 1e-9);
      QVERIFY(qAbs(tick - std::round(tick / 100.0) * 100.0) < 1e-6);   // multiples of 100
    }

    // Degenerate / non-finite ranges return no ticks.
    QVERIFY(niceTicks(5.0, 5.0, 5).empty());
    QVERIFY(niceTicks(10.0, 0.0, 5).empty());
  }

  void ticRendersNumericAxesUnitsAndHover()
  {
    OpenMSViewer::TicWidget tic;
    tic.resize(700, 260);
    tic.show();
    tic.setTrace({{100.0, 10.0, 0}, {200.0, 90.0, 1}, {300.0, 40.0, 2}, {400.0, 70.0, 3}},
                 QStringLiteral("MS1 TIC"));
    QVERIFY(!tic.grab().isNull());       // seconds axis + gridlines render
    tic.setRtInMinutes(true);
    QVERIFY(!tic.grab().isNull());       // minutes axis renders

    const QRect plot = tic.rect().adjusted(62, 30, -16, -35);
    QTest::mouseMove(&tic, plot.center());
    QVERIFY(!tic.grab().isNull());       // hover readout path renders without crashing
  }

  void ticWheelZoomAnchorsOnCursorRt()
  {
    OpenMSViewer::TicWidget tic;
    tic.resize(700, 260);
    tic.show();
    std::vector<OpenMSViewer::TicPoint> points;
    for (int i = 0; i <= 10; ++i) points.push_back({10.0 + i, 50.0, static_cast<std::size_t>(i)});
    tic.setTrace(points, QStringLiteral("MS1 TIC"));                      // full RT [10,20]
    tic.setPeakMapRange(OpenMSViewer::PlotRange{12.0, 18.0, 0.0, 1.0});   // current 6 s window

    std::optional<std::pair<double, double>> emitted;
    connect(&tic, &OpenMSViewer::TicWidget::rtRangeSelected, &tic,
            [&](double low, double high) { emitted = {low, high}; });

    const QRect plot = tic.rect().adjusted(62, 30, -16, -35);
    const QPointF pos(plot.left() + plot.width() * 0.25, plot.center().y());
    QWheelEvent wheel(pos, tic.mapToGlobal(pos.toPoint()), {}, {0, 120},
                      Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QApplication::sendEvent(&tic, &wheel);

    QVERIFY(emitted.has_value());
    // Cursor at 25% of the FULL [10,20] axis is RT 12.5; the zoom centres there
    // (the old code anchored inside the sub-range and drifted).
    const double mid = (emitted->first + emitted->second) / 2.0;
    QVERIFY(qAbs(mid - 12.5) < 0.2);
    QVERIFY(emitted->second - emitted->first < 6.0);   // zoomed in from the 6 s window
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
    const QRect plot = tic.rect().adjusted(62, 30, -16, -35);
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
