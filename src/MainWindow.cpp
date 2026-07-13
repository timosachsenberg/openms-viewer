#include "MainWindow.h"

#include "model/FormatRegistry.h"

#include "model/RtUnit.h"

#include <OpenMS/FORMAT/BrukerTimsImagingFile.h>
#include <OpenMS/FORMAT/FileTypes.h>
#include <OpenMS/KERNEL/ConsensusMap.h>

#include "export/MzMLExportDialog.h"
#include "export/PlotExporter.h"
#include "logging/ApplicationLog.h"

#include "widgets/PeakMapWidget.h"
#include "widgets/PeakSurface3DWidget.h"
#include "widgets/ChromatogramPanelWidget.h"
#include "widgets/DataLayersWidget.h"
#include "widgets/FaimsPanelWidget.h"
#include "widgets/FeatureTableWidget.h"
#include "widgets/IdentificationTableWidget.h"
#include "widgets/SpectrumTableWidget.h"
#include "widgets/IonMobilityPanelWidget.h"
#include "widgets/ImagingPanelWidget.h"
#include "widgets/LogWidget.h"
#include "widgets/OswPanel.h"
#include "widgets/ConsensusPanel.h"
#include "widgets/MetadataBrowserWidget.h"
#include "widgets/FeatureEditDialog.h"
#include "model/ConsensusDrilldown.h"
#include "widgets/LoadingOverlayWidget.h"
#include "widgets/SpectrumWidget.h"
#include "widgets/TicWidget.h"
#include "widgets/ToastOverlay.h"
#include "widgets/WelcomeWidget.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <QWidgetAction>

#include <algorithm>
#include <utility>

namespace OpenMSViewer
{
  namespace
  {
    class AddFeatureCommand final : public QUndoCommand
    {
    public:
      AddFeatureCommand(ViewerDocument& document, double rt, double mz, double intensity, int charge)
        : document_(document), index_(document.features().size()), hadFeatureMap_(document.hasFeatures())
      {
        feature_.setRT(rt);
        feature_.setMZ(mz);
        feature_.setIntensity(static_cast<OpenMS::Feature::IntensityType>(intensity));
        feature_.setCharge(charge);
        feature_.setOverallQuality(1.0);
        feature_.ensureUniqueId();
        setText(QObject::tr("Add feature"));
      }

      void redo() override { index_ = document_.insertFeature(index_, feature_); }
      void undo() override
      {
        if (hadFeatureMap_) document_.removeFeature(index_);
        else document_.clearFeatures();
      }
      [[nodiscard]] std::size_t index() const noexcept { return index_; }

    private:
      ViewerDocument& document_;
      OpenMS::Feature feature_;
      std::size_t index_{0};
      bool hadFeatureMap_{false};
    };

    class ReplaceFeatureCommand final : public QUndoCommand
    {
    public:
      ReplaceFeatureCommand(ViewerDocument& document, std::size_t index,
                            const OpenMS::Feature& before, OpenMS::Feature after,
                            const QString& text)
        : document_(document), index_(index), before_(before), after_(std::move(after))
      {
        setText(text);
      }
      void redo() override { document_.replaceFeature(index_, after_); }
      void undo() override { document_.replaceFeature(index_, before_); }

    private:
      ViewerDocument& document_;
      std::size_t index_{0};
      OpenMS::Feature before_;
      OpenMS::Feature after_;
    };

    class DeleteFeatureCommand final : public QUndoCommand
    {
    public:
      DeleteFeatureCommand(ViewerDocument& document, std::size_t index,
                           const OpenMS::Feature& feature)
        : document_(document), index_(index), feature_(feature)
      {
        setText(QObject::tr("Delete feature"));
      }
      void redo() override { document_.removeFeature(index_); }
      void undo() override { document_.insertFeature(index_, feature_); }

    private:
      ViewerDocument& document_;
      std::size_t index_{0};
      OpenMS::Feature feature_;
    };

    // On WSLg a floating QDockWidget cannot receive the mouse grab it needs to be
    // moved or docked back (microsoft/wslg#1153); it becomes an input-dead window.
    // There we keep panels non-floating and rely on the native title bar, whose
    // in-window drag uses an X11 grab that still works under XWayland/xcb. The
    // offscreen test platform is exempt so the custom-header tests keep exercising
    // the normal desktop path.
    [[nodiscard]] bool restrictDockFloating()
    {
      const bool wsl = qEnvironmentVariableIsSet("WSL_INTEROP")
                       || qEnvironmentVariableIsSet("WSL_DISTRO_NAME");
      const bool offscreen = QGuiApplication::platformName().contains(
        QStringLiteral("offscreen"), Qt::CaseInsensitive);
      return wsl && !offscreen;
    }

    class FloatingDockTitleBar final : public QWidget
    {
    public:
      explicit FloatingDockTitleBar(QDockWidget* dock) : QWidget(dock), dock_(dock)
      {
        setObjectName(dock->objectName() + QStringLiteral("FloatingTitleBar"));
        setAccessibleName(tr("Movable title bar for %1").arg(dock->windowTitle()));
        setCursor(Qt::SizeAllCursor);
        setFixedHeight(32);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 2, 3, 2);
        layout->setSpacing(4);
        title_ = new QLabel(QStringLiteral("⠿  %1").arg(dock->windowTitle()), this);
        title_->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(title_, 1);

        dockButton_ = new QToolButton(this);
        dockButton_->setObjectName(dock->objectName() + QStringLiteral("DockButton"));
        dockButton_->setAutoRaise(true);
        connect(dockButton_, &QToolButton::clicked, dock,
                [dock] { dock->setFloating(!dock->isFloating()); });
        layout->addWidget(dockButton_);

        auto* closeButton = new QToolButton(this);
        closeButton->setObjectName(dock->objectName() + QStringLiteral("CloseButton"));
        closeButton->setAutoRaise(true);
        closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
        closeButton->setToolTip(tr("Close panel"));
        connect(closeButton, &QToolButton::clicked, dock,
                [dock] { dock->toggleViewAction()->trigger(); });
        layout->addWidget(closeButton);

        // A right-click menu keeps float/close reachable even where the small
        // buttons are awkward to hit (e.g. against a split or window edge).
        contextMenu_ = new QMenu(this);
        floatMenuAction_ = contextMenu_->addAction(QString());
        floatMenuAction_->setObjectName(dock->objectName()
                                        + QStringLiteral("FloatMenuAction"));
        connect(floatMenuAction_, &QAction::triggered, dock,
                [dock] { dock->setFloating(!dock->isFloating()); });
        auto* closeMenuAction = contextMenu_->addAction(tr("Close panel"));
        closeMenuAction->setObjectName(dock->objectName()
                                       + QStringLiteral("CloseMenuAction"));
        connect(closeMenuAction, &QAction::triggered, dock,
                [dock] { dock->toggleViewAction()->trigger(); });

        // Explicit re-docking, since this custom header's drag floats the panel
        // rather than invoking Qt's native drag-to-dock.
        auto* dockToMenu = contextMenu_->addMenu(tr("Dock to"));
        const auto addMove = [this, dockToMenu](const QString& text, Qt::DockWidgetArea area)
        {
          dockToMenu->addAction(text, this, [this, area]
          {
            if (auto* main = qobject_cast<QMainWindow*>(dock_->parentWidget()))
            {
              dock_->setFloating(false);
              main->addDockWidget(area, dock_);
              dock_->setVisible(true);
              dock_->raise();
            }
          });
        };
        addMove(tr("Left (vertical)"), Qt::LeftDockWidgetArea);
        addMove(tr("Right (vertical)"), Qt::RightDockWidgetArea);
        addMove(tr("Top"), Qt::TopDockWidgetArea);
        addMove(tr("Bottom"), Qt::BottomDockWidgetArea);

        connect(dock, &QDockWidget::windowTitleChanged, this, [this](const QString& title)
        {
          title_->setText(QStringLiteral("⠿  %1").arg(title));
          setAccessibleName(tr("Movable title bar for %1").arg(title));
        });
        connect(dock, &QDockWidget::topLevelChanged, this,
                [this](bool floating) { setFloatingState(floating); });
        setFloatingState(dock->isFloating());
      }

    protected:
      void mousePressEvent(QMouseEvent* event) override
      {
        if (event->button() != Qt::LeftButton)
        {
          QWidget::mousePressEvent(event);
          return;
        }
        dragging_ = true;
        systemMove_ = false;
        undockPending_ = false;
        pressGlobalPos_ = event->globalPosition().toPoint();
        // A docked panel is torn off into a floating window once the drag passes
        // the start-drag threshold; a floating panel starts moving immediately.
        if (dock_->isFloating())
          beginFloatingDrag(pressGlobalPos_);
        else
          undockPending_ = true;
        event->accept();
      }

      void mouseMoveEvent(QMouseEvent* event) override
      {
        if (!(event->buttons() & Qt::LeftButton) || !dragging_)
        {
          QWidget::mouseMoveEvent(event);
          return;
        }
        const QPoint globalPos = event->globalPosition().toPoint();
        if (undockPending_)
        {
          if ((globalPos - pressGlobalPos_).manhattanLength()
              < QApplication::startDragDistance())
          {
            event->accept();
            return;
          }
          undockPending_ = false;
          dock_->setFloating(true);
          dock_->raise();
          // Keep the grabbed point under the cursor without depending on the
          // freshly-floated window's frame geometry, which may not be settled yet.
          dragOffset_ = event->position().toPoint();
          QWindow* handle = dock_->windowHandle();
          const bool offscreen = QGuiApplication::platformName().contains(
            QStringLiteral("offscreen"), Qt::CaseInsensitive);
          systemMove_ = !offscreen && handle && handle->isExposed()
            && handle->startSystemMove();
        }
        if (dock_->isFloating() && !systemMove_)
        {
          const QString platform = QGuiApplication::platformName();
          const bool offscreen = platform.contains(QStringLiteral("offscreen"),
                                                    Qt::CaseInsensitive);
          const bool wayland = platform.contains(QStringLiteral("wayland"),
                                                  Qt::CaseInsensitive);
          QWindow* handle = dock_->windowHandle();
          if (!wayland && (offscreen || (handle && handle->isExposed())))
            dock_->move(globalPos - dragOffset_);
        }
        event->accept();
      }

      void mouseReleaseEvent(QMouseEvent* event) override
      {
        systemMove_ = false;
        dragging_ = false;
        undockPending_ = false;
        QWidget::mouseReleaseEvent(event);
      }

      void mouseDoubleClickEvent(QMouseEvent* event) override
      {
        if (event->button() == Qt::LeftButton)
        {
          dock_->setFloating(!dock_->isFloating());
          event->accept();
          return;
        }
        QWidget::mouseDoubleClickEvent(event);
      }

      void contextMenuEvent(QContextMenuEvent* event) override
      {
        contextMenu_->popup(event->globalPos());
        event->accept();
      }

    private:
      void beginFloatingDrag(const QPoint& globalPos)
      {
        dragOffset_ = globalPos - dock_->frameGeometry().topLeft();
        dock_->raise();
        QWindow* handle = dock_->windowHandle();
        const bool offscreen = QGuiApplication::platformName().contains(
          QStringLiteral("offscreen"), Qt::CaseInsensitive);
        systemMove_ = !offscreen && handle && handle->isExposed()
          && handle->startSystemMove();
      }

      void setFloatingState(bool floating)
      {
        setCursor(floating ? Qt::SizeAllCursor : Qt::ArrowCursor);
        dockButton_->setIcon(style()->standardIcon(
          floating ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton));
        dockButton_->setToolTip(floating ? tr("Dock panel back into the main window")
                                        : tr("Float panel in a movable window"));
        floatMenuAction_->setText(floating ? tr("Dock panel")
                                           : tr("Float panel"));
      }

