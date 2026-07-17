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
#include "widgets/RowStackWidget.h"
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

namespace
{
  // Drive a panel-header drag the way the widget sees it: press, a move past the
  // start-drag threshold, a move onto the target, release. Nothing floats and no
  // top-level window moves, so plain synthetic events are the whole story.
  void dragHeader(QWidget* header, const QPoint& fromGlobal, const QPoint& ontoGlobal)
  {
    const auto send = [header](QEvent::Type type, const QPoint& global,
                               Qt::MouseButton button, Qt::MouseButtons buttons)
    {
      const QPoint local = header->mapFromGlobal(global);
      QMouseEvent event(type, QPointF(local), QPointF(global), button, buttons, Qt::NoModifier);
      QApplication::sendEvent(header, &event);
    };
    send(QEvent::MouseButtonPress, fromGlobal, Qt::LeftButton, Qt::LeftButton);
    const QPoint nudge = fromGlobal + QPoint(QApplication::startDragDistance() * 2 + 4, 0);
    send(QEvent::MouseMove, nudge, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseMove, ontoGlobal, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, ontoGlobal, Qt::LeftButton, Qt::NoButton);
  }
}

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
    auto* featuresPanel = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("features"));
    auto* spectrumPanel = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("spectrum"));
    auto* ticPanel = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("tic"));
    auto* imaging = window.findChild<QAction*>(QStringLiteral("layoutImaging"));
    auto* overview = window.findChild<QAction*>(QStringLiteral("layoutOverview"));
    QVERIFY(featuresPanel && spectrumPanel && ticPanel && imaging && overview);
    QTRY_VERIFY_WITH_TIMEOUT(featuresPanel->toggleViewAction()->isEnabled(), 5000);  // features loaded
    QTRY_VERIFY_WITH_TIMEOUT(ticPanel->isShown(), 5000);

    // The Imaging preset declutters: it hides the (available) TIC and features
    // table and keeps the spectrum shown.
    imaging->trigger();
    QVERIFY(!ticPanel->isShown());
    QVERIFY(!featuresPanel->isShown());
    QVERIFY(spectrumPanel->isShown());

    // Overview brings the default panels back. The features table is not among
    // them — with no tabs, a loaded featureXML shows up in Data & layers and as a
    // peak-map overlay, and its table is one View-menu click away.
    overview->trigger();
    QVERIFY(ticPanel->isShown());
    QVERIFY(spectrumPanel->isShown());
    QVERIFY(!featuresPanel->isShown());

    // Opening it explicitly places it, and it stays put across a preset re-apply
    // only if the user keeps it — Overview is a reset, so it clears it again.
    featuresPanel->toggleViewAction()->trigger();
    QVERIFY(featuresPanel->isShown());
    overview->trigger();
    QVERIFY(!featuresPanel->isShown());
  }

  // Dragging a panel header onto the side of another panel joins that panel's row.
  // The row then holds two panels and refuses a third — the invariant, driven
  // through the real widget rather than asserted on the model.
  void dragPanelHeaderFormsTwoPanelRowAndRefusesAThird()
  {
    QSettings().clear();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ux-drag.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), OpenMSViewer::TestData::experiment());

    OpenMSViewer::MainWindow window;
    window.resize(1400, 900);
    window.show();
    // A document is needed for the stack to be the visible page at all: with none
    // loaded the welcome page covers it, and a panel nobody can see is no target.
    auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    QVERIFY(peakMap != nullptr);
    window.loadFile(path);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->hasExperiment(), 5000);

    auto* stack = window.findChild<OpenMSViewer::RowStackWidget*>(QStringLiteral("rowStack"));
    QVERIFY(stack != nullptr);
    auto* tic = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("tic"));
    auto* spectrum = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("spectrum"));
    auto* log = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("log"));
    QVERIFY(tic && spectrum && log);

    // Start from a known arrangement rather than whatever the default happens to be.
    OpenMSViewer::LayoutModel start;
    start.appendRow(tic->id());
    start.appendRow(spectrum->id());
    start.appendRow(log->id());
    stack->setLayoutModel(start);
    QTest::qWait(50);
    QCOMPARE(stack->model().rowCount(), 3);

    QWidget* header = window.findChild<QWidget*>(QStringLiteral("spectrumHeader"));
    QWidget* ticFrame = window.findChild<QWidget*>(QStringLiteral("ticFrame"));
    QVERIFY(header && ticFrame);

    // Drop into the right 30% of the TIC's frame → join its row on the right.
    const QPoint from = header->mapToGlobal(header->rect().center());
    const QPoint onto = ticFrame->mapToGlobal(
      QPoint(ticFrame->width() - ticFrame->width() / 10, ticFrame->height() / 2));
    dragHeader(header, from, onto);

    QTRY_COMPARE(stack->model().rowCount(), 2);
    const auto row0 = stack->model().rows()[0].panels;
    QCOMPARE(row0.size(), std::size_t{2});
    QCOMPARE(row0[0], tic->id());
    QCOMPARE(row0[1], spectrum->id());
    QVERIFY(stack->model().invariantHolds());

    // That row is now full, so it offers no side target to a third panel: the
    // drag must leave the layout untouched rather than widen the row.
    QVERIFY(!stack->model().canDrop(log->id(),
      {OpenMSViewer::LayoutModel::DropKind::RightOfAnchor, tic->id()}));
    QWidget* logHeader = window.findChild<QWidget*>(QStringLiteral("logHeader"));
    QVERIFY(logHeader != nullptr);
    dragHeader(logHeader, logHeader->mapToGlobal(logHeader->rect().center()), onto);
    QTest::qWait(50);
    QCOMPARE(stack->model().rowCount(), 2);
    QCOMPARE(stack->model().rows()[0].panels.size(), std::size_t{2});
    QVERIFY(stack->model().invariantHolds());
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
    auto* spectrumFirst = window.findChild<QToolButton*>(QStringLiteral("spectrumFirst"));
    auto* spectrumPrevious = window.findChild<QToolButton*>(QStringLiteral("spectrumPrevious"));
    auto* spectrumNext = window.findChild<QToolButton*>(QStringLiteral("spectrumNext"));
    auto* spectrumLast = window.findChild<QToolButton*>(QStringLiteral("spectrumLast"));
    auto* spectrumMeasure = window.findChild<QToolButton*>(QStringLiteral("spectrumMeasureMode"));
    auto* spectrumLabel = window.findChild<QToolButton*>(QStringLiteral("spectrumLabelMode"));
    auto* editMode = window.findChild<QAction*>(QStringLiteral("peakMapEditMode"));
    auto* colorMap = window.findChild<QComboBox*>(QStringLiteral("peakMapColorMap"));
    auto* intensityScale = window.findChild<QComboBox*>(QStringLiteral("peakMapIntensityScale"));
    auto* level = window.findChild<QComboBox*>(QStringLiteral("spectrumLevelFilter"));
    auto* scan = window.findChild<QSpinBox*>(QStringLiteral("spectrumIndex"));
    auto* rasterWidth = window.findChild<QSpinBox*>(QStringLiteral("peakMapRasterWidth"));
    auto* runContext = window.findChild<QLabel*>(QStringLiteral("runContext"));
    auto* loading = window.findChild<OpenMSViewer::LoadingOverlayWidget*>();
    auto* ticDock = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("tic"));
    QVERIFY(stack && welcome && peakMap && interactionModes && zoomMode && panMode
            && measureMode && resetView && displayOptions && editMode && colorMap
            && intensityScale && level && scan && rasterWidth && runContext && loading
            && ticDock && spectrumFirst && spectrumPrevious && spectrumNext
            && spectrumLast && spectrumMeasure && spectrumLabel);
    QCOMPARE(resetView->toolButtonStyle(), Qt::ToolButtonIconOnly);
    QVERIFY(!resetView->icon().isNull());
    QVERIFY(!resetView->toolTip().isEmpty());
    QVERIFY(displayOptions->menu() != nullptr);
    QVERIFY(displayOptions->menu()->isAncestorOf(colorMap));
    QVERIFY(displayOptions->menu()->isAncestorOf(intensityScale));
    QVERIFY(displayOptions->menu()->isAncestorOf(rasterWidth));
    for (QToolButton* button : {spectrumFirst, spectrumPrevious, spectrumNext,
                                spectrumLast, spectrumMeasure, spectrumLabel})
    {
      QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
      QVERIFY(!button->icon().isNull());
      QVERIFY(!button->toolTip().isEmpty());
    }
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
    // The data page is the row stack; the peak map is a panel inside it rather
    // than a privileged central widget.
    QCOMPARE(stack->currentWidget(),
             window.findChild<OpenMSViewer::RowStackWidget*>(QStringLiteral("rowStack")));
    QVERIFY(peakMapPanel->isVisible());
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
    if (!ticDock->isShown()) ticDock->toggleViewAction()->trigger();
    QTRY_VERIFY(ticDock->isShown());

    // Closing the panel is the only way out of the layout: there is no floating
    // state to fall into, so a panel is in the stack or hidden and nothing else.
    ticDock->toggleViewAction()->trigger();
    QTRY_VERIFY(!ticDock->isShown());

    window.loadFile(path);
    QTest::qWait(300);
    QVERIFY(peakMap->hasExperiment());
    QVERIFY(!ticDock->isShown());

    QAction* resetLayout = window.findChild<QAction*>(QStringLiteral("layoutOverview"));
    QVERIFY(resetLayout != nullptr);
    resetLayout->trigger();
    QTRY_VERIFY(ticDock->isShown());

    QAction* closeData = nullptr;
    for (QAction* action : window.findChildren<QAction*>())
      if (action->text() == QStringLiteral("Close data")) closeData = action;
    QVERIFY(closeData != nullptr);
    closeData->trigger();
    QCOMPARE(stack->currentWidget(), static_cast<QWidget*>(welcome));
  }

  void panelCloseButtonHidesAndDragReordersRows()
  {
    QSettings().clear();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ux-panel.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), OpenMSViewer::TestData::experiment());

    OpenMSViewer::MainWindow window;
    window.resize(1280, 820);
    window.show();
    auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    QVERIFY(peakMap != nullptr);
    window.loadFile(path);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->hasExperiment(), 5000);

    auto* stack = window.findChild<OpenMSViewer::RowStackWidget*>(QStringLiteral("rowStack"));
    auto* spectrum = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("spectrum"));
    QVERIFY(stack && spectrum);
    if (!spectrum->isShown()) spectrum->toggleViewAction()->trigger();
    QTRY_VERIFY(spectrum->isShown());

    // The header's close button hides the panel — it does not float or destroy it.
    auto* closeButton = window.findChild<QToolButton*>(QStringLiteral("spectrumCloseButton"));
    QVERIFY(closeButton != nullptr);
    QTest::mouseClick(closeButton, Qt::LeftButton);
    QTRY_VERIFY(!spectrum->isShown());
    QVERIFY(spectrum->widget() != nullptr);  // hidden, never destroyed

    // Reopening puts it back as a new bottom row, so the click always has a visible effect.
    spectrum->toggleViewAction()->trigger();
    QTRY_VERIFY(spectrum->isShown());
    const auto rows = stack->model().rows();
    QCOMPARE(rows.back().panels.back(), spectrum->id());

    // Dragging its header above the TIC reorders the stack. Nothing floats.
    auto* tic = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("tic"));
    QVERIFY(tic != nullptr);
    if (!tic->isShown()) tic->toggleViewAction()->trigger();
    QTRY_VERIFY(tic->isShown());
    QTest::qWait(50);

    QWidget* header = window.findChild<QWidget*>(QStringLiteral("spectrumHeader"));
    QWidget* ticFrame = window.findChild<QWidget*>(QStringLiteral("ticFrame"));
    QVERIFY(header && ticFrame);
    dragHeader(header, header->mapToGlobal(header->rect().center()),
               ticFrame->mapToGlobal(QPoint(ticFrame->width() / 2, ticFrame->height() / 10)));

    QTRY_VERIFY(stack->model().locate(spectrum->id()).has_value());
    const auto spectrumAt = stack->model().locate(spectrum->id()).value();
    const auto ticAt = stack->model().locate(tic->id()).value();
    QVERIFY(spectrumAt.row < ticAt.row);  // dropped above the TIC
    QVERIFY(stack->model().invariantHolds());
  }

  void savedLayoutRoundTripsAndInvalidOneFallsBackToDefault()
  {
    QSettings().clear();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ux-layout.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), OpenMSViewer::TestData::experiment());

    // Session 1: arrange a two-panel row and let closeEvent persist it.
    {
      OpenMSViewer::MainWindow w;
      w.resize(1280, 820);
      w.show();
      auto* pm = w.findChild<OpenMSViewer::PeakMapWidget*>();
      w.loadFile(path);
      QTRY_VERIFY_WITH_TIMEOUT(pm->hasExperiment(), 5000);
      auto* stack = w.findChild<OpenMSViewer::RowStackWidget*>(QStringLiteral("rowStack"));
      QVERIFY(stack != nullptr);
      OpenMSViewer::LayoutModel arranged;
      arranged.appendRow(QStringLiteral("peakMap"));
      QVERIFY(arranged.applyDrop(QStringLiteral("tic"),
        {OpenMSViewer::LayoutModel::DropKind::RightOfAnchor, QStringLiteral("peakMap")}));
      arranged.appendRow(QStringLiteral("spectrum"));
      stack->setLayoutModel(arranged);
      w.close();
    }

    // Session 2: the two-panel row comes back once its data is loaded again. The
    // arrangement is remembered even though the TIC is hidden at startup for
    // want of a document — persisting only what is on screen would forget it.
    {
      OpenMSViewer::MainWindow w2;
      w2.resize(1280, 820);
      w2.show();
      auto* stack = w2.findChild<OpenMSViewer::RowStackWidget*>(QStringLiteral("rowStack"));
      QVERIFY(stack != nullptr);
      QVERIFY(!stack->model().contains(QStringLiteral("tic")));  // no data yet

      auto* pm = w2.findChild<OpenMSViewer::PeakMapWidget*>();
      w2.loadFile(path);
      QTRY_VERIFY_WITH_TIMEOUT(pm->hasExperiment(), 5000);
      QTRY_VERIFY(stack->model().contains(QStringLiteral("tic")));

      const auto tic = stack->model().locate(QStringLiteral("tic")).value();
      const auto peak = stack->model().locate(QStringLiteral("peakMap")).value();
      QCOMPARE(tic.row, peak.row);  // rejoined the peak map's row, not appended below
      QCOMPARE(stack->model().rows()[tic.row].panels[0], QStringLiteral("peakMap"));
      QCOMPARE(stack->model().rows()[tic.row].panels[1], QStringLiteral("tic"));
      QVERIFY(stack->model().invariantHolds());
    }

    // Session 3: a persisted layout the invariant forbids (three panels in a row)
    // is rejected outright rather than repaired, so startup falls back to the
    // default instead of restoring a state the model cannot represent.
    {
      QSettings s;
      s.setValue(QStringLiteral("layout/rowStack"),
                 QByteArray(R"({"layout":{"version":1,)"
                            R"("rows":[["peakMap","tic","spectrum"]]},"sizes":{}})"));
    }
    {
      OpenMSViewer::MainWindow w3;
      w3.resize(1280, 820);
      w3.show();
      auto* stack = w3.findChild<OpenMSViewer::RowStackWidget*>(QStringLiteral("rowStack"));
      QVERIFY(stack != nullptr);
      QVERIFY(stack->model().invariantHolds());
      for (const auto& row : stack->model().rows())
        QVERIFY(row.panels.size() <= std::size_t{OpenMSViewer::LayoutModel::MaxPanelsPerRow});
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
