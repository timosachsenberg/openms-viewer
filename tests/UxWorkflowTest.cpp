#include "TestData.h"

#include "MainWindow.h"
#include "model/ChromatogramSource.h"
#include "model/OswStore.h"
#include "model/RtUnit.h"
#include "model/RunData.h"
#include "widgets/ChromatogramPanelWidget.h"
#include "widgets/FeatureTableWidget.h"
#include "widgets/LoadingOverlayWidget.h"
#include "widgets/PeakMapWidget.h"
#include "widgets/ToastOverlay.h"
#include "widgets/TransitionGroupPlot.h"
#include "widgets/WelcomeWidget.h"

#include <OpenMS/FORMAT/FeatureXMLFile.h>
#include <OpenMS/FORMAT/MzMLFile.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QScrollArea>
#include <QSpinBox>
#include <QAbstractItemModel>
#include <QStackedWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

class UxWorkflowTest final : public QObject
{
  Q_OBJECT

private slots:
  void automaticMzLabelsAreEnabledByDefault()
  {
    QSettings().clear();
    OpenMSViewer::MainWindow window;
    auto* action = window.findChild<QAction*>(QStringLiteral("showMzLabels"));
    QVERIFY(action != nullptr);
    QVERIFY(action->isChecked());
  }

  void rtInMinutesFormatsHelperAndTable()
  {
    // The shared helper is the single source of truth for RT display.
    QCOMPARE(OpenMSViewer::RtUnit::format(120.0, false), QStringLiteral("120.00"));
    QCOMPARE(OpenMSViewer::RtUnit::format(120.0, true), QStringLiteral("2.000"));
    QCOMPARE(OpenMSViewer::RtUnit::columnHeader(false), QStringLiteral("RT (s)"));
    QCOMPARE(OpenMSViewer::RtUnit::columnHeader(true), QStringLiteral("RT (min)"));

    // A model-backed table re-renders its RT column header + values on toggle.
    OpenMSViewer::FeatureTableWidget table;
    OpenMSViewer::FeatureRecord record;
    record.index = 0;
    record.rt = 120.0;
    record.mz = 500.0;
    record.charge = 2;
    record.intensity = 1000.0;
    table.setFeatures({record});
    auto* view = table.findChild<QTableView*>(QStringLiteral("featureTable"));
    QVERIFY(view != nullptr && view->model() != nullptr);
    QAbstractItemModel* model = view->model();
    int rtColumn = -1;
    for (int column = 0; column < model->columnCount(); ++column)
      if (model->headerData(column, Qt::Horizontal).toString().contains(QStringLiteral("RT")))
      { rtColumn = column; break; }
    QVERIFY(rtColumn >= 0);
    QVERIFY(model->headerData(rtColumn, Qt::Horizontal).toString().contains(QStringLiteral("(s)")));
    QCOMPARE(model->index(0, rtColumn).data().toString(), QStringLiteral("120.00"));

    table.setRtInMinutes(true);
    QVERIFY(model->headerData(rtColumn, Qt::Horizontal).toString().contains(QStringLiteral("(min)")));
    QCOMPARE(model->index(0, rtColumn).data().toString(), QStringLiteral("2.000"));
  }

  void layoutPresetsFeatureAndHideRelevantPanels()
  {
    QSettings().clear();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mzml = dir.filePath(QStringLiteral("preset.mzML"));
    const QString feat = dir.filePath(QStringLiteral("preset.featureXML"));
    OpenMS::MzMLFile().store(mzml.toStdString(), OpenMSViewer::TestData::experiment());
    OpenMS::FeatureXMLFile().store(feat.toStdString(), OpenMSViewer::TestData::featureMap());

    OpenMSViewer::MainWindow window;
    window.resize(1200, 800);
    window.show();
    window.loadFiles({mzml, feat});
    auto* featuresDock = window.findChild<QDockWidget*>(QStringLiteral("featuresDock"));
    auto* spectrumDock = window.findChild<QDockWidget*>(QStringLiteral("spectrumDock"));
    auto* imaging = window.findChild<QAction*>(QStringLiteral("layoutImaging"));
    auto* overview = window.findChild<QAction*>(QStringLiteral("layoutOverview"));
    QVERIFY(featuresDock && spectrumDock && imaging && overview);
    QTRY_VERIFY_WITH_TIMEOUT(featuresDock->toggleViewAction()->isEnabled(), 5000);  // features loaded

    // The Imaging preset declutters: it hides the (available) features table and
    // keeps the spectrum visible.
    imaging->trigger();
    QVERIFY(!featuresDock->isVisible());
    QVERIFY(spectrumDock->isVisible());

    // Overview brings the features table back.
    overview->trigger();
    QVERIFY(featuresDock->isVisible());
  }

