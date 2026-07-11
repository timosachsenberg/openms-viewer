#pragma once

#include "model/ViewerDocument.h"
#include "export/MzMLExporter.h"
#include "model/ImagingDocument.h"
#include "model/SelectionController.h"

#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMap>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <cstddef>
#include <functional>

class QAction;
class QCloseEvent;
class QDockWidget;
class QDragEnterEvent;
class QDropEvent;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QSpinBox;
class QStackedWidget;
class QToolBar;
class QWidget;

namespace OpenMSViewer
{
  class PeakMapWidget;
  class ChromatogramPanelWidget;
  class FaimsPanelWidget;
  class FeatureTableWidget;
  class IdentificationTableWidget;
  class SpectrumTableWidget;
  class IonMobilityPanelWidget;
  class ImagingPanelWidget;
  class LogWidget;
  class LoadingOverlayWidget;
  class SpectrumWidget;
  class TicWidget;
  class WelcomeWidget;

  class MainWindow final : public QMainWindow
  {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void loadFile(const QString& path);
    void loadFiles(const QStringList& paths);

  protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

  private slots:
    void openFile();
    void finishLoad();
    void finishFeatureLoad();
    void finishIdentificationLoad();
    void finishImagingLoad();
    void selectSpectrum(std::size_t index);
    void selectNearestSpectrum(double rt);
    void selectFeature(std::size_t index);
    void selectIdentification(std::size_t index, std::size_t hitIndex = 0);
    void setDarkTheme(bool dark);
    void setFaimsChannel(int channelIndex);
    void selectImagingSpectrum(std::size_t index);
    void exportMzML();
    void finishMzMLExport();
    void reloadLastFile();
    void cancelCurrentOperation();
    void resetDockLayout();

  private:
    void createActions();
    void createMenus();
    void createToolBars();
    void createPanels();
    void createStatusContext();
    void rebuildRecentFiles();
    void rememberRecentFile(const QString& path);
    void showWelcomePage();
    void showDataPage();
    void updateRunContext();
    void updateSpectrumControls();
    void setPeakMapControlsEnabled(bool enabled);
    void configureDock(QDockWidget* dock);
    void ensureFloatingDockVisible(QDockWidget* dock);
    void setDockAvailable(QDockWidget* dock, bool available);
    void initializeDockPreferences(bool hasSavedState);
    void installPlotContextMenu(QWidget* widget, const QString& defaultName,
                                std::function<void()> reset = {});
    void showOperationError(const QString& title, const QString& summary, const QString& details);
    void beginOperation(int operation, const QString& title, const QString& detail,
                        bool cancellable = true);
    void endOperation(int operation);
    void showNextPendingOperation();
    [[nodiscard]] unsigned int navigationMsLevel() const;
    void updateDocumentViews();
    void updateFeatureViews();
    void updateIdentificationViews();
    void updateSpectrumAnnotation();
    void updateLoadingUi();
    void selectFeatureAndZoom(std::size_t index);
    void selectSpectrumHit(std::size_t spectrumIndex, int identificationIndex, int hitIndex);
    void applyFaimsChannel(int channelIndex, bool selectNearest);
    // Leaf appliers driven by SelectionController signals: they only refresh
    // panels for the new value; cross-entity policy (linking, zooming, FAIMS
    // correction) stays in the select*() command methods above.
    void applySpectrumSelection(qint64 index);
    void applyFeatureSelection(qint64 index);
    void applyIdentificationSelection(qint64 index, qint64 hitIndex);
    void applyPeakMapViewRange(const OpenMSViewer::PlotRange& range);
    // Force a spectrum refresh when the value is unchanged but derived data
    // (e.g. newly loaded identifications) needs re-linking.
    void reconcileSelection();
    [[nodiscard]] std::optional<std::size_t> adjacentFilteredSpectrum(
      std::size_t current, int direction, unsigned int msLevel) const;
    [[nodiscard]] std::optional<std::size_t> edgeFilteredSpectrum(
      bool last, unsigned int msLevel) const;
    void navigateSpectrum(int direction, unsigned int msLevel = 0);
    void selectEdgeSpectrum(bool last, unsigned int msLevel = 0);
    void updateSpectrumStatus();
    void savePlot(QWidget* widget, const QString& defaultName);