      QDockWidget* dock_{nullptr};
      QLabel* title_{nullptr};
      QToolButton* dockButton_{nullptr};
      QMenu* contextMenu_{nullptr};
      QAction* floatMenuAction_{nullptr};
      QPoint dragOffset_;
      QPoint pressGlobalPos_;
      bool systemMove_{false};
      bool dragging_{false};
      bool undockPending_{false};
    };
  }

  MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
  {
    ApplicationLog::install();
    setWindowTitle(tr("OpenMS Viewer"));
    resize(1420, 920);
    setAcceptDrops(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);

    createPanels();
    featureUndoStack_ = new QUndoStack(this);
    createActions();
    createMenus();
    createToolBars();
    createStatusContext();
    statusBar()->showMessage(tr("Ready — open files, a data folder, or drop supported data here"));

    connect(welcome_, &WelcomeWidget::openRequested, this, &MainWindow::openFile);
    connect(welcome_, &WelcomeWidget::openFolderRequested, this, &MainWindow::openDataFolder);
    connect(welcome_, &WelcomeWidget::recentFileRequested, this, &MainWindow::loadFile);
    connect(featureUndoStack_, &QUndoStack::cleanChanged, this,
            [this] { updateFeatureEditState(); });
    connect(featureUndoStack_, &QUndoStack::indexChanged, this, [this]
    {
      if (selection_.feature() && *selection_.feature() >= document_.features().size())
        selection_.setFeature(std::nullopt);
      updateFeatureEditState();
    });
    connect(dataLayers_, &DataLayersWidget::visibilityChanged, this,
            [this](DataLayersWidget::Layer layer, bool visible)
    {
      switch (layer)
      {
        case DataLayersWidget::Layer::Features:
          showCentroidsAction_->setChecked(visible);
          if (!visible)
          {
            showFeatureBoundsAction_->setChecked(false);
            showFeatureHullsAction_->setChecked(false);
          }
          break;
        case DataLayersWidget::Layer::Identifications:
          showIdentificationsAction_->setChecked(visible);
          if (!visible) showIdentificationSequencesAction_->setChecked(false);
          break;
        case DataLayersWidget::Layer::Consensus:
          showConsensusAction_->setChecked(visible);
          break;
        case DataLayersWidget::Layer::OpenSwath:
          if (oswDock_->toggleViewAction()->isEnabled()) oswDock_->setVisible(visible);
          break;
        case DataLayersWidget::Layer::Primary:
        case DataLayersWidget::Layer::Count:
          break;
      }
      updateDataLayers();
    });
    connect(dataLayers_, &DataLayersWidget::removeRequested, this,
            [this](DataLayersWidget::Layer layer) { removeLayer(static_cast<int>(layer)); });
    for (QAction* action : {showCentroidsAction_, showFeatureBoundsAction_, showFeatureHullsAction_,
                            showIdentificationsAction_, showIdentificationSequencesAction_,
                            showConsensusAction_})
      connect(action, &QAction::toggled, this, [this] { updateDataLayers(); });
    connect(oswDock_, &QDockWidget::visibilityChanged, this,
            [this] { updateDataLayers(); });
    connect(loadingOverlay_, &LoadingOverlayWidget::cancelRequested,
            this, &MainWindow::cancelCurrentOperation);
    operationTimer_.setInterval(100);
    connect(&operationTimer_, &QTimer::timeout, this, [this]
    {
      if (overlayOperation_ != NoOperation && operationElapsed_.isValid())
        loadingOverlay_->setElapsed(operationElapsed_.elapsed());
    });

    connect(&loadWatcher_, &QFutureWatcher<ViewerDocument::LoadResult>::finished,
            this, &MainWindow::finishLoad);
    connect(&featureLoadWatcher_, &QFutureWatcher<ViewerDocument::FeatureLoadResult>::finished,
            this, &MainWindow::finishFeatureLoad);
    connect(&identificationLoadWatcher_, &QFutureWatcher<ViewerDocument::IdentificationLoadResult>::finished,
            this, &MainWindow::finishIdentificationLoad);
    connect(&mzMLExportWatcher_, &QFutureWatcher<MzMLExportResult>::finished,
            this, &MainWindow::finishMzMLExport);
    connect(&imagingLoadWatcher_, &QFutureWatcher<ImagingLoadResult>::finished,
            this, &MainWindow::finishImagingLoad);
    connect(&oswLoadWatcher_, &QFutureWatcher<OswLoadResult>::finished,
            this, &MainWindow::finishOswLoad);
    connect(&consensusLoadWatcher_, &QFutureWatcher<ConsensusLoadResult>::finished,
            this, &MainWindow::finishConsensusLoad);
    connect(&document_, &ViewerDocument::featuresChanged,
            this, &MainWindow::updateFeatureViews);
    connect(&document_, &ViewerDocument::identificationsChanged,
            this, &MainWindow::updateIdentificationViews);
    connect(peakMap_, &PeakMapWidget::rtActivated,
            this, &MainWindow::selectNearestSpectrum);
    connect(peakMap_, &PeakMapWidget::featureActivated,
            this, &MainWindow::selectFeature);
    connect(peakMap_, &PeakMapWidget::identificationActivated, this, [this](std::size_t index)
    {
      selectIdentification(index, 0);
    });
    connect(peakMap_, &PeakMapWidget::precursorActivated, this, &MainWindow::selectSpectrum);
    // Manual feature editing (Edit interaction mode). Each mutates the document's
    // FeatureMap; featuresChanged then refreshes the overlay + table, and the
    // "Save features as…" action persists the edits.
    connect(peakMap_, &PeakMapWidget::featureCreateRequested, this, [this](double rt, double mz)
    {
      if (featureLoadWatcher_.isRunning()) return;  // a load is replacing the map
      FeatureEditDialog dialog(rt, mz, 0.0, 1, this);
      dialog.setWindowTitle(tr("Add feature"));
      if (dialog.exec() != QDialog::Accepted) return;
      auto* command = new AddFeatureCommand(document_, dialog.rt(), dialog.mz(),
                                             dialog.intensity(), dialog.charge());
      featureUndoStack_->push(command);
      selection_.setFeature(command->index());
      notify(tr("Feature added · Ctrl+Z to undo"), ToastLevel::Info);
    });
    connect(peakMap_, &PeakMapWidget::featureMoveRequested, this,
            [this](std::size_t index, double rt, double mz)
    {
      if (featureLoadWatcher_.isRunning()) return;
      const auto before = document_.featureCopy(index);
      if (!before) return;
      OpenMS::Feature after = *before;
      const bool moved = after.getRT() != rt || after.getMZ() != mz;
      after.setRT(rt);
      after.setMZ(mz);
      if (moved) after.getConvexHulls().clear();
      featureUndoStack_->push(new ReplaceFeatureCommand(
        document_, index, *before, std::move(after), tr("Move feature")));
      selection_.setFeature(index);
    });
    connect(peakMap_, &PeakMapWidget::featureEditRequested, this, [this](std::size_t index)
    {
      // Block while a load is in flight; the dialog itself is modal, so no new load
      // can start (and swap the map) while it is open.
      if (featureLoadWatcher_.isRunning()) return;
      const auto before = document_.featureCopy(index);
      if (!before) return;
      FeatureEditDialog dialog(before->getRT(), before->getMZ(), before->getIntensity(),
                               before->getCharge(), this);
      if (dialog.exec() != QDialog::Accepted) return;
      OpenMS::Feature after = *before;
      const bool moved = after.getRT() != dialog.rt() || after.getMZ() != dialog.mz();
      after.setRT(dialog.rt());
      after.setMZ(dialog.mz());
      after.setIntensity(static_cast<OpenMS::Feature::IntensityType>(dialog.intensity()));
      after.setCharge(dialog.charge());
      if (moved) after.getConvexHulls().clear();
      featureUndoStack_->push(new ReplaceFeatureCommand(
        document_, index, *before, std::move(after), tr("Edit feature")));
      selection_.setFeature(index);
    });
    connect(peakMap_, &PeakMapWidget::featureDeleteRequested, this, [this](std::size_t index)
    {
      if (featureLoadWatcher_.isRunning()) return;
      const auto feature = document_.featureCopy(index);
      if (!feature) return;
      featureUndoStack_->push(new DeleteFeatureCommand(document_, index, *feature));
      selection_.setFeature(std::nullopt);
      notify(tr("Feature deleted · Ctrl+Z to undo"), ToastLevel::Info);
    });
    // SelectionController is the single source of truth: its signals drive the
    // per-panel leaf appliers, so any selection source updates every panel.
    connect(&selection_, &SelectionController::spectrumChanged,
            this, &MainWindow::applySpectrumSelection);
    connect(&selection_, &SelectionController::featureChanged,
            this, &MainWindow::applyFeatureSelection);
    connect(&selection_, &SelectionController::identificationChanged,
            this, &MainWindow::applyIdentificationSelection);
    // One owner for the visible region: every view-dependent panel updates here.
    connect(peakMap_, &PeakMapWidget::viewRangeChanged,
            this, &MainWindow::applyPeakMapViewRange);
    connect(peakMap_, &PeakMapWidget::zoomHistoryChanged,
            this, [this](bool available) { zoomBackAction_->setEnabled(available); });
    connect(peakMap_, &PeakMapWidget::cursorPositionChanged, this,
            [this](double rt, double mz, double intensity)
    {
      QString text = tr("RT %1 %2 · m/z %3")
        .arg(RtUnit::format(rt, rtInMinutes()), RtUnit::unit(rtInMinutes())).arg(mz, 0, 'f', 4);
      if (intensity >= 0.0) text += tr(" · I %1").arg(intensity, 0, 'g', 4);
      cursorContext_->setText(text);
    });
    connect(peakMap_, &PeakMapWidget::cursorLeft, this, [this]
    {
      cursorContext_->setText(tr("Cursor —"));
    });
    connect(tic_, &TicWidget::spectrumActivated, this,
            [this](std::size_t index) { selectSpectrum(index); });
    connect(tic_, &TicWidget::rtRangeSelected,
            peakMap_, &PeakMapWidget::setRtRange);
    connect(spectra_, &SpectrumTableWidget::spectrumActivated, this,
            [this](std::size_t spectrumIndex, int identificationIndex, int hitIndex)
    {
      selectSpectrumHit(spectrumIndex, identificationIndex, hitIndex);
    });
    connect(chromatograms_, &ChromatogramPanelWidget::rtActivated,
            this, &MainWindow::selectNearestSpectrum);
    connect(ionMobility_, &IonMobilityPanelWidget::spectrumActivated,
            this, &MainWindow::selectSpectrum);
    connect(spectrum_, &SpectrumWidget::mzViewChanged,
            ionMobility_, &IonMobilityPanelWidget::setSpectrumMzRange);
    connect(faims_, &FaimsPanelWidget::channelSelected,
            this, &MainWindow::setFaimsChannel);
    connect(faims_, &FaimsPanelWidget::spectrumActivated,
            this, &MainWindow::selectSpectrum);
    connect(imaging_, &ImagingPanelWidget::spectrumActivated,
            this, &MainWindow::selectImagingSpectrum);
    connect(features_, &FeatureTableWidget::featureActivated,
            this, &MainWindow::selectFeatureAndZoom);
    connect(identifications_, &IdentificationTableWidget::identificationActivated,
            this, &MainWindow::selectIdentification);
    connect(consensus_, &ConsensusPanel::featureActivated,
            this, &MainWindow::onConsensusFeatureActivated);
    connect(consensus_, &ConsensusPanel::featureDrillDown,
            this, &MainWindow::onConsensusFeatureDrillDown);

    QSettings settings;
    if (settings.contains(QStringLiteral("main/geometry")))
      restoreGeometry(settings.value(QStringLiteral("main/geometry")).toByteArray());
    const bool hasSavedState = settings.contains(QStringLiteral("main/state"));
    if (!hasSavedState
        || !restoreState(settings.value(QStringLiteral("main/state")).toByteArray()))
      arrangeDocksDefault();  // fresh launch or corrupt/incompatible saved layout:
                              // tables full-width in the bottom, not the side
    // A floating panel becomes a separate top-level window that, on some
    // platforms (notably WSLg/Wayland), never receives mouse input — leaving it
    // impossible to move, dock, or close. Never start in that state: re-dock any
    // panel the restored layout left floating.
    for (QDockWidget* dock : findChildren<QDockWidget*>())
      if (dock->isFloating()) dock->setFloating(false);
    const bool dark = settings.value(QStringLiteral("appearance/dark"), false).toBool();
    darkThemeAction_->setChecked(dark);
    setDarkTheme(dark);
    const bool spectrumGrid = settings.value(QStringLiteral("appearance/spectrumGrid"), true).toBool();
    showSpectrumGridAction_->setChecked(spectrumGrid);
    spectrum_->setShowGrid(spectrumGrid);
    peakMapRasterWidth_->setValue(settings.value(
      QStringLiteral("peakMap/rasterWidth"), PeakMapWidget::DefaultRasterWidth).toInt());
    recentFiles_ = settings.value(QStringLiteral("files/recent")).toStringList();
    lastPrimaryPath_ = settings.value(QStringLiteral("files/lastPrimary")).toString();
    rebuildRecentFiles();
    initializeDockPreferences(hasSavedState);
    showWelcomePage();
    updateRunContext();
    updateSpectrumControls();
    qInfo().noquote() << QStringLiteral("OpenMS Viewer ready (Qt %1)").arg(QString::fromLatin1(qVersion()));
  }

  MainWindow::~MainWindow()
  {
    // Child widgets may already be gone when QObject later deletes the undo stack.
    // QUndoStack::clear() emits during its destructor, so detach UI callbacks before
    // the QMainWindow child-destruction phase begins.
    if (featureUndoStack_) disconnect(featureUndoStack_, nullptr, this, nullptr);
    if (oswDock_) disconnect(oswDock_, nullptr, this, nullptr);
    if (mzMLCancellation_) mzMLCancellation_->store(true);
    if (loadWatcher_.isRunning()) loadWatcher_.waitForFinished();
    if (featureLoadWatcher_.isRunning()) featureLoadWatcher_.waitForFinished();
    if (identificationLoadWatcher_.isRunning()) identificationLoadWatcher_.waitForFinished();
    if (mzMLExportWatcher_.isRunning()) mzMLExportWatcher_.waitForFinished();
    if (imagingLoadWatcher_.isRunning()) imagingLoadWatcher_.waitForFinished();
    if (oswLoadWatcher_.isRunning()) oswLoadWatcher_.waitForFinished();
    if (consensusLoadWatcher_.isRunning()) consensusLoadWatcher_.waitForFinished();
  }

  void MainWindow::createPanels()
  {
    centralStack_ = new QStackedWidget(this);
    centralStack_->setObjectName(QStringLiteral("centralStack"));
    welcome_ = new WelcomeWidget(centralStack_);

    // The peak map lives inside a panel with its own control bar (populated in
    // createToolBars, once the actions exist).
    peakMapPanel_ = new QWidget(centralStack_);
    peakMapPanel_->setObjectName(QStringLiteral("peakMapPanel"));
    auto* peakMapLayout = new QVBoxLayout(peakMapPanel_);
    peakMapLayout->setContentsMargins(0, 0, 0, 0);
    peakMapLayout->setSpacing(0);
    peakMapControlBar_ = new QToolBar(peakMapPanel_);
    peakMapControlBar_->setObjectName(QStringLiteral("peakMapControlBar"));
    peakMapControlBar_->setMovable(false);
    peakMapControlBar_->setFloatable(false);
    peakMapControlBar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    peakMapScroll_ = new QScrollArea(peakMapPanel_);
    peakMapScroll_->setObjectName(QStringLiteral("peakMapScrollArea"));
    peakMapScroll_->setWidgetResizable(true);
    peakMapScroll_->setAlignment(Qt::AlignCenter);
    peakMapScroll_->setFrameShape(QFrame::NoFrame);
    peakMapScroll_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    peakMap_ = new PeakMapWidget;
    peakMapScroll_->setWidget(peakMap_);
    peakMapLayout->addWidget(peakMapControlBar_);
    peakMapLayout->addWidget(peakMapScroll_, 1);

    centralStack_->addWidget(welcome_);
    centralStack_->addWidget(peakMapPanel_);
    setCentralWidget(centralStack_);
    loadingOverlay_ = new LoadingOverlayWidget(centralStack_);
    toasts_ = new ToastOverlay(centralStack_);

    tic_ = new TicWidget(this);
    ticDock_ = new QDockWidget(tr("Total ion chromatogram"), this);
    ticDock_->setObjectName(QStringLiteral("ticDock"));
    ticDock_->setWidget(tic_);
    configureDock(ticDock_);
    addDockWidget(Qt::BottomDockWidgetArea, ticDock_);

    // The spectrum plot lives inside a panel with its own control bar.
    spectrumPanel_ = new QWidget(this);
    spectrumPanel_->setObjectName(QStringLiteral("spectrumPanel"));
    auto* spectrumLayout = new QVBoxLayout(spectrumPanel_);
    spectrumLayout->setContentsMargins(0, 0, 0, 0);
    spectrumLayout->setSpacing(0);
    spectrumControlBar_ = new QToolBar(spectrumPanel_);
    spectrumControlBar_->setObjectName(QStringLiteral("spectrumControlBar"));
    spectrumControlBar_->setMovable(false);
    spectrumControlBar_->setFloatable(false);
    spectrumControlBar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    spectrum_ = new SpectrumWidget(spectrumPanel_);
    spectrumLayout->addWidget(spectrumControlBar_);
    spectrumLayout->addWidget(spectrum_, 1);
    spectrumDock_ = new QDockWidget(tr("Spectrum"), this);
    spectrumDock_->setObjectName(QStringLiteral("spectrumDock"));
    spectrumDock_->setWidget(spectrumPanel_);
    configureDock(spectrumDock_);
    addDockWidget(Qt::BottomDockWidgetArea, spectrumDock_);
    splitDockWidget(ticDock_, spectrumDock_, Qt::Horizontal);

    chromatograms_ = new ChromatogramPanelWidget(this);
    chromatogramsDock_ = new QDockWidget(tr("Chromatograms"), this);
    chromatogramsDock_->setObjectName(QStringLiteral("chromatogramsDock"));
    chromatogramsDock_->setWidget(chromatograms_);
    configureDock(chromatogramsDock_);
    chromatogramsDock_->setMinimumWidth(620);
    addDockWidget(Qt::BottomDockWidgetArea, chromatogramsDock_);
    tabifyDockWidget(ticDock_, chromatogramsDock_);

    ionMobility_ = new IonMobilityPanelWidget(this);
    ionMobilityDock_ = new QDockWidget(tr("Ion mobility frame"), this);
    ionMobilityDock_->setObjectName(QStringLiteral("ionMobilityDock"));
    ionMobilityDock_->setWidget(ionMobility_);
    configureDock(ionMobilityDock_);
    ionMobilityDock_->setMinimumWidth(680);
    addDockWidget(Qt::BottomDockWidgetArea, ionMobilityDock_);
    tabifyDockWidget(chromatogramsDock_, ionMobilityDock_);

    imaging_ = new ImagingPanelWidget(this);
    imagingDock_ = new QDockWidget(tr("Mass-spectrometry imaging"), this);
    imagingDock_->setObjectName(QStringLiteral("imagingDock"));
    imagingDock_->setWidget(imaging_);
    configureDock(imagingDock_);
    imagingDock_->setMinimumWidth(680);
    addDockWidget(Qt::BottomDockWidgetArea, imagingDock_);
    tabifyDockWidget(ionMobilityDock_, imagingDock_);

    osw_ = new OswPanel(this);
    oswDock_ = new QDockWidget(tr("OpenSWATH results"), this);
    oswDock_->setObjectName(QStringLiteral("oswDock"));
    oswDock_->setWidget(osw_);
    configureDock(oswDock_);
    oswDock_->setMinimumWidth(720);
    addDockWidget(Qt::BottomDockWidgetArea, oswDock_);
    tabifyDockWidget(imagingDock_, oswDock_);

    consensus_ = new ConsensusPanel(this);
    consensusDock_ = new QDockWidget(tr("Consensus map"), this);
    consensusDock_->setObjectName(QStringLiteral("consensusDock"));
    consensusDock_->setWidget(consensus_);
    configureDock(consensusDock_);
    consensusDock_->setMinimumWidth(700);
    addDockWidget(Qt::BottomDockWidgetArea, consensusDock_);
    tabifyDockWidget(oswDock_, consensusDock_);

    dataLayers_ = new DataLayersWidget(this);
    dataLayersDock_ = new QDockWidget(tr("Data & layers"), this);
    dataLayersDock_->setObjectName(QStringLiteral("dataLayersDock"));
    dataLayersDock_->setWidget(dataLayers_);
    configureDock(dataLayersDock_);
    dataLayersDock_->setMinimumWidth(380);
    addDockWidget(Qt::RightDockWidgetArea, dataLayersDock_);

    metadata_ = new MetadataBrowserWidget(this);
    metadataDock_ = new QDockWidget(tr("Metadata"), this);
    metadataDock_->setObjectName(QStringLiteral("metadataDock"));
    metadataDock_->setWidget(metadata_);
    configureDock(metadataDock_);
    metadataDock_->setMinimumWidth(360);
    addDockWidget(Qt::RightDockWidgetArea, metadataDock_);

    log_ = new LogWidget(this);
    logDock_ = new QDockWidget(tr("Application log"), this);
    logDock_->setObjectName(QStringLiteral("logDock"));
    logDock_->setWidget(log_);
    configureDock(logDock_);
    logDock_->setMinimumWidth(620);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
    tabifyDockWidget(imagingDock_, logDock_);

    features_ = new FeatureTableWidget(this);
    featuresDock_ = new QDockWidget(tr("Features"), this);
    featuresDock_->setObjectName(QStringLiteral("featuresDock"));
    featuresDock_->setWidget(features_);
    configureDock(featuresDock_);
    featuresDock_->setMinimumWidth(480);
    addDockWidget(Qt::RightDockWidgetArea, featuresDock_);

    identifications_ = new IdentificationTableWidget(this);
    identificationsDock_ = new QDockWidget(tr("Identifications"), this);
    identificationsDock_->setObjectName(QStringLiteral("identificationsDock"));
    identificationsDock_->setWidget(identifications_);
    configureDock(identificationsDock_);
    identificationsDock_->setMinimumWidth(560);
    addDockWidget(Qt::RightDockWidgetArea, identificationsDock_);
    tabifyDockWidget(featuresDock_, identificationsDock_);

    spectra_ = new SpectrumTableWidget(this);
    spectraDock_ = new QDockWidget(tr("Spectra"), this);
    spectraDock_->setObjectName(QStringLiteral("spectraDock"));
    spectraDock_->setWidget(spectra_);
    configureDock(spectraDock_);
    spectraDock_->setMinimumWidth(580);
    addDockWidget(Qt::RightDockWidgetArea, spectraDock_);
    tabifyDockWidget(featuresDock_, spectraDock_);

    faims_ = new FaimsPanelWidget(this);
    faimsDock_ = new QDockWidget(tr("FAIMS compensation voltages"), this);
    faimsDock_->setObjectName(QStringLiteral("faimsDock"));
    faimsDock_->setWidget(faims_);
    configureDock(faimsDock_);
    faimsDock_->setMinimumWidth(520);
    addDockWidget(Qt::RightDockWidgetArea, faimsDock_);
    tabifyDockWidget(spectraDock_, faimsDock_);
    tabifyDockWidget(faimsDock_, metadataDock_);  // join the right-side tab group
  }

  void MainWindow::createActions()
  {
    openAction_ = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open…"), this);
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::openFile);

    // Directory-shaped inputs (Bruker .d, Parquet bundles) need a folder picker
    // rather than the file dialog above.
    openBrukerAction_ = new QAction(tr("Open data folder…"), this);
    connect(openBrukerAction_, &QAction::triggered, this, &MainWindow::openDataFolder);

    reloadAction_ = new QAction(style()->standardIcon(QStyle::SP_BrowserReload), tr("Reload"), this);
    reloadAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    reloadAction_->setEnabled(false);
    connect(reloadAction_, &QAction::triggered, this, &MainWindow::reloadLastFile);

    closeDataAction_ = new QAction(tr("Close data"), this);
    closeDataAction_->setShortcut(QKeySequence::Close);
    closeDataAction_->setEnabled(false);
    connect(closeDataAction_, &QAction::triggered, this, &MainWindow::closeData);

    exportMzMLAction_ = new QAction(tr("Export filtered mzML…"), this);
    exportMzMLAction_->setEnabled(false);
    connect(exportMzMLAction_, &QAction::triggered, this, &MainWindow::exportMzML);

    saveFeaturesAction_ = new QAction(tr("Save features"), this);
    saveFeaturesAction_->setShortcut(QKeySequence::Save);
    saveFeaturesAction_->setEnabled(false);
    connect(saveFeaturesAction_, &QAction::triggered, this, [this] { (void)saveFeatures(false); });
    saveFeaturesAsAction_ = new QAction(tr("Save features as…"), this);
    saveFeaturesAsAction_->setEnabled(false);
    connect(saveFeaturesAsAction_, &QAction::triggered, this, [this] { (void)saveFeatures(true); });
    saveIdentificationsAction_ = new QAction(tr("Save identifications as…"), this);
    saveIdentificationsAction_->setEnabled(false);
    connect(saveIdentificationsAction_, &QAction::triggered, this, &MainWindow::saveIdentifications);
    saveConsensusAction_ = new QAction(tr("Save consensus map as…"), this);
    saveConsensusAction_->setEnabled(false);
    connect(saveConsensusAction_, &QAction::triggered, this, &MainWindow::saveConsensus);

    undoFeatureAction_ = featureUndoStack_->createUndoAction(this, tr("Undo"));
    redoFeatureAction_ = featureUndoStack_->createRedoAction(this, tr("Redo"));
    undoFeatureAction_->setObjectName(QStringLiteral("undoFeatureEdit"));
    redoFeatureAction_->setObjectName(QStringLiteral("redoFeatureEdit"));
    undoFeatureAction_->setShortcut(QKeySequence::Undo);
    redoFeatureAction_->setShortcut(QKeySequence::Redo);

    zoomBackAction_ = new QAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("Previous view"), this);
    zoomBackAction_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    zoomBackAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    zoomBackAction_->setEnabled(false);
    connect(zoomBackAction_, &QAction::triggered, peakMap_, &PeakMapWidget::zoomBack);
    peakMapPanel_->addAction(zoomBackAction_);

    resetViewAction_ = new QAction(style()->standardIcon(QStyle::SP_DialogResetButton),
                                   tr("Reset peak-map view"), this);
    resetViewAction_->setShortcut(Qt::Key_Home);
    resetViewAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(resetViewAction_, &QAction::triggered, peakMap_, &PeakMapWidget::resetView);
    peakMap_->addAction(resetViewAction_);

    auto* zoomInAction = new QAction(tr("Zoom in"), this);
    zoomInAction->setShortcuts({QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal)});
    zoomInAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(zoomInAction, &QAction::triggered, peakMap_, &PeakMapWidget::zoomIn);
    peakMapPanel_->addAction(zoomInAction);

    auto* zoomOutAction = new QAction(tr("Zoom out"), this);
    zoomOutAction->setShortcut(Qt::Key_Minus);
    zoomOutAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(zoomOutAction, &QAction::triggered, peakMap_, &PeakMapWidget::zoomOut);
    peakMapPanel_->addAction(zoomOutAction);

    auto addPanAction = [this](int key, auto slot)
    {
      auto* action = new QAction(this);
      action->setShortcut(key);
      action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
      connect(action, &QAction::triggered, peakMap_, slot);
      peakMap_->addAction(action);
    };
    addPanAction(Qt::Key_Left, &PeakMapWidget::panLeft);
    addPanAction(Qt::Key_Right, &PeakMapWidget::panRight);
    addPanAction(Qt::Key_Up, &PeakMapWidget::panUp);
    addPanAction(Qt::Key_Down, &PeakMapWidget::panDown);

    swapAxesAction_ = new QAction(tr("Swap RT and m/z axes"), this);
    swapAxesAction_->setCheckable(true);
    swapAxesAction_->setChecked(true);
    connect(swapAxesAction_, &QAction::toggled, peakMap_, &PeakMapWidget::setAxesSwapped);

    surface3DAction_ = new QAction(tr("3-D surface of the current view…"), this);
    surface3DAction_->setStatusTip(
      tr("Show the zoomed region as a rotatable 3-D intensity surface (zoom in first)"));
    connect(surface3DAction_, &QAction::triggered, this, &MainWindow::show3DSurface);

    darkThemeAction_ = new QAction(tr("Dark theme"), this);
    darkThemeAction_->setCheckable(true);
    connect(darkThemeAction_, &QAction::toggled, this, &MainWindow::setDarkTheme);

    showMinimapAction_ = new QAction(tr("Peak-map minimap"), this);
    showMinimapAction_->setCheckable(true);
    showMinimapAction_->setChecked(true);
    connect(showMinimapAction_, &QAction::toggled,
            peakMap_, &PeakMapWidget::setShowMinimap);

    // Single RT-unit control shared by every RT panel (always reachable, unlike a
    // per-panel checkbox that hides with the chromatogram dock), so units can't diverge.
    rtInMinutesAction_ = new QAction(tr("RT in minutes"), this);
    rtInMinutesAction_->setCheckable(true);
    connect(rtInMinutesAction_, &QAction::toggled, tic_, &TicWidget::setRtInMinutes);
    connect(rtInMinutesAction_, &QAction::toggled, chromatograms_, &ChromatogramPanelWidget::setRtInMinutes);
    connect(rtInMinutesAction_, &QAction::toggled, peakMap_, &PeakMapWidget::setRtInMinutes);
    connect(rtInMinutesAction_, &QAction::toggled, spectrum_, &SpectrumWidget::setRtInMinutes);
    connect(rtInMinutesAction_, &QAction::toggled, features_, &FeatureTableWidget::setRtInMinutes);
    connect(rtInMinutesAction_, &QAction::toggled, spectra_, &SpectrumTableWidget::setRtInMinutes);
    connect(rtInMinutesAction_, &QAction::toggled, identifications_, &IdentificationTableWidget::setRtInMinutes);
    connect(rtInMinutesAction_, &QAction::toggled, consensus_, &ConsensusPanel::setRtInMinutes);
    connect(rtInMinutesAction_, &QAction::toggled, faims_, &FaimsPanelWidget::setRtInMinutes);
    connect(rtInMinutesAction_, &QAction::toggled, ionMobility_, &IonMobilityPanelWidget::setRtInMinutes);
    connect(rtInMinutesAction_, &QAction::toggled, osw_, &OswPanel::setRtInMinutes);
    connect(rtInMinutesAction_, &QAction::toggled, metadata_, &MetadataBrowserWidget::setRtInMinutes);
    // surface3D_ is created lazily (show3DSurface), so it cannot be wired here —
    // connecting a null receiver silently drops the connection. It is connected
    // and seeded with the current unit when it is constructed instead.
    // Refresh MainWindow's own cached RT readouts (status bar / context labels).
    connect(rtInMinutesAction_, &QAction::toggled, this, [this]
    {
      updateSpectrumStatus();
      if (spectrumSearch_)
        spectrumSearch_->setPlaceholderText(
          tr("Native ID or RT (%1)…").arg(RtUnit::unit(rtInMinutes())));
      if (peakMap_ && peakMap_->hasExperiment()) applyPeakMapViewRange(peakMap_->viewRange());
    });

    relativeIntensityAction_ = new QAction(tr("Relative spectrum intensity"), this);
    relativeIntensityAction_->setCheckable(true);
    relativeIntensityAction_->setChecked(true);
    connect(relativeIntensityAction_, &QAction::toggled,
            spectrum_, &SpectrumWidget::setRelativeIntensity);

    autoYScaleAction_ = new QAction(tr("Auto-scale intensity axis"), this);
    autoYScaleAction_->setCheckable(true);
    autoYScaleAction_->setChecked(true);
    autoYScaleAction_->setStatusTip(
      tr("Fit the intensity axis to the tallest visible peak; when off, hold the base-peak scale"));
    connect(autoYScaleAction_, &QAction::toggled,
            spectrum_, &SpectrumWidget::setAutoYScale);

    showCentroidsAction_ = new QAction(tr("Feature centroids"), this);
    showCentroidsAction_->setCheckable(true);
    showCentroidsAction_->setChecked(true);
    showCentroidsAction_->setEnabled(false);
    connect(showCentroidsAction_, &QAction::toggled,
            peakMap_, &PeakMapWidget::setShowFeatureCentroids);

    showFeatureBoundsAction_ = new QAction(tr("Feature bounding boxes"), this);
    showFeatureBoundsAction_->setCheckable(true);
    showFeatureBoundsAction_->setEnabled(false);
    connect(showFeatureBoundsAction_, &QAction::toggled,
            peakMap_, &PeakMapWidget::setShowFeatureBounds);

    showFeatureHullsAction_ = new QAction(tr("Feature convex hulls"), this);
    showFeatureHullsAction_->setCheckable(true);
    showFeatureHullsAction_->setEnabled(false);
    connect(showFeatureHullsAction_, &QAction::toggled,
            peakMap_, &PeakMapWidget::setShowFeatureHulls);

    showIdentificationsAction_ = new QAction(tr("Identification markers"), this);
    showIdentificationsAction_->setCheckable(true);
    showIdentificationsAction_->setChecked(true);
    showIdentificationsAction_->setEnabled(false);
    connect(showIdentificationsAction_, &QAction::toggled,
            peakMap_, &PeakMapWidget::setShowIdentifications);

    showIdentificationSequencesAction_ = new QAction(tr("Identification sequences"), this);
    showIdentificationSequencesAction_->setCheckable(true);
    showIdentificationSequencesAction_->setEnabled(false);
    connect(showIdentificationSequencesAction_, &QAction::toggled,
            peakMap_, &PeakMapWidget::setShowIdentificationSequences);

    showConsensusAction_ = new QAction(tr("Consensus features"), this);
    showConsensusAction_->setCheckable(true);
    showConsensusAction_->setChecked(true);
    showConsensusAction_->setEnabled(false);
    connect(showConsensusAction_, &QAction::toggled,
            peakMap_, &PeakMapWidget::setShowConsensus);

    showPrecursorsAction_ = new QAction(tr("MS/MS precursors + isolation windows"), this);
    showPrecursorsAction_->setCheckable(true);
    showPrecursorsAction_->setChecked(false);
    showPrecursorsAction_->setEnabled(false);
    connect(showPrecursorsAction_, &QAction::toggled,
            peakMap_, &PeakMapWidget::setShowPrecursors);

    clearFeatureOverlayAction_ = new QAction(tr("Clear feature overlay"), this);
    connect(clearFeatureOverlayAction_, &QAction::triggered, this, [this]
    {
      if (!confirmFeatureChanges(tr("remove the feature layer"))) return;
      document_.clearFeatures();
      featureUndoStack_->clear();
      featureSavePath_.clear();
      statusBar()->showMessage(tr("Feature overlay cleared"), 3000);
      updateRunContext();
    });
    clearIdentificationOverlayAction_ = new QAction(tr("Clear identification overlay"), this);
    connect(clearIdentificationOverlayAction_, &QAction::triggered, this, [this]
    {
      document_.clearIdentifications();
      statusBar()->showMessage(tr("Identification overlay cleared"), 3000);
      updateRunContext();
    });

    annotateSpectrumAction_ = new QAction(tr("Annotate identified spectra"), this);
    annotateSpectrumAction_->setCheckable(true);
    annotateSpectrumAction_->setChecked(true);
    connect(annotateSpectrumAction_, &QAction::toggled, this, [this](bool enabled)
    {
      spectrum_->setAnnotationEnabled(enabled);
      if (enabled) updateSpectrumAnnotation();
    });

    mirrorSpectrumAction_ = new QAction(tr("Mirror theoretical spectrum"), this);
    mirrorSpectrumAction_->setCheckable(true);
    connect(mirrorSpectrumAction_, &QAction::toggled,
            spectrum_, &SpectrumWidget::setMirrorMode);

    showUnmatchedIonsAction_ = new QAction(tr("Show unmatched theoretical ions"), this);
    showUnmatchedIonsAction_->setCheckable(true);
    showUnmatchedIonsAction_->setChecked(true);
    connect(showUnmatchedIonsAction_, &QAction::toggled,
            spectrum_, &SpectrumWidget::setShowUnmatchedTheoretical);

    measureSpectrumAction_ = new QAction(tr("Measure peak distance"), this);
    measureSpectrumAction_->setCheckable(true);
    measureSpectrumAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(measureSpectrumAction_, &QAction::toggled,
            spectrum_, &SpectrumWidget::setMeasurementMode);

    labelSpectrumAction_ = new QAction(tr("Label peaks"), this);
    labelSpectrumAction_->setCheckable(true);
    labelSpectrumAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    labelSpectrumAction_->setStatusTip(
      tr("Click a peak to add, edit, or remove a free-text label (clear the text to remove)"));
    connect(labelSpectrumAction_, &QAction::toggled, spectrum_, &SpectrumWidget::setLabelMode);

    // The two click tools are mutually exclusive but either can be off.
    auto* spectrumToolGroup = new QActionGroup(this);
    spectrumToolGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    spectrumToolGroup->addAction(measureSpectrumAction_);
    spectrumToolGroup->addAction(labelSpectrumAction_);

    showMzLabelsAction_ = new QAction(tr("Automatic m/z labels"), this);
    showMzLabelsAction_->setObjectName(QStringLiteral("showMzLabels"));
    showMzLabelsAction_->setCheckable(true);
    showMzLabelsAction_->setChecked(true);
    connect(showMzLabelsAction_, &QAction::toggled,
            spectrum_, &SpectrumWidget::setShowMzLabels);

    showSpectrumGridAction_ = new QAction(tr("Show gridlines"), this);
    showSpectrumGridAction_->setCheckable(true);
    showSpectrumGridAction_->setChecked(true);
    connect(showSpectrumGridAction_, &QAction::toggled,
            spectrum_, &SpectrumWidget::setShowGrid);

    resetSpectrumViewAction_ = new QAction(tr("Reset spectrum m/z view"), this);
    connect(resetSpectrumViewAction_, &QAction::triggered,
            spectrum_, &SpectrumWidget::resetMzView);

    clearSpectrumMeasurementsAction_ = new QAction(tr("Clear spectrum measurements"), this);
    connect(clearSpectrumMeasurementsAction_, &QAction::triggered,
            spectrum_, &SpectrumWidget::clearMeasurements);

    clearSpectrumLabelsAction_ = new QAction(tr("Clear spectrum labels"), this);
    connect(clearSpectrumLabelsAction_, &QAction::triggered,
            spectrum_, &SpectrumWidget::clearLabels);

    spectrumFirstAction_ = new QAction(style()->standardIcon(QStyle::SP_MediaSkipBackward),
                                       QStringLiteral("⏮"), this);
    spectrumPreviousAction_ = new QAction(style()->standardIcon(QStyle::SP_MediaSeekBackward),
                                          QStringLiteral("◀"), this);
    spectrumNextAction_ = new QAction(style()->standardIcon(QStyle::SP_MediaSeekForward),
                                      QStringLiteral("▶"), this);
    spectrumLastAction_ = new QAction(style()->standardIcon(QStyle::SP_MediaSkipForward),
                                      QStringLiteral("⏭"), this);
    spectrumFirstAction_->setToolTip(tr("First spectrum"));
    spectrumPreviousAction_->setToolTip(tr("Previous spectrum"));
    spectrumNextAction_->setToolTip(tr("Next spectrum"));
    spectrumLastAction_->setToolTip(tr("Last spectrum"));
    spectrumFirstAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home));
    spectrumPreviousAction_->setShortcut(QKeySequence(Qt::Key_PageUp));
    spectrumNextAction_->setShortcut(QKeySequence(Qt::Key_PageDown));
    spectrumLastAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_End));
    // PageUp/PageDown/Ctrl+Home/Ctrl+End navigate spectra window-wide (default
    // WindowShortcut context) by design: table selection drives spectrum
    // selection, so these keys page through spectra even when a table has focus.
    connect(spectrumFirstAction_, &QAction::triggered, this,
            [this] { selectEdgeSpectrum(false, navigationMsLevel()); });
    connect(spectrumPreviousAction_, &QAction::triggered, this,
            [this] { navigateSpectrum(-1, navigationMsLevel()); });
    connect(spectrumNextAction_, &QAction::triggered, this,
            [this] { navigateSpectrum(1, navigationMsLevel()); });
    connect(spectrumLastAction_, &QAction::triggered, this,
            [this] { selectEdgeSpectrum(true, navigationMsLevel()); });

    auto* fullscreenAction = new QAction(tr("Fullscreen"), this);
    fullscreenAction->setShortcut(Qt::Key_F11);
    connect(fullscreenAction, &QAction::triggered, this, [this]
    {
      isFullScreen() ? showNormal() : showFullScreen();
    });

    addAction(fullscreenAction);
  }

  void MainWindow::createMenus()
  {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openAction_);
    fileMenu->addAction(openBrukerAction_);
    fileMenu->addAction(reloadAction_);
    recentFilesMenu_ = fileMenu->addMenu(tr("Open recent"));
    fileMenu->addAction(closeDataAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(saveFeaturesAction_);
    fileMenu->addAction(saveFeaturesAsAction_);
    fileMenu->addAction(saveIdentificationsAction_);
    fileMenu->addAction(saveConsensusAction_);
    fileMenu->addSeparator();
    auto* exportMenu = fileMenu->addMenu(tr("Export"));
    exportMenu->addAction(exportMzMLAction_);
    auto* plotMenu = exportMenu->addMenu(tr("Plot as image (PNG/SVG)"));
    plotMenu->addAction(tr("Peak map…"), this,
                        [this] { savePlot(peakMap_, QStringLiteral("peak_map.png")); });
    plotMenu->addAction(tr("Spectrum…"), this,
                        [this] { savePlot(spectrum_, QStringLiteral("spectrum.png")); });
    plotMenu->addAction(tr("TIC/BPC…"), this,
                        [this] { savePlot(tic_, QStringLiteral("tic.png")); });
    plotMenu->addAction(tr("Chromatograms…"), this,
                        [this] { savePlot(chromatograms_->plot(), QStringLiteral("chromatograms.png")); });
    plotMenu->addAction(tr("Ion mobility…"), this,
                        [this] { savePlot(ionMobility_->plot(), QStringLiteral("ion_mobility.png")); });
    plotMenu->addAction(tr("FAIMS traces…"), this,
                        [this] { savePlot(faims_->plot(), QStringLiteral("faims_traces.png")); });
    plotMenu->addAction(tr("Ion image…"), this,
                        [this] { savePlot(imaging_->imageWidget(), QStringLiteral("ion_image.png")); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(undoFeatureAction_);
    editMenu->addAction(redoFeatureAction_);

    // View holds only global chrome; peak-map and spectrum options now live in
    // the peak-map / spectrum panel control bars.
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(darkThemeAction_);
    viewMenu->addAction(rtInMinutesAction_);
    auto* layoutMenu = viewMenu->addMenu(tr("Layout"));
    const auto addPreset = [this, layoutMenu](const QString& text, LayoutPreset preset, const QString& objectName)
    {
      QAction* action = layoutMenu->addAction(text, this, [this, preset] { applyLayoutPreset(preset); });
      action->setObjectName(objectName);
    };
    addPreset(tr("Overview (reset)"), LayoutPreset::Overview, QStringLiteral("layoutOverview"));
    addPreset(tr("Identification"), LayoutPreset::Identification, QStringLiteral("layoutIdentification"));
    addPreset(tr("Imaging"), LayoutPreset::Imaging, QStringLiteral("layoutImaging"));
    addPreset(tr("DIA / OpenSWATH"), LayoutPreset::Dia, QStringLiteral("layoutDia"));

    // Explicit re-docking. Drag-to-dock is blocked by the custom header (desktop)
    // and by WSLg, so offer every panel a Left/Right (vertical column), Top, or
    // Bottom placement from the menu.
    auto* dockMenu = viewMenu->addMenu(tr("Dock panel"));
    const std::pair<QDockWidget*, QString> layoutPanels[] = {
      {spectrumDock_, tr("Spectrum")}, {ticDock_, tr("Total ion chromatogram")},
      {featuresDock_, tr("Features")}, {identificationsDock_, tr("Identifications")},
      {spectraDock_, tr("Spectra")}, {chromatogramsDock_, tr("Chromatograms")},
      {dataLayersDock_, tr("Data & layers")},
      {ionMobilityDock_, tr("Ion mobility")}, {faimsDock_, tr("FAIMS")},
      {imagingDock_, tr("Imaging")}, {oswDock_, tr("OpenSWATH")},
      {consensusDock_, tr("Consensus")}, {metadataDock_, tr("Metadata")},
      {logDock_, tr("Log")}};
    for (const auto& panel : layoutPanels)
    {
      QDockWidget* dock = panel.first;
      auto* sub = dockMenu->addMenu(panel.second);
      const auto add = [&](const QString& text, const QString& suffix, Qt::DockWidgetArea area)
      {
        QAction* action = sub->addAction(text, this, [this, dock, area] { moveDock(dock, area); });
        action->setObjectName(dock->objectName() + suffix);
      };
      add(tr("Dock left (vertical)"), QStringLiteral("MoveLeft"), Qt::LeftDockWidgetArea);
      add(tr("Dock right (vertical)"), QStringLiteral("MoveRight"), Qt::RightDockWidgetArea);
      add(tr("Dock top"), QStringLiteral("MoveTop"), Qt::TopDockWidgetArea);
      add(tr("Dock bottom"), QStringLiteral("MoveBottom"), Qt::BottomDockWidgetArea);
    }

    viewMenu->addSeparator();
    viewMenu->addAction(ticDock_->toggleViewAction());
    viewMenu->addAction(spectrumDock_->toggleViewAction());
    viewMenu->addAction(featuresDock_->toggleViewAction());
    viewMenu->addAction(identificationsDock_->toggleViewAction());
    viewMenu->addAction(spectraDock_->toggleViewAction());
    viewMenu->addAction(chromatogramsDock_->toggleViewAction());
    viewMenu->addAction(dataLayersDock_->toggleViewAction());
    viewMenu->addAction(ionMobilityDock_->toggleViewAction());
    viewMenu->addAction(faimsDock_->toggleViewAction());
    viewMenu->addAction(imagingDock_->toggleViewAction());
    viewMenu->addAction(oswDock_->toggleViewAction());
    viewMenu->addAction(consensusDock_->toggleViewAction());
    viewMenu->addAction(metadataDock_->toggleViewAction());
    viewMenu->addAction(logDock_->toggleViewAction());

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("Getting started…"), this, [this]
    {
      QMessageBox::information(this, tr("Getting started"),
        tr("1. Open a spectra run, imaging dataset, or supported data folder.\n"
           "2. Open matching FeatureXML, identification, consensus, or OSW result files as layers.\n"
           "3. Use Data & layers to see each source, hide overlays, or remove them.\n\n"
           "Feature edits are undoable with Ctrl+Z. Modified features are marked in the window "
           "title and are offered for saving before they are replaced or closed.\n\n"
           "Tip: you can select several related files in one Open dialog, or drag them onto the window."));
    });
    auto* shortcutsAction = new QAction(tr("Plot interactions and shortcuts…"), this);
    shortcutsAction->setShortcut(QKeySequence::HelpContents);
    addAction(shortcutsAction);
    helpMenu->addAction(shortcutsAction);
    connect(shortcutsAction, &QAction::triggered, this, [this]
    {
      QMessageBox::information(this, tr("Plot interactions and shortcuts"),
        tr("Peak map\n"
           "  Z: zoom mode   P: pan mode   M: measure mode\n"
           "  Wheel: zoom at cursor   Double-click/Home: reset view\n"
           "  Alt/Ctrl-drag: temporary pan   Shift-drag: temporary measure\n"
           "  Alt+Left: previous view\n\n"
           "Spectrum and traces\n"
           "  Wheel: zoom   drag: select range   double-click: reset\n"
           "  Ctrl+M: measure spectrum peak distance\n\n"
           "Files and window\n"
           "  Ctrl+O: open   Ctrl+S: save features   Ctrl+Z/Y: undo/redo feature edit\n"
           "  Ctrl+R: reload   Ctrl+W: close data   F11: fullscreen"));
    });
    helpMenu->addAction(tr("About OpenMS Viewer"), this, [this]
    {
      QMessageBox::about(this, tr("About OpenMS Viewer"),
        tr("OpenMS Viewer %1\nA standalone Qt 6 viewer linked against OpenMS.")
          .arg(QApplication::applicationVersion()));
    });
  }

  void MainWindow::createToolBars()
  {
    auto* fileBar = addToolBar(tr("File and peak map"));
    fileBar->setObjectName(QStringLiteral("fileToolbar"));
    fileBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    fileBar->addAction(openAction_);
    fileBar->addAction(reloadAction_);
    // ---- Peak-map options live in the peak-map panel's control bar ----
    peakMapControlBar_->addAction(zoomBackAction_);
    peakMapControlBar_->addAction(resetViewAction_);

    auto* modeLabel = new QLabel(tr("Mode"), peakMapControlBar_);
    modeLabel->setObjectName(QStringLiteral("peakMapModeLabel"));
    peakMapControlBar_->addWidget(modeLabel);
    auto* interactionMode = new QComboBox(peakMapControlBar_);
    interactionMode->setObjectName(QStringLiteral("peakMapInteractionMode"));
    interactionMode->addItems({tr("Zoom (Z)"), tr("Pan (P)"), tr("Measure (M)"), tr("Edit features (E)")});
    interactionMode->setToolTip(tr("Choose what dragging in the peak map does"));
    interactionMode->setAccessibleName(tr("Peak-map interaction mode"));
    peakMapControlBar_->addWidget(interactionMode);
    connect(interactionMode, qOverload<int>(&QComboBox::currentIndexChanged),
            peakMap_, &PeakMapWidget::setInteractionMode);
    connect(peakMap_, &PeakMapWidget::interactionModeChanged, interactionMode,
            [interactionMode](int modeIndex)
            {
              const QSignalBlocker blocker(interactionMode);
              interactionMode->setCurrentIndex(modeIndex);
            });

    auto* colorLabel = new QLabel(tr("Color"), peakMapControlBar_);
    colorLabel->setObjectName(QStringLiteral("peakMapColorLabel"));
    peakMapControlBar_->addWidget(colorLabel);
    auto* colorMap = new QComboBox(peakMapControlBar_);
    colorMap->setObjectName(QStringLiteral("peakMapColorMap"));
    colorMap->addItems({tr("Viridis"), tr("Plasma"), tr("Inferno"), tr("Magma"),
                        tr("Jet"), tr("Hot"), tr("Grayscale")});
    colorMap->setMaximumWidth(110);
    colorMap->setAccessibleName(tr("Peak-map color map"));
    peakMapControlBar_->addWidget(colorMap);
    connect(colorMap, qOverload<int>(&QComboBox::currentIndexChanged),
            peakMap_, &PeakMapWidget::setColorMap);
    connect(colorMap, qOverload<int>(&QComboBox::currentIndexChanged),
            ionMobility_, &IonMobilityPanelWidget::setColorMap);
    connect(colorMap, qOverload<int>(&QComboBox::currentIndexChanged),
            faims_, &FaimsPanelWidget::setColorMap);

    auto* scale = new QComboBox(peakMapControlBar_);
    scale->setObjectName(QStringLiteral("peakMapIntensityScale"));
    scale->addItems({tr("Equalize"), tr("Log"), tr("Square root"), tr("Linear")});
    scale->setToolTip(tr("Peak-map intensity normalization"));
    scale->setAccessibleName(tr("Peak-map intensity scale"));
    peakMapControlBar_->addWidget(scale);
    connect(scale, qOverload<int>(&QComboBox::currentIndexChanged),
            peakMap_, &PeakMapWidget::setIntensityScale);

    auto* rasterLabel = new QLabel(tr("Raster max width"), peakMapControlBar_);
    rasterLabel->setObjectName(QStringLiteral("peakMapRasterLabel"));
    peakMapControlBar_->addWidget(rasterLabel);
    peakMapRasterWidth_ = new QSpinBox(peakMapControlBar_);
    peakMapRasterWidth_->setObjectName(QStringLiteral("peakMapRasterWidth"));
    peakMapRasterWidth_->setRange(PeakMapWidget::MinimumRasterWidth,
                                  PeakMapWidget::MaximumRasterWidth);
    peakMapRasterWidth_->setSingleStep(128);
    peakMapRasterWidth_->setValue(PeakMapWidget::DefaultRasterWidth);
    peakMapRasterWidth_->setSuffix(tr(" px"));
    peakMapRasterWidth_->setKeyboardTracking(false);
    peakMapRasterWidth_->setMaximumWidth(105);
    peakMapRasterWidth_->setToolTip(
      tr("Maximum peak-map raster size; smaller viewports render a smaller 1:1 raster"));
    peakMapRasterWidth_->setAccessibleName(tr("Peak-map maximum raster width"));
    peakMapControlBar_->addWidget(peakMapRasterWidth_);
    connect(peakMapRasterWidth_, qOverload<int>(&QSpinBox::valueChanged),
            peakMap_, &PeakMapWidget::setRasterWidth);

    auto* display = new QToolButton(peakMapControlBar_);
    display->setObjectName(QStringLiteral("peakMapDisplayOptions"));
    display->setText(tr("Display"));
    display->setPopupMode(QToolButton::InstantPopup);
    display->setAccessibleName(tr("Peak-map display options"));
    auto* displayMenu = new QMenu(display);
    displayMenu->addAction(swapAxesAction_);
    displayMenu->addAction(showMinimapAction_);
    displayMenu->addSeparator();
    displayMenu->addAction(surface3DAction_);
    display->setMenu(displayMenu);
    peakMapControlBar_->addWidget(display);

    auto* overlays = new QToolButton(peakMapControlBar_);
    overlays->setObjectName(QStringLiteral("peakMapOverlayOptions"));
    overlays->setText(tr("Overlays"));
    overlays->setPopupMode(QToolButton::InstantPopup);
    overlays->setAccessibleName(tr("Peak-map overlay options"));
    auto* overlayMenu = new QMenu(overlays);
    overlayMenu->addAction(showCentroidsAction_);
    overlayMenu->addAction(showFeatureBoundsAction_);
    overlayMenu->addAction(showFeatureHullsAction_);
    overlayMenu->addSeparator();
    overlayMenu->addAction(showIdentificationsAction_);
    overlayMenu->addAction(showIdentificationSequencesAction_);
    overlayMenu->addSeparator();
    overlayMenu->addAction(showPrecursorsAction_);
    overlayMenu->addAction(showConsensusAction_);
    overlayMenu->addSeparator();
    overlayMenu->addAction(clearFeatureOverlayAction_);
    overlayMenu->addAction(clearIdentificationOverlayAction_);
    overlays->setMenu(overlayMenu);
    peakMapControlBar_->addWidget(overlays);

    // ---- Spectrum options live in the spectrum panel's control bar ----
    const auto addNavigationButton = [this](QAction* action, const QString& glyph,
                                            const QString& accessibleName)
    {
      auto* button = new QToolButton(spectrumControlBar_);
      button->setDefaultAction(action);
      button->setText(glyph);
      button->setToolTip(accessibleName);
      button->setAccessibleName(accessibleName);
      button->setToolButtonStyle(Qt::ToolButtonTextOnly);
      spectrumControlBar_->addWidget(button);
    };
    addNavigationButton(spectrumFirstAction_, QStringLiteral("⏮"), tr("First spectrum"));
    addNavigationButton(spectrumPreviousAction_, QStringLiteral("◀"), tr("Previous spectrum"));
    addNavigationButton(spectrumNextAction_, QStringLiteral("▶"), tr("Next spectrum"));
    addNavigationButton(spectrumLastAction_, QStringLiteral("⏭"), tr("Last spectrum"));
    spectrumControlBar_->addSeparator();

    spectrumControlBar_->addWidget(new QLabel(tr("Navigate"), spectrumControlBar_));
    spectrumLevel_ = new QComboBox(spectrumControlBar_);
    spectrumLevel_->setObjectName(QStringLiteral("spectrumLevelFilter"));
    spectrumLevel_->addItems({tr("All"), tr("MS1"), tr("MS2")});
    spectrumLevel_->setAccessibleName(tr("Spectrum navigation MS level"));
    spectrumControlBar_->addWidget(spectrumLevel_);

    spectrumIndex_ = new QSpinBox(spectrumControlBar_);
    spectrumIndex_->setObjectName(QStringLiteral("spectrumIndex"));
    spectrumIndex_->setPrefix(tr("Scan "));
    spectrumIndex_->setRange(0, 0);
    spectrumIndex_->setSpecialValueText(tr("No scan"));
    spectrumIndex_->setMaximumWidth(130);
    spectrumIndex_->setAccessibleName(tr("Spectrum number"));
    spectrumControlBar_->addWidget(spectrumIndex_);
    connect(spectrumIndex_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value)
    {
      if (!updatingSpectrumIndex_ && value > 0) selectSpectrum(static_cast<std::size_t>(value - 1));
    });

    spectrumSearch_ = new QLineEdit(spectrumControlBar_);
    spectrumSearch_->setObjectName(QStringLiteral("spectrumSearch"));
    spectrumSearch_->setPlaceholderText(
      tr("Native ID or RT (%1)…").arg(RtUnit::unit(rtInMinutes())));
    spectrumSearch_->setClearButtonEnabled(true);
    spectrumSearch_->setMaximumWidth(175);
    spectrumSearch_->setAccessibleName(tr("Find spectrum by native ID or retention time"));
    spectrumControlBar_->addWidget(spectrumSearch_);
    connect(spectrumSearch_, &QLineEdit::returnPressed, this, [this]
    {
      const QString query = spectrumSearch_->text().trimmed();
      if (query.isEmpty() || document_.isEmpty()) return;
      bool numeric = false;
      const double rt = query.toDouble(&numeric) * RtUnit::scale(rtInMinutes());
      if (numeric)
      {
        if (const auto index = document_.nearestSpectrumIndex(rt, navigationMsLevel()))
        {
          selectSpectrum(*index);
          return;
        }
      }
      else
      {
        const auto& records = document_.spectra();
        const auto found = std::find_if(records.cbegin(), records.cend(),
          [&query](const SpectrumRecord& record)
          {
            return record.nativeId.contains(query, Qt::CaseInsensitive);
          });
        if (found != records.cend())
        {
          selectSpectrum(found->index);
          return;
        }
      }
      statusBar()->showMessage(tr("No spectrum matches ‘%1’").arg(query), 4000);
    });

    spectrumControlBar_->addSeparator();
    auto* measureButton = new QToolButton(spectrumControlBar_);
    measureButton->setDefaultAction(measureSpectrumAction_);
    measureButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    spectrumControlBar_->addWidget(measureButton);

    auto* labelButton = new QToolButton(spectrumControlBar_);
    labelButton->setDefaultAction(labelSpectrumAction_);
    labelButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    spectrumControlBar_->addWidget(labelButton);

    auto* annotationButton = new QToolButton(spectrumControlBar_);
    annotationButton->setText(tr("Annotation"));
    annotationButton->setPopupMode(QToolButton::InstantPopup);
    annotationButton->setAccessibleName(tr("Spectrum annotation settings"));
    auto* annotationMenu = new QMenu(annotationButton);
    annotationMenu->addAction(annotateSpectrumAction_);
    annotationMenu->addAction(mirrorSpectrumAction_);
    annotationMenu->addAction(showUnmatchedIonsAction_);
    annotationMenu->addAction(showMzLabelsAction_);
    annotationMenu->addAction(showSpectrumGridAction_);
    annotationMenu->addSeparator();
    auto* toleranceContainer = new QWidget(annotationMenu);
    auto* toleranceLayout = new QHBoxLayout(toleranceContainer);
    toleranceLayout->setContentsMargins(8, 3, 8, 3);
    toleranceLayout->addWidget(new QLabel(tr("Tolerance (Da)"), toleranceContainer));
    annotationTolerance_ = new QDoubleSpinBox(toleranceContainer);
    annotationTolerance_->setDecimals(3);
    annotationTolerance_->setRange(0.001, 2.0);
    annotationTolerance_->setSingleStep(0.01);
    annotationTolerance_->setValue(0.05);
    annotationTolerance_->setMaximumWidth(90);
    annotationTolerance_->setAccessibleName(tr("Fragment annotation tolerance in daltons"));
    toleranceLayout->addWidget(annotationTolerance_);
    auto* toleranceAction = new QWidgetAction(annotationMenu);
    toleranceAction->setDefaultWidget(toleranceContainer);
    annotationMenu->addAction(toleranceAction);
    annotationButton->setMenu(annotationMenu);
    spectrumControlBar_->addWidget(annotationButton);

    auto* more = new QToolButton(spectrumControlBar_);
    more->setText(tr("More"));
    more->setPopupMode(QToolButton::InstantPopup);
    auto* moreMenu = new QMenu(more);
    moreMenu->addAction(relativeIntensityAction_);
    moreMenu->addAction(autoYScaleAction_);
    moreMenu->addSeparator();
    moreMenu->addAction(resetSpectrumViewAction_);
    moreMenu->addAction(clearSpectrumMeasurementsAction_);
    moreMenu->addAction(clearSpectrumLabelsAction_);
    more->setMenu(moreMenu);
    spectrumControlBar_->addWidget(more);

    connect(annotationTolerance_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] { updateSpectrumAnnotation(); });
  }

  void MainWindow::createStatusContext()
  {
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("statusProgress"));
    progress_->setMaximumWidth(140);
    progress_->setTextVisible(false);
    progress_->hide();

    runContext_ = new QLabel(tr("No data"), this);
    runContext_->setObjectName(QStringLiteral("runContext"));
    runContext_->setAccessibleName(tr("Loaded run summary"));
    selectionContext_ = new QLabel(tr("Spectrum —"), this);
    selectionContext_->setObjectName(QStringLiteral("selectionContext"));
    selectionContext_->setAccessibleName(tr("Selected spectrum summary"));
    cursorContext_ = new QLabel(tr("Cursor —"), this);
    cursorContext_->setObjectName(QStringLiteral("cursorContext"));
    cursorContext_->setAccessibleName(tr("Peak-map cursor position"));
    viewContext_ = new QLabel(tr("View —"), this);
    viewContext_->setObjectName(QStringLiteral("viewContext"));
    viewContext_->setAccessibleName(tr("Peak-map visible range"));
    for (QLabel* label : {runContext_, selectionContext_, cursorContext_, viewContext_})
    {
      label->setMargin(4);
      statusBar()->addPermanentWidget(label);
    }
    runContext_->setMaximumWidth(260);
    selectionContext_->setMaximumWidth(240);
    cursorContext_->setMaximumWidth(170);
    viewContext_->setMaximumWidth(285);
    statusBar()->addPermanentWidget(progress_);

    installPlotContextMenu(peakMap_, QStringLiteral("peak_map.png"),
                           [this] { peakMap_->resetView(); });
    installPlotContextMenu(spectrum_, QStringLiteral("spectrum.png"),
                           [this] { spectrum_->resetMzView(); });
    installPlotContextMenu(tic_, QStringLiteral("tic.png"),
                           [this] { tic_->resetView(); });
    installPlotContextMenu(chromatograms_->plot(), QStringLiteral("chromatograms.png"));
    installPlotContextMenu(ionMobility_->plot(), QStringLiteral("ion_mobility.png"),
                           [this] { ionMobility_->plot()->resetView(); });
    installPlotContextMenu(faims_->plot(), QStringLiteral("faims_traces.png"));
    installPlotContextMenu(imaging_->imageWidget(), QStringLiteral("ion_image.png"));
  }

  void MainWindow::installPlotContextMenu(QWidget* widget, const QString& defaultName,
                                          std::function<void()> reset)
  {
    if (!widget) return;
    widget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(widget, &QWidget::customContextMenuRequested, this,
      [this, widget, defaultName, reset = std::move(reset)](const QPoint& point)
      {
        QMenu menu(widget);
        if (reset)
        {
          menu.addAction(tr("Reset view"), this, reset);
          menu.addSeparator();
        }
        menu.addAction(tr("Export plot as image…"), this,
                       [this, widget, defaultName] { savePlot(widget, defaultName); });
        menu.exec(widget->mapToGlobal(point));
      });
  }

  void MainWindow::showOperationError(const QString& title, const QString& summary,
                                      const QString& details)
  {
    qWarning().noquote() << title << details;
    QMessageBox message(QMessageBox::Critical, title, summary, QMessageBox::Close, this);
    message.setDetailedText(details);
    QPushButton* openLog = message.addButton(tr("Open application log"), QMessageBox::ActionRole);
    message.exec();
    if (message.clickedButton() == openLog)
    {
      dockVisibilityPreference_[logDock_->objectName()] = true;
      setDockAvailable(logDock_, true);
      logDock_->raise();
    }
  }

  void MainWindow::notify(const QString& message, ToastLevel level)
  {
    if (toasts_) toasts_->showToast(message, level);
  }

  void MainWindow::rebuildRecentFiles()
  {
    QStringList valid;
    for (const QString& path : std::as_const(recentFiles_))
    {
      // Bruker .d datasets are directories, so keep those alongside plain files.
      const QFileInfo info(path);
      const auto format = FormatRegistry::detect(path);
      const bool isSupportedDirectory = info.isDir()
        && (info.suffix().compare(QStringLiteral("d"), Qt::CaseInsensitive) == 0
            || (format.supported
                && (format.category == FormatRegistry::Category::Features
                    || format.category == FormatRegistry::Category::Identifications
                    || format.category == FormatRegistry::Category::Consensus)));
      if ((info.isFile() || isSupportedDirectory) && !valid.contains(info.absoluteFilePath()))
        valid.push_back(info.absoluteFilePath());
    }
    recentFiles_ = valid.mid(0, 10);
    if (welcome_) welcome_->setRecentFiles(recentFiles_);
    if (!recentFilesMenu_) return;
    recentFilesMenu_->clear();
    if (recentFiles_.isEmpty())
    {
      QAction* empty = recentFilesMenu_->addAction(tr("No recent files"));
      empty->setEnabled(false);
      return;
    }
    for (const QString& path : std::as_const(recentFiles_))
    {
      QAction* action = recentFilesMenu_->addAction(QFileInfo(path).fileName());
      action->setToolTip(path);
      connect(action, &QAction::triggered, this, [this, path] { loadFile(path); });
    }
    recentFilesMenu_->addSeparator();
    recentFilesMenu_->addAction(tr("Clear recent files"), this, [this]
    {
      recentFiles_.clear();
      rebuildRecentFiles();
    });
  }

  void MainWindow::rememberRecentFile(const QString& path)
  {
    const QString absolute = QFileInfo(path).absoluteFilePath();
    recentFiles_.removeAll(absolute);
    recentFiles_.prepend(absolute);
    rebuildRecentFiles();
  }

  void MainWindow::reloadLastFile()
  {
    if (lastPrimaryPath_.isEmpty() || !QFileInfo::exists(lastPrimaryPath_))
    {
      statusBar()->showMessage(tr("The last primary data file is no longer available"), 5000);
      return;
    }
    loadFile(lastPrimaryPath_);
  }

  void MainWindow::showWelcomePage()
  {
    centralStack_->setCurrentWidget(welcome_);
    setPeakMapControlsEnabled(false);
    closeDataAction_->setEnabled(false);
    if (saveFeaturesAction_) saveFeaturesAction_->setEnabled(false);
    if (saveFeaturesAsAction_) saveFeaturesAsAction_->setEnabled(false);
    if (saveIdentificationsAction_) saveIdentificationsAction_->setEnabled(false);
    if (saveConsensusAction_) saveConsensusAction_->setEnabled(false);
    setDockAvailable(ticDock_, false);
    setDockAvailable(spectrumDock_, false);
    setDockAvailable(featuresDock_, false);
    setDockAvailable(identificationsDock_, false);
    setDockAvailable(spectraDock_, false);
    setDockAvailable(chromatogramsDock_, false);
    setDockAvailable(dataLayersDock_, false);
    setDockAvailable(ionMobilityDock_, false);
    setDockAvailable(faimsDock_, false);
    setDockAvailable(imagingDock_, false);
    setDockAvailable(metadataDock_, false);
    if (metadata_) metadata_->clear();
    setDockAvailable(oswDock_, false);
    if (osw_) osw_->clear();
    hasOswData_ = false;
    oswSourcePath_.clear();
    setDockAvailable(consensusDock_, false);
    if (consensus_) consensus_->clear();
    hasConsensusData_ = false;
    consensusMap_.reset();
    consensusColumns_.clear();
    consensusSourcePath_.clear();
    if (peakMap_) peakMap_->setConsensusFeatures({});
    if (showConsensusAction_) showConsensusAction_->setEnabled(false);
    if (peakMap_) peakMap_->setPrecursorMarkers({});
    if (showPrecursorsAction_) showPrecursorsAction_->setEnabled(false);
  }

  void MainWindow::showDataPage()
  {
    centralStack_->setCurrentWidget(peakMapPanel_);
    closeDataAction_->setEnabled(true);
  }

  void MainWindow::initializeDockPreferences(bool hasSavedState)
  {
    const QMap<QString, bool> defaults{
      {ticDock_->objectName(), true}, {spectrumDock_->objectName(), true},
      {featuresDock_->objectName(), true}, {identificationsDock_->objectName(), true},
      {spectraDock_->objectName(), false}, {chromatogramsDock_->objectName(), true},
      {dataLayersDock_->objectName(), true},
      {ionMobilityDock_->objectName(), true}, {faimsDock_->objectName(), true},
      {imagingDock_->objectName(), true}, {oswDock_->objectName(), true},
      {consensusDock_->objectName(), true}, {metadataDock_->objectName(), false},
      {logDock_->objectName(), false}
    };
    QSettings settings;
    for (QDockWidget* dock : {ticDock_, spectrumDock_, featuresDock_, identificationsDock_,
                              spectraDock_, chromatogramsDock_, dataLayersDock_, ionMobilityDock_, faimsDock_,
                              imagingDock_, oswDock_, consensusDock_, metadataDock_, logDock_})
    {
      const QString key = QStringLiteral("docks/%1/preferredVisible").arg(dock->objectName());
      bool preferred = defaults.value(dock->objectName(), false);
      if (settings.contains(key)) preferred = settings.value(key).toBool();
      else if (hasSavedState) preferred = dock->toggleViewAction()->isChecked();
      dockVisibilityPreference_[dock->objectName()] = preferred;
      connect(dock->toggleViewAction(), &QAction::triggered, this,
              [this, name = dock->objectName()](bool visible)
              {
                dockVisibilityPreference_[name] = visible;
              });
    }
    setDockAvailable(logDock_, true);
  }

  bool MainWindow::rtInMinutes() const
  {
    return rtInMinutesAction_ && rtInMinutesAction_->isChecked();
  }

  void MainWindow::moveDock(QDockWidget* dock, Qt::DockWidgetArea area)
  {
    if (!dock) return;
    if (dock->isFloating()) dock->setFloating(false);
    // If the target area already holds a visible panel, split it vertically so the
    // moved panel STACKS beneath it (both visible) instead of tabbing on top of it.
    // Only an empty area uses addDockWidget (which would otherwise tab).
    QDockWidget* anchor = nullptr;
    for (QDockWidget* other : findChildren<QDockWidget*>())
      if (other != dock && !other->isFloating() && other->isVisible()
          && dockWidgetArea(other) == area)
      { anchor = other; break; }
    if (anchor)
      splitDockWidget(anchor, dock, Qt::Vertical);
    else
      addDockWidget(area, dock);
    dock->setVisible(true);
    dock->raise();
    dockVisibilityPreference_[dock->objectName()] = true;
  }

  void MainWindow::configureDock(QDockWidget* dock)
  {
    if (!dock) return;
    const bool restrict = restrictDockFloating();
    auto features = QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable;
    if (!restrict) features |= QDockWidget::DockWidgetFloatable;
    dock->setFeatures(features);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    // Native title bar where floating is unsafe (WSLg); custom draggable header
    // with float/dock/close controls everywhere else.
    if (!restrict) dock->setTitleBarWidget(new FloatingDockTitleBar(dock));
    connect(dock, &QDockWidget::topLevelChanged, this, [this, dock](bool floating)
    {
      if (!floating) return;
      QTimer::singleShot(100, dock, [this, dock] { ensureFloatingDockVisible(dock); });
    });
  }

  void MainWindow::ensureFloatingDockVisible(QDockWidget* dock)
  {
    if (!dock || !dock->isFloating()) return;
    // Wayland owns top-level placement. Moving a surface before/around its first
    // configure event is a protocol error; the compositor already keeps it visible.
    if (QGuiApplication::platformName().contains(QStringLiteral("wayland"),
                                                  Qt::CaseInsensitive)) return;
    if (!dock->isVisible()) return;
    QWindow* handle = dock->windowHandle();
    if (!handle || !handle->isExposed())
    {
      QTimer::singleShot(100, dock, [this, dock] { ensureFloatingDockVisible(dock); });
      return;
    }
    QScreen* screen = QGuiApplication::screenAt(dock->frameGeometry().center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QRect available = screen->availableGeometry();
    QSize size = dock->size();
    size.setWidth(std::min(size.width(), static_cast<int>(available.width() * 0.9)));
    size.setHeight(std::min(size.height(), static_cast<int>(available.height() * 0.9)));
    dock->resize(size);

    const QRect frame = dock->frameGeometry();
    const int minimumVisibleWidth = std::min(160, frame.width());
    const int x = std::clamp(frame.x(), available.left() - frame.width() + minimumVisibleWidth,
                             available.right() - minimumVisibleWidth + 1);
    const int y = std::clamp(frame.y(), available.top(),
                             std::max(available.top(), available.bottom() - 31));
    dock->move(x, y);
  }

  void MainWindow::setDockAvailable(QDockWidget* dock, bool available)
  {
    if (!dock) return;
    dock->toggleViewAction()->setEnabled(available);
    dock->setVisible(available && dockVisibilityPreference_.value(dock->objectName(), false));
  }

  void MainWindow::closeSurface3D()
  {
    // Hide + release on any primary-data transition so a stale run isn't shown and
    // its (potentially large) experiment isn't retained.
    if (surface3D_) surface3D_->clear();
    if (surface3DDialog_) surface3DDialog_->hide();
  }

  void MainWindow::show3DSurface()
  {
    if (imagingStore_) return;   // imaging mode has no RT/m-z peak map
    // Use the experiment the peak map is actually showing (e.g. a FAIMS-filtered
    // channel), not the full document, so the surface matches the 2-D view.
    const auto experiment = peakMap_->experiment();
    if (!experiment)
    {
      statusBar()->showMessage(tr("Load an mzML file first"), 4000);
      return;
    }
    const PlotRange view = peakMap_->viewRange();
    if (!PeakSurface3DWidget::viewFitsForSurface(view))
    {
      statusBar()->showMessage(
        tr("Zoom the peak map in (RT ≤ %1 %2, m/z ≤ %3) before opening the 3-D view")
          .arg(RtUnit::format(PeakSurface3DWidget::kMaxRtSpan, rtInMinutes(), 0), RtUnit::unit(rtInMinutes()))
          .arg(PeakSurface3DWidget::kMaxMzSpan, 0, 'f', 0), 5000);
      return;
    }
    if (!surface3DDialog_)
    {
      surface3DDialog_ = new QDialog(this);
      surface3DDialog_->setWindowTitle(tr("3-D peak surface"));
      surface3DDialog_->resize(560, 480);
      auto* layout = new QVBoxLayout(surface3DDialog_);
      layout->setContentsMargins(0, 0, 0, 0);
      surface3D_ = new PeakSurface3DWidget(surface3DDialog_);
      // Wire the "RT in minutes" toggle now that the widget exists, and seed it
      // with the current unit so the first view already matches the rest of the app.
      connect(rtInMinutesAction_, &QAction::toggled, surface3D_, &PeakSurface3DWidget::setRtInMinutes);
      surface3D_->setRtInMinutes(rtInMinutes());
      layout->addWidget(surface3D_);
    }
    surface3D_->setView(experiment, view, peakMap_->colorMap());
    surface3DDialog_->show();
    surface3DDialog_->raise();
    surface3DDialog_->activateWindow();
  }

  void MainWindow::resetDockLayout()
  {
    dockVisibilityPreference_[ticDock_->objectName()] = true;
    dockVisibilityPreference_[spectrumDock_->objectName()] = true;
    dockVisibilityPreference_[featuresDock_->objectName()] = true;
    dockVisibilityPreference_[identificationsDock_->objectName()] = true;
    dockVisibilityPreference_[spectraDock_->objectName()] = false;
    dockVisibilityPreference_[chromatogramsDock_->objectName()] = true;
    dockVisibilityPreference_[dataLayersDock_->objectName()] = true;
    dockVisibilityPreference_[ionMobilityDock_->objectName()] = true;
    dockVisibilityPreference_[faimsDock_->objectName()] = true;
    dockVisibilityPreference_[imagingDock_->objectName()] = true;
    dockVisibilityPreference_[oswDock_->objectName()] = true;
    dockVisibilityPreference_[consensusDock_->objectName()] = true;
    dockVisibilityPreference_[metadataDock_->objectName()] = false;
    dockVisibilityPreference_[logDock_->objectName()] = false;

    for (QDockWidget* dock : {ticDock_, spectrumDock_, featuresDock_, identificationsDock_,
                              spectraDock_, chromatogramsDock_, dataLayersDock_, ionMobilityDock_, faimsDock_,
                              imagingDock_, oswDock_, consensusDock_, metadataDock_, logDock_})
      dock->setFloating(false);

    arrangeDocksDefault();
    updateDataLayers();

    if (document_.isEmpty() && !document_.hasFeatures() && !document_.hasIdentifications()
        && !imagingStore_ && !hasOswData_ && !hasConsensusData_)
      showWelcomePage();
    else if (hasOswData_)
    {
      setDockAvailable(oswDock_, true);
      oswDock_->raise();
    }
    else if (hasConsensusData_)
    {
      setDockAvailable(consensusDock_, true);
      consensusDock_->raise();
    }
    else if (imagingStore_)
    {
      setDockAvailable(spectrumDock_, true);
      setDockAvailable(imagingDock_, true);
      imagingDock_->raise();
    }
    else
    {
      const bool hasSpectra = !document_.spectra().empty();
      setDockAvailable(ticDock_, hasSpectra);
      setDockAvailable(spectrumDock_, hasSpectra);
      setDockAvailable(spectraDock_, hasSpectra);
      setDockAvailable(featuresDock_, document_.hasFeatures());
      setDockAvailable(identificationsDock_, document_.hasIdentifications());
      setDockAvailable(chromatogramsDock_, document_.hasChromatograms());
      setDockAvailable(ionMobilityDock_, document_.hasIonMobility());
      setDockAvailable(faimsDock_, document_.hasFaims());
      setDockAvailable(metadataDock_, document_.experimentHandle() != nullptr);
    }
    setDockAvailable(logDock_, true);
    // Re-sync the save actions to the actual data: resetDockLayout can route through
    // showWelcomePage (which disables them) even when only an overlay — no raw run —
    // is loaded, which would otherwise leave a loaded map unsaveable.
    updateSaveActions();
    statusBar()->showMessage(tr("Panel layout reset"), 3000);
  }

  void MainWindow::arrangeDocksDefault()
  {
    // Everything lives in the bottom area, stacked in full-width rows — nothing in
    // the narrow right column, and no half-width plots. Spectrum over TIC over a
    // full-width tabbed row of tables/panels; every panel spans the full width.
    addDockWidget(Qt::BottomDockWidgetArea, spectrumDock_);
    splitDockWidget(spectrumDock_, ticDock_, Qt::Vertical);   // TIC full-width below the spectrum
    splitDockWidget(ticDock_, featuresDock_, Qt::Vertical);   // tables full-width below the TIC
    QDockWidget* previous = featuresDock_;
    for (QDockWidget* dock : {identificationsDock_, spectraDock_, chromatogramsDock_,
                              ionMobilityDock_, imagingDock_, faimsDock_, oswDock_,
                              consensusDock_, metadataDock_, logDock_})
    {
      tabifyDockWidget(previous, dock);
      previous = dock;
    }
    featuresDock_->raise();
    addDockWidget(Qt::RightDockWidgetArea, dataLayersDock_);
  }

  void MainWindow::applyLayoutPreset(LayoutPreset preset)
  {
    if (preset == LayoutPreset::Overview) { resetDockLayout(); return; }

    const std::vector<QDockWidget*> allDocks = {
      ticDock_, spectrumDock_, featuresDock_, identificationsDock_, spectraDock_,
      chromatogramsDock_, ionMobilityDock_, faimsDock_, imagingDock_, oswDock_,
      consensusDock_, metadataDock_, logDock_};
    for (QDockWidget* dock : allDocks) dock->setFloating(false);

    // A dock is only worth showing if its data is present; setDockAvailable keeps
    // the toggle action's enabled state in sync with that.
    const auto available = [](QDockWidget* dock) { return dock->toggleViewAction()->isEnabled(); };
    // Feature a dock (record the preference + show it if it has data), or hide it.
    const auto feature = [&](QDockWidget* dock, bool wanted)
    {
      dockVisibilityPreference_[dock->objectName()] = wanted;
      dock->setVisible(wanted && available(dock));
    };
    const auto hideRest = [&](std::initializer_list<QDockWidget*> featured)
    {
      for (QDockWidget* dock : allDocks)
        if (std::find(featured.begin(), featured.end(), dock) == featured.end())
          feature(dock, false);
    };

    QString name;
    QDockWidget* focus = nullptr;
    switch (preset)
    {
      case LayoutPreset::Identification:
        // Analyte-centric: the annotated spectrum full width on top, the ID + spectra
        // tables tabbed full width below (never a side column).
        addDockWidget(Qt::BottomDockWidgetArea, spectrumDock_);
        splitDockWidget(spectrumDock_, identificationsDock_, Qt::Vertical);
        tabifyDockWidget(identificationsDock_, spectraDock_);
        feature(spectrumDock_, true);
        feature(identificationsDock_, true);
        feature(spectraDock_, true);
        hideRest({spectrumDock_, identificationsDock_, spectraDock_});
        focus = identificationsDock_;
        name = tr("Identification");
        break;

      case LayoutPreset::Imaging:
        // MSI: the ion image full width over the spectrum.
        addDockWidget(Qt::BottomDockWidgetArea, imagingDock_);
        splitDockWidget(imagingDock_, spectrumDock_, Qt::Vertical);
        feature(imagingDock_, true);
        feature(spectrumDock_, true);
        hideRest({imagingDock_, spectrumDock_});
        focus = imagingDock_;
        name = tr("Imaging");
        break;

      case LayoutPreset::Dia:
        // Targeted DIA: OpenSWATH panel, spectrum, and chromatograms as full-width
        // rows stacked top to bottom.
        addDockWidget(Qt::BottomDockWidgetArea, oswDock_);
        splitDockWidget(oswDock_, spectrumDock_, Qt::Vertical);
        splitDockWidget(spectrumDock_, chromatogramsDock_, Qt::Vertical);
        feature(oswDock_, true);
        feature(spectrumDock_, true);
        feature(chromatogramsDock_, true);
        hideRest({oswDock_, spectrumDock_, chromatogramsDock_});
        focus = oswDock_;
        name = tr("DIA / OpenSWATH");
        break;

      case LayoutPreset::Overview:
        break;  // handled above
    }

    if (focus && available(focus)) focus->raise();
    updateSaveActions();
    statusBar()->showMessage(tr("Applied %1 layout").arg(name), 3000);
  }

  void MainWindow::updateSaveActions()
  {
    saveFeaturesAction_->setEnabled(document_.hasFeatures());
    saveFeaturesAsAction_->setEnabled(document_.hasFeatures());
    saveIdentificationsAction_->setEnabled(document_.hasIdentifications());
    saveConsensusAction_->setEnabled(hasConsensusData_);
  }

  void MainWindow::updateDataLayers()
  {
    if (!dataLayers_) return;
    const bool hasPrimary = imagingStore_ || !document_.isEmpty();
    const QString primaryPath = imagingStore_ ? imagingSummary_.sourcePath : document_.sourcePath();
    const QString primarySummary = imagingStore_
      ? tr("%1 pixels").arg(imagingSummary_.pixels.size())
      : hasPrimary ? tr("%1 spectra").arg(document_.statistics().spectrumCount) : QString{};
    dataLayers_->setLayer(DataLayersWidget::Layer::Primary, primaryPath, primarySummary, hasPrimary);

    const bool featureVisible = showCentroidsAction_->isChecked()
      || showFeatureBoundsAction_->isChecked() || showFeatureHullsAction_->isChecked();
    dataLayers_->setLayer(DataLayersWidget::Layer::Features,
      featureSavePath_.isEmpty() ? document_.featuresPath() : featureSavePath_,
      tr("%1 features").arg(document_.features().size()), document_.hasFeatures(),
      featureVisible, featureUndoStack_ && !featureUndoStack_->isClean());

    const bool idsVisible = showIdentificationsAction_->isChecked()
      || showIdentificationSequencesAction_->isChecked();
    dataLayers_->setLayer(DataLayersWidget::Layer::Identifications,
      document_.identificationsPath(), tr("%1 identifications").arg(document_.identifications().size()),
      document_.hasIdentifications(), idsVisible);
    dataLayers_->setLayer(DataLayersWidget::Layer::Consensus, consensusSourcePath_,
      consensusMap_ ? tr("%1 consensus features").arg(consensusMap_->size()) : QString{},
      hasConsensusData_, showConsensusAction_->isChecked());
    dataLayers_->setLayer(DataLayersWidget::Layer::OpenSwath, oswSourcePath_,
      hasOswData_ ? tr("OpenSWATH results") : QString{}, hasOswData_,
      oswDock_ && oswDock_->isVisible());

    const bool anything = hasPrimary || document_.hasFeatures() || document_.hasIdentifications()
      || hasConsensusData_ || hasOswData_;
    if (dataLayersDock_->toggleViewAction()->isEnabled() != anything)
      setDockAvailable(dataLayersDock_, anything);
  }

  void MainWindow::updateFeatureEditState()
  {
    updateSaveActions();
    updateDataLayers();
    updateWindowTitle();
  }

  void MainWindow::updateRunContext()
  {
    updateDataLayers();
    reloadAction_->setEnabled(!lastPrimaryPath_.isEmpty() && QFileInfo::exists(lastPrimaryPath_));
    // A bare feature/id overlay (no raw run) is still closable: keep "Close data"
    // reachable whenever anything is loaded, since it is the only UI that clears an
    // overlay-only session.
    closeDataAction_->setEnabled(imagingStore_ || !document_.isEmpty()
      || document_.hasFeatures() || document_.hasIdentifications()
      || hasConsensusData_ || hasOswData_);
    // Results views (consensus / OSW) can be loaded alongside — or instead of — a raw
    // run, so they are tagged everywhere rather than only on the raw-run branch.
    QStringList results;
    if (hasConsensusData_) results << tr("Consensus");
    if (hasOswData_) results << tr("OSW");
    const QString resultsTag = results.isEmpty()
      ? QString() : tr(" · %1").arg(results.join(QStringLiteral(" + ")));

    if (imagingStore_)
    {
      runContext_->setText(tr("%1 · %2 pixels%3")
        .arg(QFileInfo(imagingSummary_.sourcePath).fileName())
        .arg(imagingSummary_.pixels.size()).arg(resultsTag));
      runContext_->setToolTip(imagingSummary_.sourcePath);
      return;
    }
    if (document_.isEmpty())
    {
      // Overlay/results-only session (no raw run): surface whatever is loaded —
      // features, identifications, consensus, or OSW — instead of "No data".
      QStringList overlaysOnly;
      if (document_.hasFeatures()) overlaysOnly << tr("Features");
      if (document_.hasIdentifications()) overlaysOnly << tr("IDs");
      overlaysOnly += results;
      if (!overlaysOnly.isEmpty())
      {
        const QString source = document_.hasFeatures()
          ? (featureSavePath_.isEmpty() ? document_.featuresPath() : featureSavePath_)
          : document_.hasIdentifications() ? document_.identificationsPath()
          : hasConsensusData_ ? consensusSourcePath_ : oswSourcePath_;
        runContext_->setText(tr("%1 · %2").arg(QFileInfo(source).fileName(),
                                               overlaysOnly.join(QStringLiteral(" + "))));
        runContext_->setToolTip(source);
      }
      else
      {
        runContext_->setText(tr("No data"));
        runContext_->setToolTip({});
      }
      selectionContext_->setText(tr("Spectrum —"));
      viewContext_->setText(tr("View —"));
      return;
    }
    QStringList overlays;
    if (document_.hasFeatures()) overlays << tr("Features");
    if (document_.hasIdentifications()) overlays << tr("IDs");
    overlays += results;
    QString text = tr("%1 · %2 spectra · %3 peaks")
      .arg(QFileInfo(document_.sourcePath()).fileName())
      .arg(document_.statistics().spectrumCount).arg(document_.statistics().peakCount);
    if (!overlays.isEmpty()) text += tr(" · %1").arg(overlays.join(QStringLiteral(" + ")));
    runContext_->setText(text);
    runContext_->setToolTip(document_.sourcePath());
  }

  void MainWindow::updateWindowTitle()
  {
    // A raw run (or imaging) is the primary; results views loaded alongside it are
    // appended as tags so the title stays coherent no matter what loaded last —
    // rather than the previous "whichever finished most recently wins".
    QString primary;
    if (imagingStore_) primary = QFileInfo(imagingSummary_.sourcePath).fileName();
    else if (!document_.isEmpty()) primary = QFileInfo(document_.sourcePath()).fileName();
    else if (document_.hasFeatures())
    {
      const QString path = featureSavePath_.isEmpty() ? document_.featuresPath() : featureSavePath_;
      primary = path.isEmpty() ? tr("Unsaved features") : QFileInfo(path).fileName();
    }
    else if (document_.hasIdentifications())
      primary = QFileInfo(document_.identificationsPath()).fileName();

    QStringList results;
    if (hasConsensusData_ && !consensusSourcePath_.isEmpty())
      results << QFileInfo(consensusSourcePath_).fileName();
    if (hasOswData_ && !oswSourcePath_.isEmpty())
      results << QFileInfo(oswSourcePath_).fileName();

    // With no raw run, the first results view becomes the primary label itself.
    if (primary.isEmpty() && !results.isEmpty()) primary = results.takeFirst();

    QString title;
    if (primary.isEmpty())
      title = tr("OpenMS Viewer");
    else if (results.isEmpty())
      title = tr("%1 — OpenMS Viewer").arg(primary);
    else
      title = tr("%1 (+ %2) — OpenMS Viewer").arg(primary, results.join(QStringLiteral(", ")));
    if (featureUndoStack_ && !featureUndoStack_->isClean()) title.prepend(QStringLiteral("* "));
    setWindowTitle(title);
  }

  void MainWindow::onConsensusFeatureActivated(qint64 index)
  {
    // Selection only highlights the diamond on the peak map; it must not yank the
    // raw spectrum view around during arrow-key browsing of the consensus table.
    if (index < 0) { peakMap_->setSelectedConsensus(std::nullopt); return; }
    peakMap_->setSelectedConsensus(static_cast<std::size_t>(index));
  }

  void MainWindow::onConsensusFeatureDrillDown(qint64 index)
  {
    if (index < 0) return;
    const auto consensusIndex = static_cast<std::size_t>(index);
    peakMap_->setSelectedConsensus(consensusIndex);
    peakMap_->zoomToConsensus(consensusIndex);
    navigateConsensusToRawSpectrum(consensusIndex);
  }

  void MainWindow::navigateConsensusToRawSpectrum(std::size_t consensusIndex)
  {
    if (!consensusMap_) return;
    if (document_.isEmpty())
    {
      notify(tr("Load the raw run to open a consensus feature's source scan"), ToastLevel::Info);
      return;
    }

    const auto target = ConsensusDrilldown::resolve(*consensusMap_, consensusIndex,
                                                    consensusColumns_, document_);
    if (target.ambiguousRun)
    {
      notify(tr("The loaded run's name matches several input maps — can't resolve the source scan"),
             ToastLevel::Warning);
      return;
    }
    if (!target.runIsInputMap)
    {
      notify(tr("The loaded run is not an input map of this consensus — showing the envelope only"),
             ToastLevel::Info);
      return;
    }
    if (!target.spectrumIndex)
    {
      notify(tr("This consensus feature has no handle in the loaded run"), ToastLevel::Info);
      return;
    }

    selectSpectrum(*target.spectrumIndex);
    if (target.exact)
      notify(tr("Opened the source scan for %1").arg(QFileInfo(document_.sourcePath()).fileName()),
             ToastLevel::Success);
    else
      notify(tr("No source scan recorded — showing the nearest scan by RT"), ToastLevel::Warning);
  }

  unsigned int MainWindow::navigationMsLevel() const
  {
    return spectrumLevel_ ? static_cast<unsigned int>(spectrumLevel_->currentIndex()) : 0U;
  }

  void MainWindow::updateSpectrumControls()
  {
    const std::size_t count = imagingStore_ ? imagingStore_->spectrumCount()
                                            : document_.statistics().spectrumCount;
    const bool available = count > 0;
    for (QAction* action : {spectrumFirstAction_, spectrumPreviousAction_, spectrumNextAction_,
                            spectrumLastAction_, annotateSpectrumAction_, mirrorSpectrumAction_,
                            showUnmatchedIonsAction_, measureSpectrumAction_, labelSpectrumAction_,
                            showMzLabelsAction_, showSpectrumGridAction_,
                            resetSpectrumViewAction_, clearSpectrumMeasurementsAction_,
                            clearSpectrumLabelsAction_})
      if (action) action->setEnabled(available);
    if (spectrumLevel_) spectrumLevel_->setEnabled(available && !imagingStore_);
    if (spectrumSearch_) spectrumSearch_->setEnabled(available && !imagingStore_);
    if (annotationTolerance_) annotationTolerance_->setEnabled(available && !imagingStore_);
    if (spectrumIndex_)
    {
      QSignalBlocker blocker(spectrumIndex_);
      spectrumIndex_->setSpecialValueText(available ? QString() : tr("No scan"));
      spectrumIndex_->setRange(available ? 1 : 0, available ? static_cast<int>(count) : 0);
      spectrumIndex_->setValue(available && selection_.spectrum()
        ? static_cast<int>(*selection_.spectrum() + 1) : (available ? 1 : 0));
      spectrumIndex_->setEnabled(available);
    }
  }

  void MainWindow::setPeakMapControlsEnabled(bool enabled)
  {
    // The whole in-panel control bar is shown/enabled together; hide it when
    // there is no peak map (welcome page, imaging mode) so it takes no space.
    if (peakMapControlBar_)
    {
      peakMapControlBar_->setEnabled(enabled);
      peakMapControlBar_->setVisible(enabled);
    }
    resetViewAction_->setEnabled(enabled);
    if (!enabled) zoomBackAction_->setEnabled(false);
  }

  void MainWindow::beginOperation(int operation, const QString& title, const QString& detail,
                                  bool cancellable)
  {
    const auto next = static_cast<Operation>(operation);
    const auto priority = [](Operation value)
    {
      if (value == MzMLOperation || value == ImagingOperation) return 3;
      if (value == ExportOperation) return 2;
      if (value == FeatureOperation || value == IdentificationOperation) return 1;
      return 0;
    };
    if (overlayOperation_ != NoOperation && priority(next) < priority(overlayOperation_)) return;
    overlayOperation_ = next;
    operationElapsed_.restart();
    loadingOverlay_->begin(title, detail, cancellable);
    operationTimer_.start();
  }

  void MainWindow::endOperation(int operation)
  {
    if (overlayOperation_ != static_cast<Operation>(operation)) return;
    overlayOperation_ = NoOperation;
    operationTimer_.stop();
    loadingOverlay_->finish();
    showNextPendingOperation();
  }

  void MainWindow::showNextPendingOperation()
  {
    if (loadWatcher_.isRunning())
      beginOperation(MzMLOperation, tr("Loading mzML"), tr("Reading spectra and chromatograms"));
    else if (imagingLoadWatcher_.isRunning())
      beginOperation(ImagingOperation, tr("Loading imaging data"), tr("Opening imzML and IBD data"));
    else if (mzMLExportWatcher_.isRunning())
      beginOperation(ExportOperation, tr("Exporting mzML"), tr("Writing filtered spectra"));
    else if (featureLoadWatcher_.isRunning())
      beginOperation(FeatureOperation, tr("Loading feature overlay"), tr("Reading FeatureXML"));
    else if (identificationLoadWatcher_.isRunning())
      beginOperation(IdentificationOperation, tr("Loading identification overlay"), tr("Reading idXML"));
    else if (oswLoadWatcher_.isRunning())
      beginOperation(OswOperation, tr("Loading OpenSWATH results"), tr("Reading .osw and .xic"), false);
    else if (consensusLoadWatcher_.isRunning())
      beginOperation(ConsensusOperation, tr("Loading consensus map"), tr("Reading consensus features"), false);
  }

  void MainWindow::cancelCurrentOperation()
  {
    switch (overlayOperation_)
    {
      case MzMLOperation:
        if (mzMLCancellation_) mzMLCancellation_->store(true);
        break;
      case ImagingOperation: imagingCancelled_ = true; break;
      case FeatureOperation: featureCancelled_ = true; break;
      case IdentificationOperation: identificationCancelled_ = true; break;
      case ExportOperation: exportCancelled_ = true; break;
      case OswOperation: return;        // not cancellable (single blocking read)
      case ConsensusOperation: return;  // not cancellable
      case NoOperation: return;
    }
    statusBar()->showMessage(tr("Cancelling background operation…"));
  }

  void MainWindow::openFile()
  {
    QSettings settings;
    const QString startDirectory = settings.value(QStringLiteral("files/lastDirectory")).toString();
    const QStringList paths = QFileDialog::getOpenFileNames(
      this, tr("Open mass-spectrometry data"), startDirectory,
      tr("OpenMS data (*.mzML *.mzml *.imzML *.imzml *.raw *.mzXML *.mzxml *.mzData *.mzdata *.sqMass *.sqmass *.featureXML *.featurexml *.idXML *.idxml *.mzid *.mzIdentML *.osw *.consensusXML *.consensusxml);;"
         "Spectra (*.mzML *.mzml *.mzXML *.mzxml *.mzData *.mzdata *.sqMass *.sqmass);;"
         "imzML imaging files (*.imzML *.imzml);;Thermo RAW files (*.raw);;"
         "Feature maps (*.featureXML *.featurexml);;"
         "Identifications (*.idXML *.idxml *.mzid *.mzIdentML);;"
         "OpenSWATH results (*.osw);;Consensus maps (*.consensusXML *.consensusxml);;All files (*)"));
    if (paths.isEmpty()) return;
    settings.setValue(QStringLiteral("files/lastDirectory"), QFileInfo(paths.front()).absolutePath());
    loadFiles(paths);
  }

  void MainWindow::openDataFolder()
  {
    QSettings settings;
    const QString startDirectory = settings.value(QStringLiteral("files/lastDirectory")).toString();
    const QString path = QFileDialog::getExistingDirectory(
      this, tr("Open data folder (Bruker .d or Parquet bundle)"), startDirectory,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (path.isEmpty()) return;
    // Directory inputs are Bruker .d datasets or the OpenMS Parquet bundle
    // directories the loader can currently route: featureparquet / idparquet.
    // (consensusparquet / chromparquet are recognized but not wired yet.)
    const bool isBruker = QFileInfo(path).suffix().compare(QStringLiteral("d"), Qt::CaseInsensitive) == 0;
    const auto format = FormatRegistry::detect(path);
    const bool routable = isBruker
      || (format.supported && (format.category == FormatRegistry::Category::Features
                               || format.category == FormatRegistry::Category::Identifications
                               || format.category == FormatRegistry::Category::Consensus));
    if (!routable)
    {
      statusBar()->showMessage(tr("Not a supported data folder: %1").arg(QFileInfo(path).fileName()), 5000);
      notify(tr("Not a supported data folder — expected a Bruker .d or a Parquet bundle"),
             ToastLevel::Warning);
      return;
    }
    settings.setValue(QStringLiteral("files/lastDirectory"), QFileInfo(path).absolutePath());
    loadFile(path);
  }

  void MainWindow::loadFile(const QString& path)
  {
    if (path.isEmpty()) return;
    if (editedFeaturesWouldBeReplacedBy(path)
        && !confirmFeatureChanges(tr("open %1").arg(QFileInfo(path).fileName())))
      return;
    const QString suffix = QFileInfo(path).suffix();
    if (suffix.compare(QStringLiteral("imzML"), Qt::CaseInsensitive) == 0)
    {
      if (imagingLoadWatcher_.isRunning() || loadWatcher_.isRunning())
      {
        statusBar()->showMessage(tr("A primary data file is already loading"), 3000);
        return;
      }
      statusBar()->showMessage(tr("Opening on-disc imaging dataset %1…")
        .arg(QFileInfo(path).fileName()));
      imagingCancelled_ = false;
      beginOperation(ImagingOperation, tr("Loading imaging data"),
                     tr("Opening %1 and its IBD data").arg(QFileInfo(path).fileName()));
      imagingLoadWatcher_.setFuture(QtConcurrent::run(
        [path] { return ImagingDocument::readImzML(path); }));
      updateLoadingUi();
      return;
    }
    if (suffix.compare(QStringLiteral("mzML"), Qt::CaseInsensitive) == 0)
    {
      if (loadWatcher_.isRunning() || imagingLoadWatcher_.isRunning())
      {
        statusBar()->showMessage(tr("An mzML file is already loading"), 3000);
        return;
      }
      statusBar()->showMessage(tr("Loading %1 with OpenMS…").arg(QFileInfo(path).fileName()));
      mzMLLoadTimer_.start();
      mzMLCancellation_ = std::make_shared<std::atomic_bool>(false);
      beginOperation(MzMLOperation, tr("Loading mzML"),
                     tr("Opening %1").arg(QFileInfo(path).fileName()));
      const auto cancellation = mzMLCancellation_;
      loadWatcher_.setFuture(QtConcurrent::run([this, path, cancellation]
      {
        return ViewerDocument::readMzML(path, [this, filename = QFileInfo(path).fileName()]
          (const QString& label, int percent)
        {
          QMetaObject::invokeMethod(this, [this, filename, label, percent]
          {
            if (!loadWatcher_.isRunning()) return;
            progress_->setRange(0, 100);
            progress_->setValue(percent);
            progress_->setTextVisible(true);
            if (overlayOperation_ == MzMLOperation)
              loadingOverlay_->setProgress(label, percent);
            statusBar()->showMessage(tr("Loading %1 · %2 · %3%")
              .arg(filename, label).arg(percent));
          }, Qt::QueuedConnection);
        }, [cancellation] { return cancellation->load(); });
      }));
      updateLoadingUi();
      return;
    }
    if (suffix.compare(QStringLiteral("raw"), Qt::CaseInsensitive) == 0 && QFileInfo(path).isFile())
    {
      if (loadWatcher_.isRunning() || imagingLoadWatcher_.isRunning())
      {
        statusBar()->showMessage(tr("A primary data file is already loading"), 3000);
        return;
      }
      statusBar()->showMessage(tr("Loading Thermo RAW %1…").arg(QFileInfo(path).fileName()));
      mzMLLoadTimer_.start();
      mzMLCancellation_ = std::make_shared<std::atomic_bool>(false);
      // The vendor bridge reads in one blocking call with no progress/cancel
      // hook, so the overlay stays indeterminate and non-cancellable.
      beginOperation(MzMLOperation, tr("Loading Thermo RAW"),
                     tr("Opening %1").arg(QFileInfo(path).fileName()), false);
      const auto cancellation = mzMLCancellation_;
      loadWatcher_.setFuture(QtConcurrent::run([path, cancellation]
      {
        return ViewerDocument::readThermoRaw(path, {},
          [cancellation] { return cancellation->load(); });
      }));
      updateLoadingUi();
      return;
    }
    if (suffix.compare(QStringLiteral("d"), Qt::CaseInsensitive) == 0 && QFileInfo(path).isDir())
    {
      if (loadWatcher_.isRunning() || imagingLoadWatcher_.isRunning())
      {
        statusBar()->showMessage(tr("A primary data file is already loading"), 3000);
        return;
      }
#ifdef WITH_OPENTIMS
      // A Bruker MALDI imaging .d (analysis.tdf, MaldiApplicationType == 'Imaging')
      // routes to the imaging pipeline; every other .d loads as a normal run.
      if (OpenMS::BrukerTimsImagingFile::isImagingDataset(path.toStdString()))
      {
        statusBar()->showMessage(tr("Opening MALDI imaging .d %1…").arg(QFileInfo(path).fileName()));
        imagingCancelled_ = false;
        beginOperation(ImagingOperation, tr("Loading MALDI imaging"),
                       tr("Opening %1").arg(QFileInfo(path).fileName()), false);
        imagingLoadWatcher_.setFuture(QtConcurrent::run(
          [path] { return ImagingDocument::readBrukerMaldi(path); }));
        updateLoadingUi();
        return;
      }
#endif
      statusBar()->showMessage(tr("Loading Bruker .d %1…").arg(QFileInfo(path).fileName()));
      mzMLLoadTimer_.start();
      mzMLCancellation_ = std::make_shared<std::atomic_bool>(false);
      beginOperation(MzMLOperation, tr("Loading Bruker .d"),
                     tr("Opening %1").arg(QFileInfo(path).fileName()), false);
      const auto cancellation = mzMLCancellation_;
      loadWatcher_.setFuture(QtConcurrent::run([path, cancellation]
      {
        return ViewerDocument::readBrukerTims(path, {},
          [cancellation] { return cancellation->load(); });
      }));
      updateLoadingUi();
      return;
    }
    // Everything else routes by detected semantic category — this covers the
    // XML formats (featureXML/idXML) and the new ones (featureparquet, idparquet,
    // mzIdentML, mzXML/mzData, sqMass) uniformly.
    const auto format = FormatRegistry::detect(path);
    if (format.supported)
    {
      switch (format.category)
      {
        case FormatRegistry::Category::Features:        loadFeatureData(path); return;
        case FormatRegistry::Category::Identifications: loadIdentificationData(path); return;
        case FormatRegistry::Category::Experiment:      loadExperimentData(path, format.type); return;
        case FormatRegistry::Category::Osw:             loadOswData(path); return;
        case FormatRegistry::Category::Consensus:       loadConsensusData(path); return;
        default: break;
      }
    }
    QMessageBox::warning(this, tr("Unsupported file"),
                         tr("OpenMS Viewer does not yet support '%1'.").arg(QFileInfo(path).fileName()));
  }

  void MainWindow::loadOswData(const QString& path)
  {
    if (oswLoadWatcher_.isRunning())
    {
      statusBar()->showMessage(tr("OpenSWATH results are already loading"), 3000);
      return;
    }
    statusBar()->showMessage(tr("Loading OpenSWATH results %1…").arg(QFileInfo(path).fileName()));
    beginOperation(OswOperation, tr("Loading OpenSWATH results"),
                   tr("Opening %1 and its .xic chromatograms").arg(QFileInfo(path).fileName()), false);
    oswLoadWatcher_.setFuture(QtConcurrent::run([path] { return OswDocument::read(path); }));
    updateLoadingUi();
  }

  void MainWindow::finishOswLoad()
  {
    OswLoadResult result = oswLoadWatcher_.result();
    endOperation(OswOperation);
    updateLoadingUi();
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("OpenSWATH load failed"), 5000);
      notify(tr("OpenSWATH load failed"), ToastLevel::Error);
      showOperationError(tr("Could not open OpenSWATH results"),
                         tr("The .osw results database could not be read."), result.error);
      return;
    }
    const QString sourcePath = result.sourcePath;
    const bool hasChromatograms = result.chromatograms != nullptr;
    osw_->setData(std::move(result.store), std::move(result.chromatograms), result.chromatogramNote);
    hasOswData_ = true;
    oswSourcePath_ = sourcePath;
    lastPrimaryPath_ = sourcePath;
    rememberRecentFile(sourcePath);
    showDataPage();
    dockVisibilityPreference_[oswDock_->objectName()] = true;
    setDockAvailable(oswDock_, true);
    oswDock_->raise();
    updateWindowTitle();
    updateRunContext();
    statusBar()->showMessage(tr("Loaded OpenSWATH results from %1").arg(QFileInfo(sourcePath).fileName()), 8000);
    notify(hasChromatograms ? tr("Loaded OpenSWATH results")
                            : tr("Loaded OpenSWATH results (no .xic chromatograms found)"),
           hasChromatograms ? ToastLevel::Success : ToastLevel::Warning);
  }

  void MainWindow::loadConsensusData(const QString& path)
  {
    if (consensusLoadWatcher_.isRunning())
    {
      statusBar()->showMessage(tr("A consensus map is already loading"), 3000);
      return;
    }
    statusBar()->showMessage(tr("Loading consensus map %1…").arg(QFileInfo(path).fileName()));
    beginOperation(ConsensusOperation, tr("Loading consensus map"),
                   tr("Reading %1").arg(QFileInfo(path).fileName()), false);
    consensusLoadWatcher_.setFuture(QtConcurrent::run([path] { return ConsensusDocument::read(path); }));
    updateLoadingUi();
  }

  void MainWindow::finishConsensusLoad()
  {
    ConsensusLoadResult result = consensusLoadWatcher_.result();
    endOperation(ConsensusOperation);
    updateLoadingUi();
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("Consensus map load failed"), 5000);
      notify(tr("Consensus map load failed"), ToastLevel::Error);
      showOperationError(tr("Could not open consensus map"),
                         tr("The consensus map could not be read."), result.error);
      return;
    }
    const QString sourcePath = result.sourcePath;
    const std::size_t featureCount = result.features.size();
    const std::size_t mapCount = result.columns.size();
    // Retain the map + columns + a copy of the feature summaries: the panel drives
    // the tables/chart while the peak map draws the overlay and MainWindow resolves
    // drill-down from a handle's map back to the loaded raw run.
    consensusMap_ = result.map;
    consensusColumns_ = result.columns;
    consensusSourcePath_ = sourcePath;
    peakMap_->setConsensusFeatures(result.features);
    showConsensusAction_->setEnabled(true);
    showConsensusAction_->setChecked(true);
    peakMap_->setShowConsensus(true);
    consensus_->setData(std::move(result.map), std::move(result.features),
                        std::move(result.columns), result.experimentType);
    hasConsensusData_ = true;
    saveConsensusAction_->setEnabled(true);
    lastPrimaryPath_ = sourcePath;
    rememberRecentFile(sourcePath);
    showDataPage();
    dockVisibilityPreference_[consensusDock_->objectName()] = true;
    setDockAvailable(consensusDock_, true);
    consensusDock_->raise();
    updateWindowTitle();
    updateRunContext();
    statusBar()->showMessage(tr("Loaded consensus map from %1").arg(QFileInfo(sourcePath).fileName()), 8000);
    notify(tr("Loaded %1 consensus features across %2 maps").arg(featureCount).arg(mapCount),
           ToastLevel::Success);
  }

  void MainWindow::loadFeatureData(const QString& path)
  {
    if (featureLoadWatcher_.isRunning())
    {
      statusBar()->showMessage(tr("A feature map is already loading"), 3000);
      return;
    }
    statusBar()->showMessage(tr("Loading feature overlay %1…").arg(QFileInfo(path).fileName()));
    featureCancelled_ = false;
    beginOperation(FeatureOperation, tr("Loading feature overlay"),
                   tr("Reading %1").arg(QFileInfo(path).fileName()));
    featureLoadWatcher_.setFuture(QtConcurrent::run([path] { return ViewerDocument::readFeatures(path); }));
    updateLoadingUi();
  }

  void MainWindow::loadIdentificationData(const QString& path)
  {
    if (identificationLoadWatcher_.isRunning())
    {
      statusBar()->showMessage(tr("Identifications are already loading"), 3000);
      return;
    }
    statusBar()->showMessage(tr("Loading identification overlay %1…").arg(QFileInfo(path).fileName()));
    identificationCancelled_ = false;
    beginOperation(IdentificationOperation, tr("Loading identification overlay"),
                   tr("Reading %1").arg(QFileInfo(path).fileName()));
    identificationLoadWatcher_.setFuture(QtConcurrent::run([path] { return ViewerDocument::readIdentifications(path); }));
    updateLoadingUi();
  }

  void MainWindow::loadExperimentData(const QString& path, int fileType)
  {
    if (loadWatcher_.isRunning() || imagingLoadWatcher_.isRunning())
    {
      statusBar()->showMessage(tr("A primary data file is already loading"), 3000);
      return;
    }
    statusBar()->showMessage(tr("Loading %1 with OpenMS…").arg(QFileInfo(path).fileName()));
    mzMLLoadTimer_.start();
    mzMLCancellation_ = std::make_shared<std::atomic_bool>(false);
    // FileHandler reads these formats in one call (no progress hook), so the
    // overlay is indeterminate + non-cancellable.
    beginOperation(MzMLOperation, tr("Loading spectra"),
                   tr("Opening %1").arg(QFileInfo(path).fileName()), false);
    const auto cancellation = mzMLCancellation_;
    loadWatcher_.setFuture(QtConcurrent::run([path, cancellation]
    {
      return ViewerDocument::readExperiment(path, {},
        [cancellation] { return cancellation->load(); });
    }));
    updateLoadingUi();
    // sqMass loads the whole experiment into memory — warn before a big read.
    if (fileType == static_cast<int>(OpenMS::FileTypes::SQMASS))
      notify(tr("sqMass loads fully into memory — large files may take a while"), ToastLevel::Warning);
  }

  void MainWindow::loadFiles(const QStringList& paths)
  {
    bool startedMzML = false;
    bool startedFeatures = false;
    bool startedIdentifications = false;
    for (const QString& path : paths)
    {
      const QString suffix = QFileInfo(path).suffix();
      if (suffix.compare(QStringLiteral("mzML"), Qt::CaseInsensitive) == 0)
      {
        if (startedMzML) continue;
        startedMzML = true;
      }
      else if (suffix.compare(QStringLiteral("imzML"), Qt::CaseInsensitive) == 0
               || (suffix.compare(QStringLiteral("raw"), Qt::CaseInsensitive) == 0
                   && QFileInfo(path).isFile())
               || (suffix.compare(QStringLiteral("d"), Qt::CaseInsensitive) == 0
                   && QFileInfo(path).isDir()))
      {
        // Only count a genuinely routable primary input, so an invalid .d file
        // (or .raw directory) never masks a valid sibling in the same batch.
        if (startedMzML) continue;
        startedMzML = true;
      }
      else
      {
        // Classify the remaining formats by detected category so a batch never
        // starts two loads of the same kind (featureXML/featureparquet →
        // features; idXML/mzid/idparquet → identifications; mzXML/mzData/sqMass →
        // primary spectra).
        const auto format = FormatRegistry::detect(path);
        if (format.supported && format.category == FormatRegistry::Category::Experiment)
        {
          if (startedMzML) continue;
          startedMzML = true;
        }
        else if (format.supported && format.category == FormatRegistry::Category::Features)
        {
          if (startedFeatures) continue;
          startedFeatures = true;
        }
        else if (format.supported && format.category == FormatRegistry::Category::Identifications)
        {
          if (startedIdentifications) continue;
          startedIdentifications = true;
        }
      }
      loadFile(path);
    }
  }

  void MainWindow::finishLoad()
  {
    ViewerDocument::LoadResult result = loadWatcher_.result();
    const bool cancelled = mzMLCancellation_ && mzMLCancellation_->load();
    endOperation(MzMLOperation);
    updateLoadingUi();
    mzMLCancellation_.reset();
    if (cancelled)
    {
      statusBar()->showMessage(tr("mzML loading cancelled; the previous data was kept"), 5000);
      notify(tr("mzML loading cancelled — previous data kept"), ToastLevel::Info);
      return;
    }
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("Load failed"), 5000);
      notify(tr("Load failed"), ToastLevel::Error);
      showOperationError(tr("Could not open file"), tr("The file could not be loaded."), result.error);
      return;
    }
    const QString sourcePath = result.sourcePath;
    selection_.clear();
    document_.adopt(std::move(result));
    if (!document_.hasFeatures())
    {
      featureUndoStack_->clear();
      featureSavePath_.clear();
    }
    imagingStore_.reset();
    imagingSummary_ = {};
    imaging_->clear();
    setDockAvailable(imagingDock_, false);
    lastPrimaryPath_ = sourcePath;
    rememberRecentFile(sourcePath);
    showDataPage();
    updateDocumentViews();
    notify(tr("Loaded %1 · %2 spectra, %3 peaks")
             .arg(QFileInfo(sourcePath).fileName())
             .arg(document_.statistics().spectrumCount)
             .arg(document_.statistics().peakCount),
           ToastLevel::Success);
    qInfo().noquote() << QStringLiteral(
      "Loaded mzML in %1 ms: %2 spectra, %3 peaks, %4 chromatograms, %5 IM frames, %6 FAIMS CVs")
      .arg(mzMLLoadTimer_.isValid() ? mzMLLoadTimer_.elapsed() : -1)
      .arg(document_.statistics().spectrumCount).arg(document_.statistics().peakCount)
      .arg(document_.chromatograms().size()).arg(document_.ionMobilityFrames().size())
      .arg(document_.faimsChannels().size());
    if (document_.hasIdentifications())
    {
      const std::size_t linked = static_cast<std::size_t>(std::count_if(
        document_.identifications().begin(), document_.identifications().end(),
        [](const IdentificationRecord& identification) { return identification.spectrumIndex.has_value(); }));
      qInfo().noquote() << QStringLiteral("Relinked idXML after mzML load: %1 linked spectra").arg(linked);
    }
  }

  void MainWindow::finishFeatureLoad()
  {
    ViewerDocument::FeatureLoadResult result = featureLoadWatcher_.result();
    endOperation(FeatureOperation);
    updateLoadingUi();
    if (featureCancelled_)
    {
      featureCancelled_ = false;
      statusBar()->showMessage(tr("Feature overlay loading cancelled"), 5000);
      notify(tr("Feature overlay loading cancelled"), ToastLevel::Info);
      return;
    }
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("Feature map load failed"), 5000);
      notify(tr("Feature map load failed"), ToastLevel::Error);
      showOperationError(tr("Could not open feature overlay"),
                         tr("The feature map could not be loaded."), result.error);
      return;
    }
    const std::size_t count = result.features.size();
    const QString filename = QFileInfo(result.sourcePath).fileName();
    const QString sourcePath = result.sourcePath;
    document_.adoptFeatures(std::move(result));
    featureSavePath_ = sourcePath;
    featureUndoStack_->clear();
    featureUndoStack_->setClean();
    rememberRecentFile(sourcePath);
    showDataPage();
    updateRunContext();
    qInfo().noquote() << QStringLiteral("Loaded FeatureXML: %1 features").arg(count);
    statusBar()->showMessage(tr("Loaded %1 features from %2").arg(count).arg(filename), 8000);
    notify(tr("Loaded %1 features from %2").arg(count).arg(filename), ToastLevel::Success);
  }

  void MainWindow::finishIdentificationLoad()
  {
    ViewerDocument::IdentificationLoadResult result = identificationLoadWatcher_.result();
    endOperation(IdentificationOperation);
    updateLoadingUi();
    if (identificationCancelled_)
    {
      identificationCancelled_ = false;
      statusBar()->showMessage(tr("Identification overlay loading cancelled"), 5000);
      notify(tr("Identification overlay loading cancelled"), ToastLevel::Info);
      return;
    }
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("Identification load failed"), 5000);
      notify(tr("Identification load failed"), ToastLevel::Error);
      showOperationError(tr("Could not open identification overlay"),
                         tr("The identifications could not be loaded."), result.error);
      return;
    }
    const std::size_t count = result.identifications.size();
    const QString filename = QFileInfo(result.sourcePath).fileName();
    const QString sourcePath = result.sourcePath;
    document_.adoptIdentifications(std::move(result));
    rememberRecentFile(sourcePath);
    updateRunContext();
    const std::size_t linked = static_cast<std::size_t>(std::count_if(
      document_.identifications().begin(), document_.identifications().end(),
      [](const IdentificationRecord& identification) { return identification.spectrumIndex.has_value(); }));
    qInfo().noquote() << QStringLiteral("Loaded idXML: %1 identifications, %2 linked spectra")
      .arg(count).arg(linked);
    statusBar()->showMessage(tr("Loaded %1 identifications from %2 · %3 linked spectra")
      .arg(count).arg(filename).arg(linked), 8000);
    notify(tr("Loaded %1 identifications · %2 linked spectra").arg(count).arg(linked),
           ToastLevel::Success);
  }

  void MainWindow::finishImagingLoad()
  {
    ImagingLoadResult result = imagingLoadWatcher_.result();
    endOperation(ImagingOperation);
    updateLoadingUi();
    if (imagingCancelled_)
    {
      imagingCancelled_ = false;
      statusBar()->showMessage(tr("Imaging-data loading cancelled; the previous data was kept"), 5000);
      notify(tr("Imaging-data loading cancelled — previous data kept"), ToastLevel::Info);
      return;
    }
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("imzML load failed"), 5000);
      notify(tr("imzML load failed"), ToastLevel::Error);
      showOperationError(tr("Could not open imaging dataset"),
                         tr("The imzML imaging dataset could not be loaded."), result.error);
      return;
    }

    document_.clear();
    featureUndoStack_->clear();
    featureSavePath_.clear();
    closeSurface3D();
    peakMap_->clear();
    peakMap_->setPrecursorMarkers({});  // the imaging run has no MS/MS precursors
    showPrecursorsAction_->setEnabled(false);
    metadata_->clear();
    setDockAvailable(metadataDock_, false);
    tic_->clear();
    spectrum_->clear();
    spectra_->clear();
    chromatograms_->clear();
    ionMobility_->clear();
    faims_->clear();
    setDockAvailable(ticDock_, false);
    setDockAvailable(spectraDock_, false);
    setDockAvailable(chromatogramsDock_, false);
    setDockAvailable(ionMobilityDock_, false);
    setDockAvailable(faimsDock_, false);
    setDockAvailable(featuresDock_, false);
    setDockAvailable(identificationsDock_, false);
    exportMzMLAction_->setEnabled(false);
    selection_.clear();

    const QString sourcePath = result.summary.sourcePath;
    imagingStore_ = std::move(result.store);
    imagingSummary_ = std::move(result.summary);
    imaging_->setData(imagingStore_, imagingSummary_);
    lastPrimaryPath_ = sourcePath;
    rememberRecentFile(sourcePath);
    showDataPage();
    setPeakMapControlsEnabled(false);
    setDockAvailable(imagingDock_, true);
    setDockAvailable(spectrumDock_, true);
    if (dockVisibilityPreference_.value(imagingDock_->objectName(), true)) imagingDock_->raise();
    if (!imagingSummary_.pixels.empty())
      selectImagingSpectrum(imagingSummary_.pixels.front().spectrumIndex);
    updateWindowTitle();
    updateRunContext();
    updateSpectrumControls();
    statusBar()->showMessage(tr("Loaded %1 imaging pixels (%2 × %3), %4 peaks")
      .arg(imagingSummary_.pixels.size()).arg(imagingSummary_.width).arg(imagingSummary_.height)
      .arg(imagingSummary_.peakCount), 8000);
    notify(tr("Loaded %1 imaging pixels (%2 × %3)")
             .arg(imagingSummary_.pixels.size()).arg(imagingSummary_.width).arg(imagingSummary_.height),
           ToastLevel::Success);
    qInfo().noquote() << QStringLiteral(
      "Loaded imzML: %1 pixels, %2x%3 grid, %4 peaks, m/z %5-%6")
      .arg(imagingSummary_.pixels.size()).arg(imagingSummary_.width).arg(imagingSummary_.height)
      .arg(imagingSummary_.peakCount).arg(imagingSummary_.mzMin, 0, 'f', 4)
      .arg(imagingSummary_.mzMax, 0, 'f', 4);
  }

  void MainWindow::updateDocumentViews()
  {
    const auto experiment = document_.experimentHandle();
    closeSurface3D();   // the previous run's surface no longer applies
    peakMap_->setExperiment(experiment, document_.bounds());

    // MS/MS precursor + isolation-window overlay markers (one per MS2+ scan).
    std::vector<PrecursorMarker> precursorMarkers;
    for (const SpectrumRecord& spectrum : document_.spectra())
    {
      if (spectrum.msLevel <= 1 || !spectrum.precursorMz) continue;
      PrecursorMarker marker;
      marker.spectrumIndex = spectrum.index;
      marker.rt = spectrum.rt;
      marker.mz = *spectrum.precursorMz;
      marker.lowerMz = *spectrum.precursorMz - spectrum.isolationLowerOffset;
      marker.upperMz = *spectrum.precursorMz + spectrum.isolationUpperOffset;
      marker.charge = spectrum.precursorCharge;
      marker.msLevel = spectrum.msLevel;
      precursorMarkers.push_back(marker);
    }
    const bool hasPrecursors = !precursorMarkers.empty();
    peakMap_->setPrecursorMarkers(std::move(precursorMarkers));
    showPrecursorsAction_->setEnabled(hasPrecursors);
    setPeakMapControlsEnabled(experiment != nullptr);
    tic_->setTrace(document_.tic(), document_.ticLabel());
    tic_->setPeakMapRange(document_.bounds());
    spectrum_->setExperiment(experiment);
    spectra_->setData(document_.spectra(), document_.identifications());
    const bool hasSpectra = !document_.spectra().empty();
    setDockAvailable(ticDock_, hasSpectra);
    setDockAvailable(spectrumDock_, hasSpectra);
    setDockAvailable(spectraDock_, hasSpectra);
    metadata_->setExperiment(experiment);
    setDockAvailable(metadataDock_, experiment != nullptr);
    chromatograms_->setChromatograms(document_.chromatograms());
    chromatograms_->setPeakMapRange(document_.bounds());
    setDockAvailable(chromatogramsDock_, document_.hasChromatograms());
    exportMzMLAction_->setEnabled(!document_.spectra().empty());
    ionMobility_->setData(experiment, document_.ionMobilityFrames());
    setDockAvailable(ionMobilityDock_, document_.hasIonMobility());
    selection_.setFaimsChannel(-1);
    faims_->setChannels(document_.faimsChannels());
    std::vector<std::shared_ptr<const OpenMS::MSExperiment>> faimsExperiments;
    faimsExperiments.reserve(document_.faimsChannels().size());
    for (std::size_t index = 0; index < document_.faimsChannels().size(); ++index)
      faimsExperiments.push_back(document_.faimsExperiment(index));
    faims_->setExperiments(faimsExperiments, document_.bounds());
    faims_->setPeakMapRange(document_.bounds());
    setDockAvailable(faimsDock_, document_.hasFaims());
    updateFeatureViews();
    updateIdentificationViews();

    auto first = document_.edgeSpectrumIndex(false, 1);
    if (!first) first = document_.edgeSpectrumIndex(false);
    if (first) selectSpectrum(*first);

    updateRunContext();
    updateSpectrumControls();

    const auto& stats = document_.statistics();
    updateWindowTitle();
    statusBar()->showMessage(
      tr("Loaded %1 spectra (%2 MS1), %3 peaks, %4 chromatograms, %5 IM frames, %6 FAIMS CVs")
        .arg(stats.spectrumCount)
        .arg(stats.ms1SpectrumCount)
        .arg(stats.peakCount)
        .arg(document_.chromatograms().size())
        .arg(document_.ionMobilityFrames().size())
        .arg(document_.faimsChannels().size()), 8000);
  }

  void MainWindow::updateFeatureViews()
  {
    const bool available = document_.hasFeatures();
    peakMap_->setFeatures(document_.features());
    features_->setFeatures(document_.features());
    peakMap_->setSelectedFeature(std::nullopt);
    selection_.setFeature(std::nullopt);
    showCentroidsAction_->setEnabled(available);
    showFeatureBoundsAction_->setEnabled(available);
    showFeatureHullsAction_->setEnabled(available);
    saveFeaturesAction_->setEnabled(available);
    setDockAvailable(featuresDock_, available);
    updateRunContext();
  }

  void MainWindow::updateIdentificationViews()
  {
    const bool available = document_.hasIdentifications();
    peakMap_->setIdentifications(document_.identifications());
    identifications_->setIdentifications(document_.identifications());
    spectra_->setData(document_.spectra(), document_.identifications());
    peakMap_->setSelectedIdentification(std::nullopt);
    selection_.setIdentification(std::nullopt);
    showIdentificationsAction_->setEnabled(available);
    showIdentificationSequencesAction_->setEnabled(available);
    saveIdentificationsAction_->setEnabled(available);
    setDockAvailable(identificationsDock_, available);
    updateRunContext();
    reconcileSelection();  // re-link the current spectrum to newly loaded IDs
  }

  void MainWindow::updateLoadingUi()
  {
    const bool busy = loadWatcher_.isRunning() || featureLoadWatcher_.isRunning()
      || identificationLoadWatcher_.isRunning() || mzMLExportWatcher_.isRunning()
      || imagingLoadWatcher_.isRunning() || oswLoadWatcher_.isRunning()
      || consensusLoadWatcher_.isRunning();
    openAction_->setEnabled(!busy);
    reloadAction_->setEnabled(!busy && !lastPrimaryPath_.isEmpty()
                              && QFileInfo::exists(lastPrimaryPath_));
    exportMzMLAction_->setEnabled(!busy && !document_.spectra().empty());
    if (busy)
    {
      progress_->setRange(0, 0);
      progress_->setTextVisible(false);
      progress_->show();
    }
    else
    {
      progress_->hide();
    }
  }

  void MainWindow::exportMzML()
  {
    if (document_.spectra().empty() || mzMLExportWatcher_.isRunning()) return;
    std::set<unsigned int> levels;
    for (const SpectrumRecord& spectrum : document_.spectra()) levels.insert(spectrum.msLevel);
    std::optional<double> activeCv;
    if (selection_.faimsChannel() >= 0
        && static_cast<std::size_t>(selection_.faimsChannel()) < document_.faimsChannels().size())
      activeCv = document_.faimsChannels()[static_cast<std::size_t>(selection_.faimsChannel())].compensationVoltage;
    MzMLExportDialog dialog(document_.bounds(), peakMap_->viewRange(), levels,
                            document_.spectra(), activeCv, this, rtInMinutes());
    if (dialog.exec() != QDialog::Accepted) return;

    const QFileInfo source(document_.sourcePath());
    const QString suggested = source.dir().filePath(source.completeBaseName() + QStringLiteral("_filtered.mzML"));
    const QString outputPath = QFileDialog::getSaveFileName(
      this, tr("Export filtered mzML"), suggested, tr("mzML files (*.mzML);;All files (*)"));
    if (outputPath.isEmpty()) return;
    const auto experiment = document_.experimentHandle();
    const MzMLExportFilter settings = dialog.filter();
    statusBar()->showMessage(tr("Exporting filtered mzML…"));
    exportCancelled_ = false;
    beginOperation(ExportOperation, tr("Exporting mzML"),
                   tr("Writing %1").arg(QFileInfo(outputPath).fileName()), false);
    mzMLExportWatcher_.setFuture(QtConcurrent::run([experiment, outputPath, settings]
    {
      return MzMLExporter::write(experiment, outputPath, settings);
    }));
    updateLoadingUi();
  }

  // Ask for a save path, ensuring the native suffix is present (OpenMS writers pick
  // the format from the extension, so a path without one would fail).
  QString MainWindow::askSavePath(const QString& title, const QString& sourcePath,
                                  const QString& fallbackStem, const QString& suffix)
  {
    const QFileInfo source(sourcePath);
    const QString stem = source.completeBaseName().isEmpty() ? fallbackStem : source.completeBaseName();
    const QString suggested = source.dir().filePath(stem + suffix);
    const QString filter = tr("%1 (*%2);;All files (*)").arg(suffix.mid(1), suffix);
    QString path = QFileDialog::getSaveFileName(this, title, suggested, filter);
    if (!path.isEmpty() && !path.endsWith(suffix, Qt::CaseInsensitive)) path += suffix;
    return path;
  }

  bool MainWindow::saveFeatures(bool choosePath)
  {
    if (!document_.hasFeatures()) return false;
    QString path = featureSavePath_.isEmpty() ? document_.featuresPath() : featureSavePath_;
    if (choosePath || path.isEmpty())
      path = askSavePath(tr("Save features"), path,
                         QStringLiteral("features"), QStringLiteral(".featureXML"));
    if (path.isEmpty()) return false;
    statusBar()->showMessage(tr("Saving features…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);  // large maps write on the GUI thread
    QString error;
    const bool saved = document_.saveFeatures(path, error);
    QApplication::restoreOverrideCursor();
    if (saved)
    {
      featureSavePath_ = QFileInfo(path).absoluteFilePath();
      featureUndoStack_->setClean();
      updateFeatureEditState();
      notify(tr("Saved features to %1").arg(QFileInfo(path).fileName()), ToastLevel::Success);
    }
    else
      showOperationError(tr("Could not save features"), tr("Writing the feature map failed."), error);
    return saved;
  }

  bool MainWindow::confirmFeatureChanges(const QString& action)
  {
    if (!featureUndoStack_ || featureUndoStack_->isClean()) return true;
    QMessageBox prompt(QMessageBox::Warning, tr("Unsaved feature changes"),
      tr("Save the changes to the feature layer before you %1?").arg(action),
      QMessageBox::NoButton, this);
    QPushButton* save = prompt.addButton(tr("Save"), QMessageBox::AcceptRole);
    QPushButton* discard = prompt.addButton(tr("Discard changes"), QMessageBox::DestructiveRole);
    QPushButton* cancel = prompt.addButton(QMessageBox::Cancel);
    prompt.setDefaultButton(save);
    prompt.exec();
    if (prompt.clickedButton() == save) return saveFeatures(false);
    if (prompt.clickedButton() == cancel) return false;
    if (prompt.clickedButton() == discard)
    {
      selection_.setFeature(std::nullopt);
      document_.clearFeatures();
      featureUndoStack_->clear();
      featureSavePath_.clear();
      updateFeatureEditState();
      return true;
    }
    return false;
  }

  bool MainWindow::editedFeaturesWouldBeReplacedBy(const QString& path) const
  {
    if (!featureUndoStack_ || featureUndoStack_->isClean() || !document_.hasFeatures()) return false;
    const QFileInfo input(path);
    const auto format = FormatRegistry::detect(path);
    if (format.supported && format.category == FormatRegistry::Category::Features) return true;

    const QString suffix = input.suffix().toLower();
    const bool primaryInput = suffix == QStringLiteral("mzml")
      || suffix == QStringLiteral("mzxml") || suffix == QStringLiteral("mzdata")
      || suffix == QStringLiteral("sqmass") || suffix == QStringLiteral("raw")
      || suffix == QStringLiteral("d") || suffix == QStringLiteral("imzml")
      || (format.supported && format.category == FormatRegistry::Category::Experiment);
    if (!primaryInput) return false;
    // Imaging adoption clears the in-memory ViewerDocument even when no spectra run
    // was previously loaded, so a feature-only session still needs protection.
    if (suffix == QStringLiteral("imzml")) return true;
#ifdef WITH_OPENTIMS
    if (suffix == QStringLiteral("d")
        && OpenMS::BrukerTimsImagingFile::isImagingDataset(path.toStdString()))
      return true;
#endif
    if (document_.sourcePath().isEmpty()) return false;
    return QFileInfo(document_.sourcePath()).absoluteFilePath() != input.absoluteFilePath();
  }

  void MainWindow::closeData()
  {
    if (!confirmFeatureChanges(tr("close the data session"))) return;
    document_.clear();
    featureUndoStack_->clear();
    featureSavePath_.clear();
    imagingStore_.reset();
    imagingSummary_ = {};
    closeSurface3D();
    peakMap_->clear();
    tic_->clear();
    spectrum_->clear();
    spectra_->clear();
    chromatograms_->clear();
    ionMobility_->clear();
    faims_->clear();
    imaging_->clear();
    selection_.clear();
    showWelcomePage();
    updateWindowTitle();
    updateRunContext();
    updateSpectrumControls();
  }

  void MainWindow::removeLayer(int layerValue)
  {
    const auto layer = static_cast<DataLayersWidget::Layer>(layerValue);
    switch (layer)
    {
      case DataLayersWidget::Layer::Primary:
        closeData();
        return;
      case DataLayersWidget::Layer::Features:
        if (!confirmFeatureChanges(tr("remove the feature layer"))) return;
        document_.clearFeatures();
        featureUndoStack_->clear();
        featureSavePath_.clear();
        break;
      case DataLayersWidget::Layer::Identifications:
        document_.clearIdentifications();
        break;
      case DataLayersWidget::Layer::Consensus:
        hasConsensusData_ = false;
        consensusMap_.reset();
        consensusColumns_.clear();
        consensusSourcePath_.clear();
        consensus_->clear();
        peakMap_->setConsensusFeatures({});
        showConsensusAction_->setChecked(false);
        showConsensusAction_->setEnabled(false);
        setDockAvailable(consensusDock_, false);
        break;
      case DataLayersWidget::Layer::OpenSwath:
        hasOswData_ = false;
        oswSourcePath_.clear();
        osw_->clear();
        setDockAvailable(oswDock_, false);
        break;
      case DataLayersWidget::Layer::Count:
        return;
    }
    const bool anything = imagingStore_ || !document_.isEmpty() || document_.hasFeatures()
      || document_.hasIdentifications() || hasConsensusData_ || hasOswData_;
    if (!anything) showWelcomePage();
    updateRunContext();
    updateWindowTitle();
    updateSaveActions();
  }

  void MainWindow::saveIdentifications()
  {
    if (!document_.hasIdentifications()) return;
    const QString path = askSavePath(tr("Save identifications"), document_.identificationsPath(),
                                     QStringLiteral("identifications"), QStringLiteral(".idXML"));
    if (path.isEmpty()) return;
    statusBar()->showMessage(tr("Saving identifications…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool saved = document_.saveIdentifications(path, error);
    QApplication::restoreOverrideCursor();
    if (saved)
      notify(tr("Saved identifications to %1").arg(QFileInfo(path).fileName()), ToastLevel::Success);
    else
      showOperationError(tr("Could not save identifications"),
                         tr("Writing the identifications failed."), error);
  }

  void MainWindow::saveConsensus()
  {
    if (!consensusMap_) return;
    const QString path = askSavePath(tr("Save consensus map"), consensusSourcePath_,
                                     QStringLiteral("consensus"), QStringLiteral(".consensusXML"));
    if (path.isEmpty()) return;
    statusBar()->showMessage(tr("Saving consensus map…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool saved = ConsensusDocument::save(*consensusMap_, path, error);
    QApplication::restoreOverrideCursor();
    if (saved)
      notify(tr("Saved consensus map to %1").arg(QFileInfo(path).fileName()), ToastLevel::Success);
    else
      showOperationError(tr("Could not save consensus map"),
                         tr("Writing the consensus map failed."), error);
  }

  void MainWindow::finishMzMLExport()
  {
    const MzMLExportResult result = mzMLExportWatcher_.result();
    endOperation(ExportOperation);
    updateLoadingUi();
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("mzML export failed"), 5000);
      notify(tr("mzML export failed"), ToastLevel::Error);
      showOperationError(tr("Could not export mzML"), tr("The filtered mzML export failed."), result.error);
      return;
    }
    statusBar()->showMessage(tr("Exported %1 spectra and %2 peaks to %3")
      .arg(result.spectrumCount).arg(result.peakCount).arg(QFileInfo(result.outputPath).fileName()), 8000);
    notify(tr("Exported %1 spectra to %2")
             .arg(result.spectrumCount).arg(QFileInfo(result.outputPath).fileName()),
           ToastLevel::Success);
    qInfo().noquote() << QStringLiteral("Exported filtered mzML: %1 spectra, %2 peaks, %3")
      .arg(result.spectrumCount).arg(result.peakCount).arg(result.outputPath);
  }

  void MainWindow::savePlot(QWidget* widget, const QString& defaultName)
  {
    if (!widget) return;
    const QString directory = document_.sourcePath().isEmpty()
      ? QString() : QFileInfo(document_.sourcePath()).absolutePath();
    const QString suggested = directory.isEmpty() ? defaultName : QDir(directory).filePath(defaultName);
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(
      this, tr("Export plot"), suggested,
      tr("PNG image (*.png);;SVG vector image (*.svg);;All files (*)"), &selectedFilter);
    if (path.isEmpty()) return;

    // Choose the writer by the file's extension; if the user typed none, follow the
    // filter they picked and append the matching suffix. SVG keeps axes, sticks and
    // labels scalable (vector); PNG is a pixel snapshot.
    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty())
    {
      const bool wantSvg = selectedFilter.contains(QStringLiteral("svg"), Qt::CaseInsensitive);
      suffix = wantSvg ? QStringLiteral("svg") : QStringLiteral("png");
      path += QLatin1Char('.') + suffix;
    }
    const bool svg = suffix == QStringLiteral("svg");
    const QString error = svg ? PlotExporter::writeSvg(*widget, path)
                              : PlotExporter::writePng(*widget, path);
    if (!error.isEmpty())
    {
      showOperationError(tr("Could not export plot"), tr("The plot could not be written."), error);
      return;
    }
    statusBar()->showMessage(tr("Saved plot to %1").arg(QFileInfo(path).fileName()), 5000);
  }

  void MainWindow::selectSpectrum(std::size_t index)
  {
    const auto* selected = document_.spectrum(index);
    if (!selected) return;

    // FAIMS consistency (imperative policy): if a channel is active and the
    // target spectrum is on a different CV, correct the channel first without
    // re-selecting a spectrum.
    if (selection_.faimsChannel() >= 0)
    {
      const SpectrumRecord* record = document_.spectrumRecord(index);
      const auto& channels = document_.faimsChannels();
      const std::size_t active = static_cast<std::size_t>(selection_.faimsChannel());
      if (!record || !record->compensationVoltage)
      {
        applyFaimsChannel(-1, false);
      }
      else if (active >= channels.size()
               || std::abs(channels[active].compensationVoltage
                           - *record->compensationVoltage) > 1e-6)
      {
        const auto channel = std::find_if(channels.cbegin(), channels.cend(),
          [record](const FaimsChannelRecord& candidate)
          {
            return std::abs(candidate.compensationVoltage
                            - *record->compensationVoltage) <= 1e-6;
          });
        applyFaimsChannel(channel == channels.cend()
                            ? -1 : static_cast<int>(std::distance(channels.cbegin(), channel)),
                          false);
      }
    }

    // Panels refresh through applySpectrumSelection (spectrumChanged signal).
    selection_.setSpectrum(index);
    // Keep the selection visible in the peak map from every source (including
    // re-selecting the current spectrum), preserving the current RT span.
    const PlotRange view = peakMap_->viewRange();
    if (selected->getRT() < view.rtMin || selected->getRT() > view.rtMax)
    {
      const double halfSpan = std::max(view.rtSpan(), 1.0) * 0.5;
      peakMap_->setRtRange(selected->getRT() - halfSpan, selected->getRT() + halfSpan);
    }
  }

  void MainWindow::applySpectrumSelection(qint64 index)
  {
    if (index < 0) return;
    // Imaging pixels are a separate selection domain handled by
    // selectImagingSpectrum; the document is empty in imaging mode.
    if (imagingStore_) return;
    const auto spectrumIndex = static_cast<std::size_t>(index);
    const auto* selected = document_.spectrum(spectrumIndex);
    if (!selected) return;

    updateSpectrumControls();
    spectrum_->setSpectrumIndex(spectrumIndex);
    metadata_->setSpectrumIndex(spectrumIndex);
    tic_->setSelectedSpectrum(spectrumIndex);
    tic_->setSelectedRt(selected->getRT());
    std::optional<double> precursorMz;
    if (selected->getMSLevel() >= 2 && !selected->getPrecursors().empty())
      precursorMz = selected->getPrecursors().front().getMZ();
    peakMap_->setSpectrumMarker(selected->getRT(),
                                static_cast<int>(selected->getMSLevel()), precursorMz);
    ionMobility_->setSpectrumIndex(spectrumIndex);

    // Derive the identification for this spectrum (highlight only, no zoom).
    const IdentificationRecord* identification = nullptr;
    if (selection_.identification())
    {
      const IdentificationRecord* current = document_.identification(*selection_.identification());
      if (current && current->spectrumIndex && *current->spectrumIndex == spectrumIndex
          && !current->hits.empty()) identification = current;
    }
    if (!identification) identification = document_.identificationForSpectrum(spectrumIndex);

    if (identification && !identification->hits.empty())
    {
      const std::size_t hit = selection_.identification()
        && *selection_.identification() == identification->index && selection_.hit()
          ? std::min(*selection_.hit(), identification->hits.size() - 1) : 0;
      selection_.setIdentification(identification->index, hit);  // -> applyIdentificationSelection
      spectra_->selectSpectrum(spectrumIndex, identification->index, hit);
    }
    else
    {
      selection_.setIdentification(std::nullopt);  // -> applyIdentificationSelection(-1, -1)
      spectra_->selectSpectrum(spectrumIndex);
    }
    updateSpectrumAnnotation();
    updateSpectrumStatus();
  }

  void MainWindow::reconcileSelection()
  {
    // Force a panel refresh for the current spectrum even though its index is
    // unchanged (e.g. after new identifications load, or a FAIMS view switch).
    if (selection_.spectrum())
      applySpectrumSelection(static_cast<qint64>(*selection_.spectrum()));
  }

  void MainWindow::applyPeakMapViewRange(const PlotRange& range)
  {
    tic_->setPeakMapRange(range);
    chromatograms_->setPeakMapRange(range);
    faims_->setPeakMapRange(range);
    ionMobility_->setPeakMapMzRange(range.mzMin, range.mzMax);
    viewContext_->setText(tr("View RT %1–%2 %3 · m/z %4–%5")
      .arg(RtUnit::format(range.rtMin, rtInMinutes(), 1), RtUnit::format(range.rtMax, rtInMinutes(), 1),
           RtUnit::unit(rtInMinutes()))
      .arg(range.mzMin, 0, 'f', 2).arg(range.mzMax, 0, 'f', 2));
  }

  void MainWindow::selectSpectrumHit(std::size_t spectrumIndex, int identificationIndex,
                                     int hitIndex)
  {
    selectSpectrum(spectrumIndex);
    if (identificationIndex < 0) return;
    const auto idIndex = static_cast<std::size_t>(identificationIndex);
    const IdentificationRecord* identification = document_.identification(idIndex);
    if (!identification || !identification->spectrumIndex
        || *identification->spectrumIndex != spectrumIndex || identification->hits.empty()) return;
    const std::size_t selectedHit = hitIndex >= 0
      ? std::min(static_cast<std::size_t>(hitIndex), identification->hits.size() - 1) : 0;
    selection_.setIdentification(idIndex, selectedHit);  // -> applyIdentificationSelection
    spectra_->selectSpectrum(spectrumIndex, idIndex, selectedHit);
    updateSpectrumAnnotation();
  }

  void MainWindow::selectImagingSpectrum(std::size_t index)
  {
    if (!imagingStore_ || index >= imagingStore_->spectrumCount()) return;
    try
    {
      OpenMS::MSSpectrum selected = imagingStore_->spectrum(index);
      if (selected.getMSLevel() == 0) selected.setMSLevel(1);
      selection_.setSpectrum(index);
      selection_.setIdentification(std::nullopt);
      spectrum_->setStandaloneSpectrum(std::move(selected), index, imagingStore_->spectrumCount());
      imaging_->setSelectedSpectrum(index);
      updateSpectrumControls();
      selectionContext_->setText(tr("Pixel spectrum %1/%2")
        .arg(index + 1).arg(imagingStore_->spectrumCount()));
      statusBar()->showMessage(tr("Imaging pixel spectrum %1/%2")
        .arg(index + 1).arg(imagingStore_->spectrumCount()), 5000);
    }
    catch (const std::exception& error)
    {
      statusBar()->showMessage(tr("Could not decode imaging spectrum: %1")
        .arg(QString::fromLocal8Bit(error.what())), 5000);
    }
  }

  void MainWindow::selectNearestSpectrum(double rt)
  {
    if (selection_.faimsChannel() >= 0
        && static_cast<std::size_t>(selection_.faimsChannel()) < document_.faimsChannels().size())
    {
      const auto& trace = document_.faimsChannels()[static_cast<std::size_t>(selection_.faimsChannel())].tic;
      if (!trace.empty())
      {
        const auto found = std::lower_bound(trace.begin(), trace.end(), rt,
          [](const FaimsTracePoint& point, double value) { return point.rt < value; });
        if (found == trace.begin()) selectSpectrum(found->spectrumIndex);
        else if (found == trace.end()) selectSpectrum(trace.back().spectrumIndex);
        else
        {
          const auto previous = found - 1;
          selectSpectrum(std::abs(found->rt - rt) < std::abs(previous->rt - rt)
                           ? found->spectrumIndex : previous->spectrumIndex);
        }
        return;
      }
    }
    auto index = document_.nearestSpectrumIndex(rt, 1);
    if (!index) index = document_.nearestSpectrumIndex(rt);
    if (index) selectSpectrum(*index);
  }

  void MainWindow::setFaimsChannel(int channelIndex)
  {
    applyFaimsChannel(channelIndex, true);
  }

  void MainWindow::applyFaimsChannel(int channelIndex, bool selectNearest)
  {
    closeSurface3D();   // the peak map's experiment is changing (channel filter)
    if (channelIndex < 0 || static_cast<std::size_t>(channelIndex) >= document_.faimsChannels().size())
    {
      selection_.setFaimsChannel(-1);
      faims_->setSelectedChannel(-1);
      peakMap_->setExperiment(document_.experimentHandle(), document_.bounds());
      tic_->setTrace(document_.tic(), document_.ticLabel());
      if (selectNearest) reconcileSelection();  // refresh panels for the unfiltered view
      statusBar()->showMessage(tr("Showing all FAIMS compensation voltages"), 4000);
      return;
    }

    selection_.setFaimsChannel(channelIndex);
    faims_->setSelectedChannel(channelIndex);
    const auto& channel = document_.faimsChannels()[static_cast<std::size_t>(channelIndex)];
    peakMap_->setExperiment(document_.faimsExperiment(static_cast<std::size_t>(channelIndex)),
                            document_.bounds());
    std::vector<TicPoint> trace;
    trace.reserve(channel.tic.size());
    for (const FaimsTracePoint& point : channel.tic)
      trace.push_back({point.rt, point.intensity, point.spectrumIndex});
    tic_->setTrace(std::move(trace), tr("MS1 TIC · CV %1 V").arg(channel.compensationVoltage, 0, 'f', 1));
    if (selectNearest && !channel.tic.empty())
    {
      const double targetRt = selection_.spectrum() && document_.spectrum(*selection_.spectrum())
        ? document_.spectrum(*selection_.spectrum())->getRT() : channel.tic.front().rt;
      selectNearestSpectrum(targetRt);
    }
    statusBar()->showMessage(tr("FAIMS filter: CV %1 V · %2 MS1 scans")
      .arg(channel.compensationVoltage, 0, 'f', 1).arg(channel.tic.size()), 5000);
  }

  void MainWindow::selectFeature(std::size_t index)
  {
    if (!document_.feature(index)) return;
    selection_.setFeature(index);  // -> applyFeatureSelection
  }

  void MainWindow::applyFeatureSelection(qint64 index)
  {
    if (index < 0)
    {
      peakMap_->setSelectedFeature(std::nullopt);
      return;
    }
    const auto featureIndex = static_cast<std::size_t>(index);
    const FeatureRecord* feature = document_.feature(featureIndex);
    if (!feature) return;
    peakMap_->setSelectedFeature(featureIndex);
    features_->selectFeature(featureIndex);
    statusBar()->showMessage(tr("Feature %1 · RT %2 %3 · m/z %4 · intensity %5 · charge %6")
      .arg(featureIndex)
      .arg(RtUnit::format(feature->rt, rtInMinutes()), RtUnit::unit(rtInMinutes()))
      .arg(feature->mz, 0, 'f', 4)
      .arg(feature->intensity, 0, 'e', 2)
      .arg(feature->charge), 6000);
  }

  void MainWindow::selectFeatureAndZoom(std::size_t index)
  {
    selectFeature(index);
    peakMap_->zoomToFeature(index);
  }

  void MainWindow::applyIdentificationSelection(qint64 index, qint64 hitIndex)
  {
    if (index < 0)
    {
      peakMap_->setSelectedIdentification(std::nullopt);
      return;
    }
    const auto idIndex = static_cast<std::size_t>(index);
    peakMap_->setSelectedIdentification(idIndex);
    identifications_->selectIdentification(idIndex,
      hitIndex >= 0 ? static_cast<std::size_t>(hitIndex) : 0);
  }

  void MainWindow::selectIdentification(std::size_t index, std::size_t hitIndex)
  {
    const IdentificationRecord* identification = document_.identification(index);
    if (!identification || hitIndex >= identification->hits.size()) return;
    selection_.setIdentification(index, hitIndex);  // -> applyIdentificationSelection
    if (identification->spectrumIndex) selectSpectrum(*identification->spectrumIndex);
    // Reflect the chosen hit even when the linked spectrum is already selected
    // (selectSpectrum de-dups, so its applier would not re-annotate).
    updateSpectrumAnnotation();
    peakMap_->zoomToIdentification(index);
    const PeptideHitRecord& hit = identification->hits[hitIndex];
    statusBar()->showMessage(tr("Identification %1 · %2 · z=%3 · score %4%5")
      .arg(index).arg(hit.sequence).arg(hit.charge).arg(hit.score, 0, 'g', 7)
      .arg(identification->spectrumIndex
             ? tr(" · spectrum #%1").arg(*identification->spectrumIndex + 1)
             : tr(" · unlinked")), 8000);
  }

  void MainWindow::updateSpectrumAnnotation()
  {
    if (!selection_.spectrum() || !annotateSpectrumAction_->isChecked())
    {
      spectrum_->setAnnotation(std::nullopt);
      return;
    }
    const IdentificationRecord* identification = nullptr;
    if (selection_.identification())
    {
      const IdentificationRecord* selected = document_.identification(*selection_.identification());
      if (selected && selected->spectrumIndex && *selected->spectrumIndex == *selection_.spectrum())
        identification = selected;
    }
    if (!identification) identification = document_.identificationForSpectrum(*selection_.spectrum());
    const OpenMS::MSSpectrum* experimental = document_.spectrum(*selection_.spectrum());
    if (!identification || !experimental || identification->hits.empty())
    {
      spectrum_->setAnnotation(std::nullopt);
      return;
    }
    const std::size_t hitIndex = std::min(selection_.hit().value_or(0),
                                          identification->hits.size() - 1);
    spectrum_->setAnnotation(computeSpectrumAnnotation(
      *experimental, identification->hits[hitIndex], annotationTolerance_->value()));
  }

  void MainWindow::navigateSpectrum(int direction, unsigned int msLevel)
  {
    if (imagingStore_)
    {
      const auto current = imaging_->imageWidget()->selectedSpectrum().value_or(0);
      if (direction > 0 && current + 1 < imagingStore_->spectrumCount())
        selectImagingSpectrum(current + 1);
      else if (direction < 0 && current > 0)
        selectImagingSpectrum(current - 1);
      return;
    }
    if (document_.isEmpty()) return;
    if (!selection_.spectrum())
    {
      if (const auto first = edgeFilteredSpectrum(direction < 0, msLevel)) selectSpectrum(*first);
      return;
    }
    if (const auto next = adjacentFilteredSpectrum(*selection_.spectrum(), direction, msLevel))
      selectSpectrum(*next);
  }

  void MainWindow::selectEdgeSpectrum(bool last, unsigned int msLevel)
  {
    if (imagingStore_ && imagingStore_->spectrumCount() > 0)
    {
      selectImagingSpectrum(last ? imagingStore_->spectrumCount() - 1 : 0);
      return;
    }
    if (const auto edge = edgeFilteredSpectrum(last, msLevel)) selectSpectrum(*edge);
  }

  std::optional<std::size_t> MainWindow::adjacentFilteredSpectrum(
    std::size_t current, int direction, unsigned int msLevel) const
  {
    if (direction == 0) return std::nullopt;
    const int channelIndex = selection_.faimsChannel();
    if (channelIndex < 0 || static_cast<std::size_t>(channelIndex) >= document_.faimsChannels().size())
      return document_.adjacentSpectrumIndex(current, direction, msLevel);

    const double activeCv = document_.faimsChannels()[static_cast<std::size_t>(channelIndex)]
                              .compensationVoltage;
    const auto& spectra = document_.spectra();
    std::ptrdiff_t index = static_cast<std::ptrdiff_t>(current)
      + (direction > 0 ? 1 : -1);
    while (index >= 0 && index < static_cast<std::ptrdiff_t>(spectra.size()))
    {
      const SpectrumRecord& record = spectra[static_cast<std::size_t>(index)];
      if ((msLevel == 0 || record.msLevel == msLevel) && record.compensationVoltage
          && std::abs(*record.compensationVoltage - activeCv) <= 1e-6)
        return static_cast<std::size_t>(index);
      index += direction > 0 ? 1 : -1;
    }
    return std::nullopt;
  }

  std::optional<std::size_t> MainWindow::edgeFilteredSpectrum(
    bool last, unsigned int msLevel) const
  {
    const int channelIndex = selection_.faimsChannel();
    if (channelIndex < 0 || static_cast<std::size_t>(channelIndex) >= document_.faimsChannels().size())
      return document_.edgeSpectrumIndex(last, msLevel);

    const double activeCv = document_.faimsChannels()[static_cast<std::size_t>(channelIndex)]
                              .compensationVoltage;
    const auto& spectra = document_.spectra();
    for (std::size_t offset = 0; offset < spectra.size(); ++offset)
    {
      const std::size_t index = last ? spectra.size() - 1 - offset : offset;
      const SpectrumRecord& record = spectra[index];
      if ((msLevel == 0 || record.msLevel == msLevel) && record.compensationVoltage
          && std::abs(*record.compensationVoltage - activeCv) <= 1e-6)
        return index;
    }
    return std::nullopt;
  }

  void MainWindow::updateSpectrumStatus()
  {
    if (!selection_.spectrum()) return;
    const std::size_t selectedSpectrum = *selection_.spectrum();
    const auto* selected = document_.spectrum(selectedSpectrum);
    if (!selected) return;
    selectionContext_->setText(tr("Scan %1/%2 · MS%3 · RT %4 %5 · %6 peaks")
      .arg(selectedSpectrum + 1)
      .arg(document_.statistics().spectrumCount)
      .arg(selected->getMSLevel())
      .arg(RtUnit::format(selected->getRT(), rtInMinutes()), RtUnit::unit(rtInMinutes()))
      .arg(selected->size()));
    if (const SpectrumRecord* record = document_.spectrumRecord(selectedSpectrum))
    {
      QStringList details;
      details << record->nativeId;
      if (record->precursorMz)
        details << tr("Precursor m/z %1, z=%2")
          .arg(*record->precursorMz, 0, 'f', 4).arg(record->precursorCharge);
      if (record->compensationVoltage)
        details << tr("FAIMS CV %1 V").arg(*record->compensationVoltage, 0, 'f', 1);
      selectionContext_->setToolTip(details.join(QLatin1Char('\n')));
    }
    statusBar()->showMessage(tr("Spectrum %1/%2 · MS%3 · RT %4 %5 · %6 peaks")
      .arg(selectedSpectrum + 1)
      .arg(document_.statistics().spectrumCount)
      .arg(selected->getMSLevel())
      .arg(RtUnit::format(selected->getRT(), rtInMinutes()), RtUnit::unit(rtInMinutes()))
      .arg(selected->size()));
  }

  void MainWindow::setDarkTheme(bool dark)
  {
    if (!dark)
    {
      qApp->setPalette(style()->standardPalette());
      // Custom plot canvases sample palette() at paint time; force a repaint so a
      // live toggle re-themes them immediately.
      for (QWidget* widget : findChildren<QWidget*>()) widget->update();
      return;
    }
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(35, 37, 43));
    palette.setColor(QPalette::WindowText, QColor(235, 235, 240));
    palette.setColor(QPalette::Base, QColor(23, 25, 30));
    palette.setColor(QPalette::AlternateBase, QColor(42, 45, 52));
    palette.setColor(QPalette::ToolTipBase, QColor(235, 235, 240));
    palette.setColor(QPalette::ToolTipText, QColor(20, 20, 20));
    palette.setColor(QPalette::Text, QColor(235, 235, 240));
    palette.setColor(QPalette::Button, QColor(45, 48, 56));
    palette.setColor(QPalette::ButtonText, QColor(235, 235, 240));
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Highlight, QColor(45, 140, 220));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor(145, 148, 158));
    palette.setColor(QPalette::Mid, QColor(105, 108, 118));
    qApp->setPalette(palette);
    for (QWidget* widget : findChildren<QWidget*>()) widget->update();
  }

  void MainWindow::closeEvent(QCloseEvent* event)
  {
    if (!confirmFeatureChanges(tr("exit OpenMS Viewer")))
    {
      event->ignore();
      return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("main/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("main/state"), saveState());
    settings.setValue(QStringLiteral("appearance/dark"), darkThemeAction_->isChecked());
    settings.setValue(QStringLiteral("appearance/spectrumGrid"), showSpectrumGridAction_->isChecked());
    settings.setValue(QStringLiteral("peakMap/rasterWidth"), peakMapRasterWidth_->value());
    settings.setValue(QStringLiteral("files/recent"), recentFiles_);
    settings.setValue(QStringLiteral("files/lastPrimary"), lastPrimaryPath_);
    for (auto it = dockVisibilityPreference_.cbegin(); it != dockVisibilityPreference_.cend(); ++it)
      settings.setValue(QStringLiteral("docks/%1/preferredVisible").arg(it.key()), it.value());
    QMainWindow::closeEvent(event);
  }

  namespace
  {
    // A droppable input: the vendor formats (Bruker .d directory, Thermo .raw
    // file) plus any format the registry recognizes in a category the loader can
    // currently route (spectra / features / identifications / imaging).
    bool isSupportedInputPath(const QString& path)
    {
      const QFileInfo info(path);
      const QString suffix = info.suffix();
      if (suffix.compare(QStringLiteral("d"), Qt::CaseInsensitive) == 0) return info.isDir();
      if (suffix.compare(QStringLiteral("raw"), Qt::CaseInsensitive) == 0) return info.isFile();
      using Category = FormatRegistry::Category;
      const auto format = FormatRegistry::detect(path);
      return format.supported
        && (format.category == Category::Experiment || format.category == Category::Features
            || format.category == Category::Identifications || format.category == Category::Imaging
            || format.category == Category::Osw || format.category == Category::Consensus);
    }
  }

  void MainWindow::dragEnterEvent(QDragEnterEvent* event)
  {
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl& url : event->mimeData()->urls())
    {
      if (isSupportedInputPath(url.toLocalFile()))
      {
        event->acceptProposedAction();
        return;
      }
    }
  }

  void MainWindow::dropEvent(QDropEvent* event)
  {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
    {
      const QString path = url.toLocalFile();
      if (isSupportedInputPath(path))
        paths.push_back(path);
    }
    if (!paths.isEmpty())
    {
      loadFiles(paths);
      event->acceptProposedAction();
    }
  }
}