  void dockPanelMoveActionsRedockVertically()
  {
    OpenMSViewer::MainWindow window;
    auto* spectrumDock = window.findChild<QDockWidget*>(QStringLiteral("spectrumDock"));
    auto* moveLeft = window.findChild<QAction*>(QStringLiteral("spectrumDockMoveLeft"));
    auto* moveRight = window.findChild<QAction*>(QStringLiteral("spectrumDockMoveRight"));
    QVERIFY(spectrumDock && moveLeft && moveRight);

    // The spectrum (bottom by default) can be re-docked into a vertical side column.
    moveLeft->trigger();
    QCOMPARE(window.dockWidgetArea(spectrumDock), Qt::LeftDockWidgetArea);

    // A table joins the same column and STACKS (splits) rather than tabbing.
    auto* featuresDock = window.findChild<QDockWidget*>(QStringLiteral("featuresDock"));
    auto* featuresLeft = window.findChild<QAction*>(QStringLiteral("featuresDockMoveLeft"));
    QVERIFY(featuresDock && featuresLeft);
    featuresLeft->trigger();
    QCOMPARE(window.dockWidgetArea(featuresDock), Qt::LeftDockWidgetArea);
    QVERIFY(!window.tabifiedDockWidgets(featuresDock).contains(spectrumDock));

    moveRight->trigger();
    QCOMPARE(window.dockWidgetArea(spectrumDock), Qt::RightDockWidgetArea);
  }