    ViewerDocument document_;
    SelectionController selection_;
    QStackedWidget* centralStack_{nullptr};
    WelcomeWidget* welcome_{nullptr};
    QWidget* peakMapPanel_{nullptr};
    QToolBar* peakMapControlBar_{nullptr};
    PeakMapWidget* peakMap_{nullptr};
    QWidget* spectrumPanel_{nullptr};
    QToolBar* spectrumControlBar_{nullptr};
    LoadingOverlayWidget* loadingOverlay_{nullptr};
    ChromatogramPanelWidget* chromatograms_{nullptr};
    FaimsPanelWidget* faims_{nullptr};
    FeatureTableWidget* features_{nullptr};
    IdentificationTableWidget* identifications_{nullptr};
    SpectrumTableWidget* spectra_{nullptr};
    IonMobilityPanelWidget* ionMobility_{nullptr};
    ImagingPanelWidget* imaging_{nullptr};
    LogWidget* log_{nullptr};
    TicWidget* tic_{nullptr};
    SpectrumWidget* spectrum_{nullptr};
    QDockWidget* ticDock_{nullptr};
    QDockWidget* spectrumDock_{nullptr};
    QDockWidget* featuresDock_{nullptr};
    QDockWidget* identificationsDock_{nullptr};
    QDockWidget* spectraDock_{nullptr};
    QDockWidget* chromatogramsDock_{nullptr};
    QDockWidget* faimsDock_{nullptr};
    QDockWidget* ionMobilityDock_{nullptr};
    QDockWidget* imagingDock_{nullptr};
    QDockWidget* logDock_{nullptr};
    QProgressBar* progress_{nullptr};
    QLabel* runContext_{nullptr};
    QLabel* selectionContext_{nullptr};
    QLabel* cursorContext_{nullptr};
    QLabel* viewContext_{nullptr};

    QAction* openAction_{nullptr};
    QAction* reloadAction_{nullptr};
    QAction* closeDataAction_{nullptr};
    QAction* exportMzMLAction_{nullptr};
    QAction* zoomBackAction_{nullptr};
    QAction* resetViewAction_{nullptr};
    QAction* spectrumFirstAction_{nullptr};
    QAction* spectrumPreviousAction_{nullptr};
    QAction* spectrumNextAction_{nullptr};
    QAction* spectrumLastAction_{nullptr};
    QAction* swapAxesAction_{nullptr};
    QAction* darkThemeAction_{nullptr};
    QAction* showMinimapAction_{nullptr};
    QAction* relativeIntensityAction_{nullptr};
    QAction* autoYScaleAction_{nullptr};
    QAction* showCentroidsAction_{nullptr};
    QAction* showFeatureBoundsAction_{nullptr};
    QAction* showFeatureHullsAction_{nullptr};
    QAction* showIdentificationsAction_{nullptr};
    QAction* showIdentificationSequencesAction_{nullptr};
    QAction* clearFeatureOverlayAction_{nullptr};
    QAction* clearIdentificationOverlayAction_{nullptr};
    QAction* annotateSpectrumAction_{nullptr};
    QAction* mirrorSpectrumAction_{nullptr};
    QAction* showUnmatchedIonsAction_{nullptr};
    QAction* measureSpectrumAction_{nullptr};
    QAction* labelSpectrumAction_{nullptr};
    QAction* showMzLabelsAction_{nullptr};
    QAction* resetSpectrumViewAction_{nullptr};
    QAction* clearSpectrumMeasurementsAction_{nullptr};
    QAction* clearSpectrumLabelsAction_{nullptr};
    QMenu* recentFilesMenu_{nullptr};
    QComboBox* spectrumLevel_{nullptr};
    QSpinBox* spectrumIndex_{nullptr};
    QLineEdit* spectrumSearch_{nullptr};
    QDoubleSpinBox* annotationTolerance_{nullptr};
    std::shared_ptr<ImagingStore> imagingStore_;
    ImagingSummary imagingSummary_;
    QStringList recentFiles_;
    QString lastPrimaryPath_;
    QMap<QString, bool> dockVisibilityPreference_;
    bool updatingSpectrumIndex_{false};

    enum Operation
    {
      NoOperation,
      MzMLOperation,
      ImagingOperation,
      FeatureOperation,
      IdentificationOperation,
      ExportOperation
    };
    Operation overlayOperation_{NoOperation};
    QElapsedTimer operationElapsed_;
    QTimer operationTimer_;
    std::shared_ptr<std::atomic_bool> mzMLCancellation_;
    bool featureCancelled_{false};
    bool identificationCancelled_{false};
    bool imagingCancelled_{false};
    bool exportCancelled_{false};

    QFutureWatcher<ViewerDocument::LoadResult> loadWatcher_;
    QElapsedTimer mzMLLoadTimer_;
    QFutureWatcher<ViewerDocument::FeatureLoadResult> featureLoadWatcher_;
    QFutureWatcher<ViewerDocument::IdentificationLoadResult> identificationLoadWatcher_;
    QFutureWatcher<MzMLExportResult> mzMLExportWatcher_;
    QFutureWatcher<ImagingLoadResult> imagingLoadWatcher_;
  };
}
