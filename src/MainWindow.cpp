#include "MainWindow.h"

#include "export/MzMLExportDialog.h"
#include "export/PlotExporter.h"
#include "logging/ApplicationLog.h"

#include "widgets/PeakMapWidget.h"
#include "widgets/ChromatogramPanelWidget.h"
#include "widgets/FaimsPanelWidget.h"
#include "widgets/FeatureTableWidget.h"
#include "widgets/IdentificationTableWidget.h"
#include "widgets/SpectrumTableWidget.h"
#include "widgets/IonMobilityPanelWidget.h"
#include "widgets/ImagingPanelWidget.h"
#include "widgets/LogWidget.h"
#include "widgets/LoadingOverlayWidget.h"
#include "widgets/SpectrumWidget.h"
#include "widgets/TicWidget.h"
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
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
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
    createActions();
    createMenus();
    createToolBars();
    createStatusContext();
    statusBar()->showMessage(tr("Ready — open or drop mzML, FeatureXML, or idXML files"));

    connect(welcome_, &WelcomeWidget::openRequested, this, &MainWindow::openFile);
    connect(welcome_, &WelcomeWidget::recentFileRequested, this, &MainWindow::loadFile);
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
      QString text = tr("RT %1 s · m/z %2").arg(rt, 0, 'f', 2).arg(mz, 0, 'f', 4);
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

    QSettings settings;
    if (settings.contains(QStringLiteral("main/geometry")))
      restoreGeometry(settings.value(QStringLiteral("main/geometry")).toByteArray());
    const bool hasSavedState = settings.contains(QStringLiteral("main/state"));
    if (hasSavedState)
      restoreState(settings.value(QStringLiteral("main/state")).toByteArray());
    // A floating panel becomes a separate top-level window that, on some
    // platforms (notably WSLg/Wayland), never receives mouse input — leaving it
    // impossible to move, dock, or close. Never start in that state: re-dock any
    // panel the restored layout left floating.
    for (QDockWidget* dock : findChildren<QDockWidget*>())
      if (dock->isFloating()) dock->setFloating(false);
    const bool dark = settings.value(QStringLiteral("appearance/dark"), true).toBool();
    darkThemeAction_->setChecked(dark);
    setDarkTheme(dark);
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
    if (mzMLCancellation_) mzMLCancellation_->store(true);
    if (loadWatcher_.isRunning()) loadWatcher_.waitForFinished();
    if (featureLoadWatcher_.isRunning()) featureLoadWatcher_.waitForFinished();
    if (identificationLoadWatcher_.isRunning()) identificationLoadWatcher_.waitForFinished();
    if (mzMLExportWatcher_.isRunning()) mzMLExportWatcher_.waitForFinished();
    if (imagingLoadWatcher_.isRunning()) imagingLoadWatcher_.waitForFinished();
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
    peakMap_ = new PeakMapWidget(peakMapPanel_);
    peakMapLayout->addWidget(peakMapControlBar_);
    peakMapLayout->addWidget(peakMap_, 1);

    centralStack_->addWidget(welcome_);
    centralStack_->addWidget(peakMapPanel_);
    setCentralWidget(centralStack_);
    loadingOverlay_ = new LoadingOverlayWidget(centralStack_);

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
  }

  void MainWindow::createActions()
  {
    openAction_ = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open…"), this);
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::openFile);

    reloadAction_ = new QAction(style()->standardIcon(QStyle::SP_BrowserReload), tr("Reload"), this);
    reloadAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    reloadAction_->setEnabled(false);
    connect(reloadAction_, &QAction::triggered, this, &MainWindow::reloadLastFile);

    closeDataAction_ = new QAction(tr("Close data"), this);
    closeDataAction_->setShortcut(QKeySequence::Close);
    closeDataAction_->setEnabled(false);
    connect(closeDataAction_, &QAction::triggered, this, [this]
    {
      document_.clear();
      imagingStore_.reset();
      imagingSummary_ = {};
      peakMap_->clear();
      tic_->clear();
      spectrum_->clear();
      spectra_->clear();
      chromatograms_->clear();
      ionMobility_->clear();
      faims_->clear();
      imaging_->clear();
      selection_.clear();
      setWindowTitle(tr("OpenMS Viewer"));
      showWelcomePage();
      updateRunContext();
      updateSpectrumControls();
    });

    exportMzMLAction_ = new QAction(tr("Export filtered mzML…"), this);
    exportMzMLAction_->setEnabled(false);
    connect(exportMzMLAction_, &QAction::triggered, this, &MainWindow::exportMzML);

    auto* quitAction = new QAction(tr("Quit"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

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

    darkThemeAction_ = new QAction(tr("Dark theme"), this);
    darkThemeAction_->setCheckable(true);
    connect(darkThemeAction_, &QAction::toggled, this, &MainWindow::setDarkTheme);

    showMinimapAction_ = new QAction(tr("Peak-map minimap"), this);
    showMinimapAction_->setCheckable(true);
    showMinimapAction_->setChecked(true);
    connect(showMinimapAction_, &QAction::toggled,
            peakMap_, &PeakMapWidget::setShowMinimap);

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

    clearFeatureOverlayAction_ = new QAction(tr("Clear feature overlay"), this);
    connect(clearFeatureOverlayAction_, &QAction::triggered, this, [this]
    {
      document_.clearFeatures();
      statusBar()->showMessage(tr("Feature overlay cleared"), 3000);
    });
    clearIdentificationOverlayAction_ = new QAction(tr("Clear identification overlay"), this);
    connect(clearIdentificationOverlayAction_, &QAction::triggered, this, [this]
    {
      document_.clearIdentifications();
      statusBar()->showMessage(tr("Identification overlay cleared"), 3000);
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
    showMzLabelsAction_->setCheckable(true);
    connect(showMzLabelsAction_, &QAction::toggled,
            spectrum_, &SpectrumWidget::setShowMzLabels);

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

    addAction(quitAction);
    addAction(fullscreenAction);
  }

  void MainWindow::createMenus()
  {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openAction_);
    fileMenu->addAction(reloadAction_);
    recentFilesMenu_ = fileMenu->addMenu(tr("Open recent"));
    fileMenu->addAction(closeDataAction_);
    fileMenu->addSeparator();
    auto* exportMenu = fileMenu->addMenu(tr("Export"));
    exportMenu->addAction(exportMzMLAction_);
    auto* plotMenu = exportMenu->addMenu(tr("Plot as PNG"));
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

    // View holds only global chrome; peak-map and spectrum options now live in
    // the peak-map / spectrum panel control bars.
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(darkThemeAction_);
    viewMenu->addAction(tr("Reset panel layout"), this, &MainWindow::resetDockLayout);
    viewMenu->addSeparator();
    viewMenu->addAction(ticDock_->toggleViewAction());
    viewMenu->addAction(spectrumDock_->toggleViewAction());
    viewMenu->addAction(featuresDock_->toggleViewAction());
    viewMenu->addAction(identificationsDock_->toggleViewAction());
    viewMenu->addAction(spectraDock_->toggleViewAction());
    viewMenu->addAction(chromatogramsDock_->toggleViewAction());
    viewMenu->addAction(ionMobilityDock_->toggleViewAction());
    viewMenu->addAction(faimsDock_->toggleViewAction());
    viewMenu->addAction(imagingDock_->toggleViewAction());
    viewMenu->addAction(logDock_->toggleViewAction());

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
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
           "  Ctrl+O: open   Ctrl+R: reload   Ctrl+W: close data   F11: fullscreen"));
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
    interactionMode->addItems({tr("Zoom (Z)"), tr("Pan (P)"), tr("Measure (M)")});
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
    colorMap->addItems({tr("Viridis"), tr("Plasma"), tr("Magma"), tr("Grayscale")});
    colorMap->setMaximumWidth(110);
    colorMap->setAccessibleName(tr("Peak-map color map"));
    peakMapControlBar_->addWidget(colorMap);
    connect(colorMap, qOverload<int>(&QComboBox::currentIndexChanged),
            peakMap_, &PeakMapWidget::setColorMap);

    auto* scale = new QComboBox(peakMapControlBar_);
    scale->setObjectName(QStringLiteral("peakMapIntensityScale"));
    scale->addItems({tr("Log"), tr("Square root"), tr("Linear")});
    scale->setToolTip(tr("Peak-map intensity normalization"));
    scale->setAccessibleName(tr("Peak-map intensity scale"));
    peakMapControlBar_->addWidget(scale);
    connect(scale, qOverload<int>(&QComboBox::currentIndexChanged),
            peakMap_, &PeakMapWidget::setIntensityScale);

    auto* display = new QToolButton(peakMapControlBar_);
    display->setObjectName(QStringLiteral("peakMapDisplayOptions"));
    display->setText(tr("Display"));
    display->setPopupMode(QToolButton::InstantPopup);
    display->setAccessibleName(tr("Peak-map display options"));
    auto* displayMenu = new QMenu(display);
    displayMenu->addAction(swapAxesAction_);
    displayMenu->addAction(showMinimapAction_);
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

    spectrumControlBar_->addWidget(new QLabel(tr("Level"), spectrumControlBar_));
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
    spectrumSearch_->setPlaceholderText(tr("Native ID or RT…"));
    spectrumSearch_->setClearButtonEnabled(true);
    spectrumSearch_->setMaximumWidth(175);
    spectrumSearch_->setAccessibleName(tr("Find spectrum by native ID or retention time"));
    spectrumControlBar_->addWidget(spectrumSearch_);
    connect(spectrumSearch_, &QLineEdit::returnPressed, this, [this]
    {
      const QString query = spectrumSearch_->text().trimmed();
      if (query.isEmpty() || document_.isEmpty()) return;
      bool numeric = false;
      const double rt = query.toDouble(&numeric);
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
        menu.addAction(tr("Export plot as PNG…"), this,
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

  void MainWindow::rebuildRecentFiles()
  {
    QStringList valid;
    for (const QString& path : std::as_const(recentFiles_))
      if (QFileInfo(path).isFile() && !valid.contains(QFileInfo(path).absoluteFilePath()))
        valid.push_back(QFileInfo(path).absoluteFilePath());
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
    setDockAvailable(ticDock_, false);
    setDockAvailable(spectrumDock_, false);
    setDockAvailable(featuresDock_, false);
    setDockAvailable(identificationsDock_, false);
    setDockAvailable(spectraDock_, false);
    setDockAvailable(chromatogramsDock_, false);
    setDockAvailable(ionMobilityDock_, false);
    setDockAvailable(faimsDock_, false);
    setDockAvailable(imagingDock_, false);
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
      {ionMobilityDock_->objectName(), true}, {faimsDock_->objectName(), true},
      {imagingDock_->objectName(), true}, {logDock_->objectName(), false}
    };
    QSettings settings;
    for (QDockWidget* dock : {ticDock_, spectrumDock_, featuresDock_, identificationsDock_,
                              spectraDock_, chromatogramsDock_, ionMobilityDock_, faimsDock_,
                              imagingDock_, logDock_})
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

  void MainWindow::resetDockLayout()
  {
    dockVisibilityPreference_[ticDock_->objectName()] = true;
    dockVisibilityPreference_[spectrumDock_->objectName()] = true;
    dockVisibilityPreference_[featuresDock_->objectName()] = true;
    dockVisibilityPreference_[identificationsDock_->objectName()] = true;
    dockVisibilityPreference_[spectraDock_->objectName()] = false;
    dockVisibilityPreference_[chromatogramsDock_->objectName()] = true;
    dockVisibilityPreference_[ionMobilityDock_->objectName()] = true;
    dockVisibilityPreference_[faimsDock_->objectName()] = true;
    dockVisibilityPreference_[imagingDock_->objectName()] = true;
    dockVisibilityPreference_[logDock_->objectName()] = false;

    for (QDockWidget* dock : {ticDock_, spectrumDock_, featuresDock_, identificationsDock_,
                              spectraDock_, chromatogramsDock_, ionMobilityDock_, faimsDock_,
                              imagingDock_, logDock_})
      dock->setFloating(false);

    addDockWidget(Qt::BottomDockWidgetArea, ticDock_);
    addDockWidget(Qt::BottomDockWidgetArea, spectrumDock_);
    splitDockWidget(ticDock_, spectrumDock_, Qt::Horizontal);
    addDockWidget(Qt::BottomDockWidgetArea, chromatogramsDock_);
    addDockWidget(Qt::BottomDockWidgetArea, ionMobilityDock_);
    addDockWidget(Qt::BottomDockWidgetArea, imagingDock_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
    tabifyDockWidget(ticDock_, chromatogramsDock_);
    tabifyDockWidget(chromatogramsDock_, ionMobilityDock_);
    tabifyDockWidget(ionMobilityDock_, imagingDock_);
    tabifyDockWidget(imagingDock_, logDock_);

    addDockWidget(Qt::RightDockWidgetArea, featuresDock_);
    addDockWidget(Qt::RightDockWidgetArea, identificationsDock_);
    addDockWidget(Qt::RightDockWidgetArea, spectraDock_);
    addDockWidget(Qt::RightDockWidgetArea, faimsDock_);
    tabifyDockWidget(featuresDock_, identificationsDock_);
    tabifyDockWidget(featuresDock_, spectraDock_);
    tabifyDockWidget(spectraDock_, faimsDock_);

    if (document_.isEmpty() && !imagingStore_) showWelcomePage();
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
    }
    setDockAvailable(logDock_, true);
    statusBar()->showMessage(tr("Panel layout reset"), 3000);
  }

  void MainWindow::updateRunContext()
  {
    reloadAction_->setEnabled(!lastPrimaryPath_.isEmpty() && QFileInfo::exists(lastPrimaryPath_));
    if (imagingStore_)
    {
      runContext_->setText(tr("%1 · %2 pixels")
        .arg(QFileInfo(imagingSummary_.sourcePath).fileName()).arg(imagingSummary_.pixels.size()));
      runContext_->setToolTip(imagingSummary_.sourcePath);
      return;
    }
    if (document_.isEmpty())
    {
      runContext_->setText(tr("No data"));
      runContext_->setToolTip({});
      selectionContext_->setText(tr("Spectrum —"));
      viewContext_->setText(tr("View —"));
      return;
    }
    QStringList overlays;
    if (document_.hasFeatures()) overlays << tr("Features");
    if (document_.hasIdentifications()) overlays << tr("IDs");
    QString text = tr("%1 · %2 spectra · %3 peaks")
      .arg(QFileInfo(document_.sourcePath()).fileName())
      .arg(document_.statistics().spectrumCount).arg(document_.statistics().peakCount);
    if (!overlays.isEmpty()) text += tr(" · %1").arg(overlays.join(QStringLiteral(" + ")));
    runContext_->setText(text);
    runContext_->setToolTip(document_.sourcePath());
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
                            showUnmatchedIonsAction_, measureSpectrumAction_, showMzLabelsAction_,
                            resetSpectrumViewAction_, clearSpectrumMeasurementsAction_})
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
      tr("OpenMS data (*.mzML *.mzml *.imzML *.imzml *.featureXML *.featurexml *.idXML *.idxml);;mzML files (*.mzML *.mzml);;imzML imaging files (*.imzML *.imzml);;FeatureXML files (*.featureXML *.featurexml);;idXML files (*.idXML *.idxml);;All files (*)"));
    if (paths.isEmpty()) return;
    settings.setValue(QStringLiteral("files/lastDirectory"), QFileInfo(paths.front()).absolutePath());
    loadFiles(paths);
  }

  void MainWindow::loadFile(const QString& path)
  {
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
    if (suffix.compare(QStringLiteral("featureXML"), Qt::CaseInsensitive) == 0)
    {
      if (featureLoadWatcher_.isRunning())
      {
        statusBar()->showMessage(tr("A FeatureXML file is already loading"), 3000);
        return;
      }
      statusBar()->showMessage(tr("Loading feature overlay %1…").arg(QFileInfo(path).fileName()));
      featureCancelled_ = false;
      beginOperation(FeatureOperation, tr("Loading feature overlay"),
                     tr("Reading %1").arg(QFileInfo(path).fileName()));
      featureLoadWatcher_.setFuture(QtConcurrent::run([path] { return ViewerDocument::readFeatureXML(path); }));
      updateLoadingUi();
      return;
    }
    if (suffix.compare(QStringLiteral("idXML"), Qt::CaseInsensitive) == 0)
    {
      if (identificationLoadWatcher_.isRunning())
      {
        statusBar()->showMessage(tr("An idXML file is already loading"), 3000);
        return;
      }
      statusBar()->showMessage(tr("Loading identification overlay %1…").arg(QFileInfo(path).fileName()));
      identificationCancelled_ = false;
      beginOperation(IdentificationOperation, tr("Loading identification overlay"),
                     tr("Reading %1").arg(QFileInfo(path).fileName()));
      identificationLoadWatcher_.setFuture(QtConcurrent::run([path] { return ViewerDocument::readIdXML(path); }));
      updateLoadingUi();
      return;
    }
    QMessageBox::warning(this, tr("Unsupported file"),
                         tr("OpenMS Viewer does not yet support '%1'.").arg(QFileInfo(path).fileName()));
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
      else if (suffix.compare(QStringLiteral("imzML"), Qt::CaseInsensitive) == 0)
      {
        if (startedMzML) continue;
        startedMzML = true;
      }
      else if (suffix.compare(QStringLiteral("featureXML"), Qt::CaseInsensitive) == 0)
      {
        if (startedFeatures) continue;
        startedFeatures = true;
      }
      else if (suffix.compare(QStringLiteral("idXML"), Qt::CaseInsensitive) == 0)
      {
        if (startedIdentifications) continue;
        startedIdentifications = true;
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
      return;
    }
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("Load failed"), 5000);
      showOperationError(tr("Could not open file"), tr("The mzML file could not be loaded."), result.error);
      return;
    }
    const QString sourcePath = result.sourcePath;
    selection_.clear();
    document_.adopt(std::move(result));
    imagingStore_.reset();
    imagingSummary_ = {};
    imaging_->clear();
    setDockAvailable(imagingDock_, false);
    lastPrimaryPath_ = sourcePath;
    rememberRecentFile(sourcePath);
    showDataPage();
    updateDocumentViews();
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
      return;
    }
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("FeatureXML load failed"), 5000);
      showOperationError(tr("Could not open feature overlay"),
                         tr("The FeatureXML overlay could not be loaded."), result.error);
      return;
    }
    const std::size_t count = result.features.size();
    const QString filename = QFileInfo(result.sourcePath).fileName();
    const QString sourcePath = result.sourcePath;
    document_.adoptFeatures(std::move(result));
    rememberRecentFile(sourcePath);
    updateRunContext();
    qInfo().noquote() << QStringLiteral("Loaded FeatureXML: %1 features").arg(count);
    statusBar()->showMessage(tr("Loaded %1 features from %2").arg(count).arg(filename), 8000);
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
      return;
    }
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("idXML load failed"), 5000);
      showOperationError(tr("Could not open identification overlay"),
                         tr("The idXML overlay could not be loaded."), result.error);
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
      return;
    }
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("imzML load failed"), 5000);
      showOperationError(tr("Could not open imaging dataset"),
                         tr("The imzML imaging dataset could not be loaded."), result.error);
      return;
    }

    document_.clear();
    peakMap_->clear();
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
    setWindowTitle(tr("%1 — OpenMS Viewer").arg(QFileInfo(imagingSummary_.sourcePath).fileName()));
    updateRunContext();
    updateSpectrumControls();
    statusBar()->showMessage(tr("Loaded %1 imaging pixels (%2 × %3), %4 peaks")
      .arg(imagingSummary_.pixels.size()).arg(imagingSummary_.width).arg(imagingSummary_.height)
      .arg(imagingSummary_.peakCount), 8000);
    qInfo().noquote() << QStringLiteral(
      "Loaded imzML: %1 pixels, %2x%3 grid, %4 peaks, m/z %5-%6")
      .arg(imagingSummary_.pixels.size()).arg(imagingSummary_.width).arg(imagingSummary_.height)
      .arg(imagingSummary_.peakCount).arg(imagingSummary_.mzMin, 0, 'f', 4)
      .arg(imagingSummary_.mzMax, 0, 'f', 4);
  }

  void MainWindow::updateDocumentViews()
  {
    const auto experiment = document_.experimentHandle();
    peakMap_->setExperiment(experiment, document_.bounds());
    setPeakMapControlsEnabled(experiment != nullptr);
    tic_->setTrace(document_.tic(), document_.ticLabel());
    tic_->setPeakMapRange(document_.bounds());
    spectrum_->setExperiment(experiment);
    spectra_->setData(document_.spectra(), document_.identifications());
    const bool hasSpectra = !document_.spectra().empty();
    setDockAvailable(ticDock_, hasSpectra);
    setDockAvailable(spectrumDock_, hasSpectra);
    setDockAvailable(spectraDock_, hasSpectra);
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
    setWindowTitle(tr("%1 — OpenMS Viewer").arg(QFileInfo(document_.sourcePath()).fileName()));
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
    setDockAvailable(identificationsDock_, available);
    updateRunContext();
    reconcileSelection();  // re-link the current spectrum to newly loaded IDs
  }

  void MainWindow::updateLoadingUi()
  {
    const bool busy = loadWatcher_.isRunning() || featureLoadWatcher_.isRunning()
      || identificationLoadWatcher_.isRunning() || mzMLExportWatcher_.isRunning()
      || imagingLoadWatcher_.isRunning();
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
                            document_.spectra(), activeCv, this);
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

  void MainWindow::finishMzMLExport()
  {
    const MzMLExportResult result = mzMLExportWatcher_.result();
    endOperation(ExportOperation);
    updateLoadingUi();
    if (!result.succeeded())
    {
      statusBar()->showMessage(tr("mzML export failed"), 5000);
      showOperationError(tr("Could not export mzML"), tr("The filtered mzML export failed."), result.error);
      return;
    }
    statusBar()->showMessage(tr("Exported %1 spectra and %2 peaks to %3")
      .arg(result.spectrumCount).arg(result.peakCount).arg(QFileInfo(result.outputPath).fileName()), 8000);
    qInfo().noquote() << QStringLiteral("Exported filtered mzML: %1 spectra, %2 peaks, %3")
      .arg(result.spectrumCount).arg(result.peakCount).arg(result.outputPath);
  }

  void MainWindow::savePlot(QWidget* widget, const QString& defaultName)
  {
    if (!widget) return;
    const QString directory = document_.sourcePath().isEmpty()
      ? QString() : QFileInfo(document_.sourcePath()).absolutePath();
    const QString suggested = directory.isEmpty() ? defaultName : QDir(directory).filePath(defaultName);
    const QString path = QFileDialog::getSaveFileName(
      this, tr("Export plot as PNG"), suggested, tr("PNG image (*.png);;All files (*)"));
    if (path.isEmpty()) return;
    const QString error = PlotExporter::writePng(*widget, path);
    if (!error.isEmpty())
    {
      showOperationError(tr("Could not export plot"), tr("The PNG plot could not be written."), error);
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
    tic_->setSelectedSpectrum(spectrumIndex);
    tic_->setSelectedRt(selected->getRT());
    peakMap_->setSelectedRt(selected->getRT());
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
    viewContext_->setText(tr("View RT %1–%2 s · m/z %3–%4")
      .arg(range.rtMin, 0, 'f', 1).arg(range.rtMax, 0, 'f', 1)
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
    statusBar()->showMessage(tr("Feature %1 · RT %2 s · m/z %3 · intensity %4 · charge %5")
      .arg(featureIndex)
      .arg(feature->rt, 0, 'f', 2)
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
    selectionContext_->setText(tr("Scan %1/%2 · MS%3 · RT %4 s · %5 peaks")
      .arg(selectedSpectrum + 1)
      .arg(document_.statistics().spectrumCount)
      .arg(selected->getMSLevel())
      .arg(selected->getRT(), 0, 'f', 2)
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
    statusBar()->showMessage(tr("Spectrum %1/%2 · MS%3 · RT %4 s · %5 peaks")
      .arg(selectedSpectrum + 1)
      .arg(document_.statistics().spectrumCount)
      .arg(selected->getMSLevel())
      .arg(selected->getRT(), 0, 'f', 2)
      .arg(selected->size()));
  }

  void MainWindow::setDarkTheme(bool dark)
  {
    if (!dark)
    {
      qApp->setPalette(style()->standardPalette());
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
  }

  void MainWindow::closeEvent(QCloseEvent* event)
  {
    QSettings settings;
    settings.setValue(QStringLiteral("main/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("main/state"), saveState());
    settings.setValue(QStringLiteral("appearance/dark"), darkThemeAction_->isChecked());
    settings.setValue(QStringLiteral("files/recent"), recentFiles_);
    settings.setValue(QStringLiteral("files/lastPrimary"), lastPrimaryPath_);
    for (auto it = dockVisibilityPreference_.cbegin(); it != dockVisibilityPreference_.cend(); ++it)
      settings.setValue(QStringLiteral("docks/%1/preferredVisible").arg(it.key()), it.value());
    QMainWindow::closeEvent(event);
  }

  void MainWindow::dragEnterEvent(QDragEnterEvent* event)
  {
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl& url : event->mimeData()->urls())
    {
      const QString suffix = QFileInfo(url.toLocalFile()).suffix();
      if (suffix.compare(QStringLiteral("mzML"), Qt::CaseInsensitive) == 0
          || suffix.compare(QStringLiteral("imzML"), Qt::CaseInsensitive) == 0
          || suffix.compare(QStringLiteral("featureXML"), Qt::CaseInsensitive) == 0
          || suffix.compare(QStringLiteral("idXML"), Qt::CaseInsensitive) == 0)
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
      const QString suffix = QFileInfo(path).suffix();
      if (suffix.compare(QStringLiteral("mzML"), Qt::CaseInsensitive) == 0
          || suffix.compare(QStringLiteral("imzML"), Qt::CaseInsensitive) == 0
          || suffix.compare(QStringLiteral("featureXML"), Qt::CaseInsensitive) == 0
          || suffix.compare(QStringLiteral("idXML"), Qt::CaseInsensitive) == 0)
        paths.push_back(path);
    }
    if (!paths.isEmpty())
    {
      loadFiles(paths);
      event->acceptProposedAction();
    }
  }
}