  void welcomeNavigationRecentFilesAndDockPreference()
  {
    QSettings().clear();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ux-small.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), OpenMSViewer::TestData::experiment());

    OpenMSViewer::MainWindow window;
    window.resize(1280, 820);
    window.show();

    auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("centralStack"));
    auto* welcome = window.findChild<OpenMSViewer::WelcomeWidget*>();
    auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    auto* interactionModes = window.findChild<QWidget*>(QStringLiteral("peakMapInteractionModes"));
    auto* zoomMode = window.findChild<QToolButton*>(QStringLiteral("peakMapZoomMode"));
    auto* panMode = window.findChild<QToolButton*>(QStringLiteral("peakMapPanMode"));
    auto* measureMode = window.findChild<QToolButton*>(QStringLiteral("peakMapMeasureMode"));
    auto* resetView = window.findChild<QToolButton*>(QStringLiteral("peakMapResetView"));
    auto* displayOptions = window.findChild<QToolButton*>(QStringLiteral("peakMapDisplayOptions"));
    auto* editMode = window.findChild<QAction*>(QStringLiteral("peakMapEditMode"));
    auto* colorMap = window.findChild<QComboBox*>(QStringLiteral("peakMapColorMap"));
    auto* intensityScale = window.findChild<QComboBox*>(QStringLiteral("peakMapIntensityScale"));
    auto* level = window.findChild<QComboBox*>(QStringLiteral("spectrumLevelFilter"));
    auto* scan = window.findChild<QSpinBox*>(QStringLiteral("spectrumIndex"));
    auto* rasterWidth = window.findChild<QSpinBox*>(QStringLiteral("peakMapRasterWidth"));
    auto* runContext = window.findChild<QLabel*>(QStringLiteral("runContext"));
    auto* loading = window.findChild<OpenMSViewer::LoadingOverlayWidget*>();
    auto* ticDock = window.findChild<QDockWidget*>(QStringLiteral("ticDock"));
    QVERIFY(stack && welcome && peakMap && interactionModes && zoomMode && panMode
            && measureMode && resetView && displayOptions && editMode && colorMap
            && intensityScale && level && scan && rasterWidth && runContext && loading
            && ticDock);
    QCOMPARE(resetView->toolButtonStyle(), Qt::ToolButtonIconOnly);
    QVERIFY(!resetView->icon().isNull());
    QVERIFY(!resetView->toolTip().isEmpty());
    QVERIFY(displayOptions->menu() != nullptr);
    QVERIFY(displayOptions->menu()->isAncestorOf(colorMap));
    QVERIFY(displayOptions->menu()->isAncestorOf(intensityScale));
    QVERIFY(displayOptions->menu()->isAncestorOf(rasterWidth));
    QCOMPARE(rasterWidth->value(), OpenMSViewer::PeakMapWidget::DefaultRasterWidth);
    rasterWidth->setValue(768);
    QCOMPARE(stack->currentWidget(), static_cast<QWidget*>(welcome));
    QVERIFY(!ticDock->toggleViewAction()->isEnabled());
    for (QToolButton* mode : {zoomMode, panMode, measureMode})
    {
      QVERIFY(!mode->icon().isNull());
      QVERIFY(mode->isCheckable());
      QVERIFY(!mode->toolTip().isEmpty());
    }
    QVERIFY(!editMode->icon().isNull());
    QVERIFY(editMode->isCheckable());
    QVERIFY(!editMode->toolTip().isEmpty());
    QVERIFY(window.findChild<QToolButton*>(QStringLiteral("peakMapEditMode")) == nullptr);
    QVERIFY(zoomMode->isChecked());
    QCOMPARE(intensityScale->count(), 3);
    QCOMPARE(intensityScale->findText(QStringLiteral("Linear")), -1);
    QCOMPARE(level->count(), 3);
    QVERIFY(!interactionModes->isVisible());
    QVERIFY(!peakMap->accessibleName().isEmpty());
    auto* peakMapPanel = window.findChild<QWidget*>(QStringLiteral("peakMapPanel"));
    auto* peakMapScroll = window.findChild<QScrollArea*>(QStringLiteral("peakMapScrollArea"));
    QVERIFY(peakMapPanel != nullptr);
    QVERIFY(peakMapScroll != nullptr);
    QVERIFY(peakMapScroll->widgetResizable());
    QCOMPARE(peakMapScroll->widget(), peakMap);
    QCOMPARE(peakMap->parentWidget(), peakMapScroll->viewport());
    window.loadFile(path);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->hasExperiment(), 5000);
    displayOptions->menu()->popup(
      displayOptions->mapToGlobal(QPoint(0, displayOptions->height())));
    QTRY_VERIFY(displayOptions->menu()->isVisible());
    QVERIFY(colorMap->isVisible());
    QVERIFY(intensityScale->isVisible());
    QVERIFY(rasterWidth->isVisible());
    displayOptions->menu()->hide();
    QTRY_COMPARE_WITH_TIMEOUT(peakMap->rasterImage().size(),
                              QSize(peakMap->width() - 90, peakMap->height() - 72), 3000);
    QVERIFY(peakMap->rasterImage().width() <= 768);
    QVERIFY(peakMap->rasterImage().height() <= 384);
    QCOMPARE(stack->currentWidget(), peakMapPanel);
    QCOMPARE(scan->maximum(), 3);
    QCOMPARE(scan->text(), QStringLiteral("Scan 1"));
    QVERIFY(interactionModes->isVisible());
    measureMode->click();
    QVERIFY(measureMode->isChecked());
    zoomMode->click();
    QVERIFY(zoomMode->isChecked());
    QVERIFY(runContext->text().contains(QStringLiteral("3 spectra")));
    QVERIFY(!loading->isVisible());
    auto* recent = welcome->findChild<QListWidget*>(QStringLiteral("recentFiles"));
    QVERIFY(recent != nullptr);
    QCOMPARE(recent->count(), 1);
    QCOMPARE(recent->item(0)->data(Qt::UserRole).toString(), path);

    QVERIFY(ticDock->toggleViewAction()->isEnabled());
    if (!ticDock->isVisible()) ticDock->toggleViewAction()->trigger();
    QTRY_VERIFY(ticDock->isVisible());

    const auto requiredFeatures = QDockWidget::DockWidgetClosable
      | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable;
    QCOMPARE(ticDock->features() & requiredFeatures, requiredFeatures);
    QVERIFY(ticDock->titleBarWidget() != nullptr);
    auto* dockButton = ticDock->titleBarWidget()->findChild<QToolButton*>(
      QStringLiteral("ticDockDockButton"));
    QVERIFY(dockButton != nullptr);

    ticDock->setFloating(true);
    QTRY_VERIFY(ticDock->isFloating());
    QCOMPARE(ticDock->titleBarWidget()->cursor().shape(), Qt::SizeAllCursor);
    const QPoint originalPosition = ticDock->pos();
    QWidget* titleBar = ticDock->titleBarWidget();
    const QPoint titleCenter = titleBar->rect().center();
    const QPoint globalCenter = titleBar->mapToGlobal(titleCenter);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(titleCenter), QPointF(globalCenter),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(titleBar, &press);
    const QPoint delta(35, 25);
    QMouseEvent move(QEvent::MouseMove, QPointF(titleCenter + delta),
                     QPointF(globalCenter + delta), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(titleBar, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(titleCenter + delta),
                        QPointF(globalCenter + delta), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(titleBar, &release);
    QTRY_VERIFY(ticDock->pos() != originalPosition);

    QTest::mouseClick(dockButton, Qt::LeftButton);
    QTRY_VERIFY(!ticDock->isFloating());
    QCOMPARE(ticDock->titleBarWidget()->cursor().shape(), Qt::ArrowCursor);
    ticDock->toggleViewAction()->trigger();
    QTRY_VERIFY(!ticDock->isVisible());

    window.loadFile(path);
    QTest::qWait(300);
    QVERIFY(peakMap->hasExperiment());
    QVERIFY(!ticDock->isVisible());

    QAction* resetLayout = window.findChild<QAction*>(QStringLiteral("layoutOverview"));
    QVERIFY(resetLayout != nullptr);
    resetLayout->trigger();
    QTRY_VERIFY(ticDock->isVisible());

    QAction* closeData = nullptr;
    for (QAction* action : window.findChildren<QAction*>())
      if (action->text() == QStringLiteral("Close data")) closeData = action;
    QVERIFY(closeData != nullptr);
    closeData->trigger();
    QCOMPARE(stack->currentWidget(), static_cast<QWidget*>(welcome));
  }

  void spectrumDockClosesAndDockedDragUndocks()
  {
    QSettings().clear();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ux-dock.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), OpenMSViewer::TestData::experiment());

    OpenMSViewer::MainWindow window;
    window.resize(1280, 820);
    window.show();
    auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    QVERIFY(peakMap != nullptr);
    window.loadFile(path);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->hasExperiment(), 5000);

    auto* spectrumDock = window.findChild<QDockWidget*>(QStringLiteral("spectrumDock"));
    QVERIFY(spectrumDock != nullptr);
    if (!spectrumDock->isVisible()) spectrumDock->toggleViewAction()->trigger();
    QTRY_VERIFY(spectrumDock->isVisible());
    QVERIFY(!spectrumDock->isFloating());

    // The title-bar close button must actually hide the panel.
    auto* closeButton = spectrumDock->titleBarWidget()->findChild<QToolButton*>(
      QStringLiteral("spectrumDockCloseButton"));
    QVERIFY(closeButton != nullptr);
    QTest::mouseClick(closeButton, Qt::LeftButton);
    QTRY_VERIFY(!spectrumDock->isVisible());

    // Bring it back, still docked, and drag its title bar: it must tear off.
    spectrumDock->toggleViewAction()->trigger();
    QTRY_VERIFY(spectrumDock->isVisible());
    QVERIFY(!spectrumDock->isFloating());

    QWidget* titleBar = spectrumDock->titleBarWidget();
    const QPoint start = titleBar->rect().center();
    const QPoint globalStart = titleBar->mapToGlobal(start);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(start), QPointF(globalStart),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(titleBar, &press);
    const QPoint delta(60, 50);
    QMouseEvent move(QEvent::MouseMove, QPointF(start + delta), QPointF(globalStart + delta),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(titleBar, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(start + delta),
                        QPointF(globalStart + delta), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(titleBar, &release);
    QTRY_VERIFY(spectrumDock->isFloating());

    // The title-bar context menu offers an always-reachable dock/close path.
    auto* floatMenuAction = spectrumDock->titleBarWidget()->findChild<QAction*>(
      QStringLiteral("spectrumDockFloatMenuAction"));
    auto* closeMenuAction = spectrumDock->titleBarWidget()->findChild<QAction*>(
      QStringLiteral("spectrumDockCloseMenuAction"));
    QVERIFY(floatMenuAction != nullptr);
    QVERIFY(closeMenuAction != nullptr);
    QCOMPARE(floatMenuAction->text(), QStringLiteral("Dock panel"));
    floatMenuAction->trigger();
    QTRY_VERIFY(!spectrumDock->isFloating());
    QCOMPARE(floatMenuAction->text(), QStringLiteral("Float panel"));
    closeMenuAction->trigger();
    QTRY_VERIFY(!spectrumDock->isVisible());
  }

  void restoredFloatingPanelComesBackDocked()
  {
    QSettings().clear();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ux-float.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), OpenMSViewer::TestData::experiment());

    // Session 1: float the Spectrum panel and persist that layout.
    {
      OpenMSViewer::MainWindow w;
      w.resize(1280, 820);
      w.show();
      auto* pm = w.findChild<OpenMSViewer::PeakMapWidget*>();
      w.loadFile(path);
      QTRY_VERIFY_WITH_TIMEOUT(pm->hasExperiment(), 5000);
      auto* spec = w.findChild<QDockWidget*>(QStringLiteral("spectrumDock"));
      QVERIFY(spec != nullptr);
      spec->setFloating(true);
      QTRY_VERIFY(spec->isFloating());
      QSettings s;
      s.setValue(QStringLiteral("main/geometry"), w.saveGeometry());
      s.setValue(QStringLiteral("main/state"), w.saveState());
    }

    // Session 2: a floating top-level dock is unresponsive on some platforms
    // (WSLg/Wayland), so startup must re-dock it rather than restore it floating.
    {
      OpenMSViewer::MainWindow w2;
      w2.resize(1280, 820);
      w2.show();
      auto* spec = w2.findChild<QDockWidget*>(QStringLiteral("spectrumDock"));
      QVERIFY(spec != nullptr);
      QVERIFY(!spec->isFloating());
      for (QDockWidget* d : w2.findChildren<QDockWidget*>())
        QVERIFY(!d->isFloating());
    }
    // Do not leak persisted window state into later suites' MainWindow instances.
    QSettings().clear();
  }

  void keyboardModeSwitchSyncsToolbarButtons()
  {
    OpenMSViewer::MainWindow window;
    window.show();

    auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    auto* zoomMode = window.findChild<QToolButton*>(QStringLiteral("peakMapZoomMode"));
    auto* panMode = window.findChild<QToolButton*>(QStringLiteral("peakMapPanMode"));
    auto* measureMode = window.findChild<QToolButton*>(QStringLiteral("peakMapMeasureMode"));
    auto* editMode = window.findChild<QAction*>(QStringLiteral("peakMapEditMode"));
    QVERIFY(peakMap != nullptr);
    QVERIFY(zoomMode && panMode && measureMode && editMode);
    QVERIFY(zoomMode->isChecked());

    // Keyboard mode changes on the plot must keep the toolbar buttons in sync.
    QSignalSpy modeSpy(peakMap, &OpenMSViewer::PeakMapWidget::interactionModeChanged);
    peakMap->setFocus();
    QTest::keyClick(peakMap, Qt::Key_P);
    QVERIFY(panMode->isChecked());
    QTest::keyClick(peakMap, Qt::Key_M);
    QVERIFY(measureMode->isChecked());
    QTest::keyClick(peakMap, Qt::Key_E);
    QVERIFY(editMode->isChecked());
    QTest::keyClick(peakMap, Qt::Key_Z);
    QVERIFY(zoomMode->isChecked());
    QVERIFY(!editMode->isChecked());
    QCOMPARE(modeSpy.count(), 4);

    // Selecting the same mode again must not re-emit or loop.
    peakMap->setInteractionMode(0);
    QCOMPARE(modeSpy.count(), 4);

    // The button's action still drives the plot in the other direction. The
    // toolbar itself is hidden/disabled here because this window has no data.
    measureMode->defaultAction()->trigger();
    QVERIFY(measureMode->isChecked());
    QCOMPARE(modeSpy.count(), 5);
  }

  void toastOverlayStacksColourCodedAndCaps()
  {
    QWidget host;
    host.resize(640, 480);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    auto* toasts = new OpenMSViewer::ToastOverlay(&host);
    QCOMPARE(toasts->activeToastCount(), 0);

    toasts->showToast(QStringLiteral("Loaded run"), OpenMSViewer::ToastLevel::Success);
    QCOMPARE(toasts->activeToastCount(), 1);
    QVERIFY(toasts->isVisible());
    // Painting the overlay (colour-coded card + accent stripe) must not crash.
    QVERIFY(!toasts->grab().isNull());

    // A burst beyond the cap evicts the oldest; live toasts never exceed four.
    for (int index = 0; index < 8; ++index)
      toasts->showToast(QStringLiteral("Event %1").arg(index), OpenMSViewer::ToastLevel::Warning);
    QVERIFY(toasts->activeToastCount() <= 4);
    QVERIFY(toasts->activeToastCount() >= 1);

    toasts->clearToasts();
    QCOMPARE(toasts->activeToastCount(), 0);
  }

  void transitionGroupPlotRendersWithoutCrash()
  {
    OpenMSViewer::TransitionGroupPlot plot;
    plot.resize(520, 300);
    QVERIFY(!plot.grab().isNull());  // empty data → "select a precursor" placeholder

    std::vector<OpenMSViewer::TransitionChromatogram> transitions;
    OpenMSViewer::TransitionChromatogram ms1;
    ms1.msLevel = 1;
    OpenMSViewer::TransitionChromatogram fragment;
    fragment.msLevel = 2;
    fragment.transitionId = 7;
    fragment.annotation = QStringLiteral("y5");
    const double infinity = std::numeric_limits<double>::infinity();
    for (int step = 0; step <= 40; ++step)
    {
      const double rt = 100.0 + step;
      const double value = 1000.0 * std::exp(-((rt - 120.0) * (rt - 120.0)) / 60.0);
      ms1.rt.push_back(rt);
      ms1.intensity.push_back(value * 0.5);
      fragment.rt.push_back(rt);
      // Inject a non-finite sample to exercise the finite-guard path.
      fragment.intensity.push_back(step == 20 ? infinity : value);
    }
    transitions.push_back(ms1);
    transitions.push_back(fragment);

    OpenMSViewer::OswPeakGroup group;
    group.apexRt = 120.0;
    group.leftWidth = 112.0;
    group.rightWidth = 128.0;
    plot.setData(transitions, {group}, 0, 118.0);
    QVERIFY(!plot.grab().isNull());
    plot.setShowAllTransitions(true);
    QVERIFY(!plot.grab().isNull());
  }

  void chromatogramHoverReadoutRendersWithoutCrash()
  {
    OpenMSViewer::ChromatogramPlotWidget plot;
    plot.resize(520, 320);

    OpenMSViewer::ChromatogramRecord record;
    record.nativeId = QStringLiteral("SRM 500.0 -> 300.0");
    record.rtMin = 10.0;
    record.rtMax = 60.0;
    for (int step = 0; step <= 50; ++step)
    {
      const double rt = 10.0 + step;
      const double intensity = 1000.0 * std::exp(-((rt - 35.0) * (rt - 35.0)) / 50.0);
      record.points.push_back({rt, intensity});
      record.maximumIntensity = std::max(record.maximumIntensity, intensity);
    }
    plot.setChromatograms({record});
    plot.setSelectedIndices({0});
    plot.show();
    QVERIFY(QTest::qWaitForWindowExposed(&plot));

    // A hover over the plot draws the snapped RT/intensity chip; it must paint
    // cleanly whether the cursor is over the trace or in empty space. Deliver the
    // move directly, since QTest::mouseMove does not warp the cursor offscreen.
    const auto hoverAt = [&plot](const QPointF& local)
    {
      QMouseEvent move(QEvent::MouseMove, local, plot.mapToGlobal(local.toPoint()),
                       Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(&plot, &move);
    };
    hoverAt(QPointF(260, 160));
    QVERIFY(!plot.grab().isNull());
    hoverAt(QPointF(400, 40));
    QVERIFY(!plot.grab().isNull());
  }
};

int runUxWorkflowTests(int argc, char** argv)
{
  UxWorkflowTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "UxWorkflowTest.moc"
