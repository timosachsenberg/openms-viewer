#pragma once

#include "model/ViewerDocument.h"
#include "export/MzMLExporter.h"
#include "model/ImagingDocument.h"
#include "model/OswDocument.h"
#include "model/ConsensusDocument.h"
#include "model/SelectionController.h"
#include "widgets/RowStackWidget.h"

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
class QDragEnterEvent;
class QDropEvent;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QToolBar;
class QUndoStack;
class QWidget;

namespace OpenMSViewer
{
  class PeakMapWidget;
  class ChromatogramPanelWidget;
  class DataLayersWidget;
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
  enum class ToastLevel;

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
    void openDataFolder();
    void finishLoad();
    void finishFeatureLoad();
    void finishIdentificationLoad();
    void finishImagingLoad();
    void finishOswLoad();
    void finishConsensusLoad();
    void selectSpectrum(std::size_t index);
    void selectNearestSpectrum(double rt);
    void selectFeature(std::size_t index);
    void selectIdentification(std::size_t index, std::size_t hitIndex = 0);
    void setDarkTheme(bool dark);
    // Apply the currently selected theme mode (System/Light/Dark) to the palette.
    void applyThemeMode();
    // True when the OS reports a dark colour scheme (Qt 6.5+); otherwise inferred
    // from the startup palette luminance as a best-effort fallback.
    static bool systemPrefersDark();
    // Persist (save=true) or restore (save=false) per-panel display preferences.
    // A single key list drives both directions so they cannot drift apart.
    void syncDisplayPreferences(bool save);
    void setFaimsChannel(int channelIndex);
    void selectImagingSpectrum(std::size_t index);
    void exportMzML();
    void finishMzMLExport();
    void reloadLastFile();
    void cancelCurrentOperation();
    void resetPanelLayout();
    void show3DSurface();
    void closeSurface3D();

  private:
    // Task-oriented panel arrangements. Overview is the general default
    // (resetPanelLayout); the others declutter to the panels a task needs and
    // stack them sensibly. Only panels with data are actually shown.
    enum class LayoutPreset { Overview, Identification, Imaging, Dia };
    void applyLayoutPreset(LayoutPreset preset);
    // Canonical default arrangement: peak map over TIC over spectrum, each a
    // full-width row. Every other panel starts hidden and is opened on demand —
    // with no tabs, an opinionated default is the only way 15 panels stay usable.
    void applyDefaultLayout();

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
    void updateWindowTitle();
    [[nodiscard]] bool saveFeatures(bool choosePath = false);
    void saveIdentifications();
    void saveConsensus();
    void updateSaveActions();
    void updateDataLayers();
    void updateFeatureEditState();
    void closeData();
    void removeLayer(int layer);
    [[nodiscard]] bool confirmFeatureChanges(const QString& action);
    [[nodiscard]] bool editedFeaturesWouldBeReplacedBy(const QString& path) const;
    [[nodiscard]] QString askSavePath(const QString& title, const QString& sourcePath,
                                      const QString& fallbackStem, const QString& suffix);
    // Consensus peak-map overlay: highlight on selection, and on activation drill
    // from a consensus feature down to the source scan in the loaded raw run.
    void onConsensusFeatureActivated(qint64 index);
    void onConsensusFeatureDrillDown(qint64 index);
    void navigateConsensusToRawSpectrum(std::size_t consensusIndex);
    void updateSpectrumControls();
    void setPeakMapControlsEnabled(bool enabled);
    [[nodiscard]] bool rtInMinutes() const;
    // A panel is shown only when its data exists (available) and the user wants
    // it (preference). Availability is the document's business, the preference
    // is the user's; neither alone decides.
    void setPanelAvailable(PanelHandle* panel, bool available);
    void initializePanelPreferences(bool hasSavedState);
    [[nodiscard]] QMap<QString, bool> defaultPanelPreferences() const;
    [[nodiscard]] bool panelWanted(PanelHandle* panel) const;
    // Show desiredLayout_ minus the panels whose data is absent. Everything that
    // changes availability or preference routes through here.
    void applyEffectiveLayout();
    // Fold a user-made change to the shown layout back into desiredLayout_.
    void captureDesiredLayout();
    void installPlotContextMenu(QWidget* widget, const QString& defaultName,
                                std::function<void()> reset = {});
    void showOperationError(const QString& title, const QString& summary, const QString& details);
    void notify(const QString& message, ToastLevel level);
    // Category-routed loaders shared by loadFile() and the open dialogs/drops.
    void loadFeatureData(const QString& path);
    void loadIdentificationData(const QString& path);
    void loadExperimentData(const QString& path, int fileType);
    void loadChromatogramFile(const QString& path);
    void loadOswData(const QString& path);
    void loadConsensusData(const QString& path);
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
    QScrollArea* peakMapScroll_{nullptr};
    PeakMapWidget* peakMap_{nullptr};
    class PeakSurface3DWidget* surface3D_{nullptr};
    QDialog* surface3DDialog_{nullptr};
    QWidget* spectrumPanel_{nullptr};
    QToolBar* spectrumControlBar_{nullptr};
    LoadingOverlayWidget* loadingOverlay_{nullptr};
    ChromatogramPanelWidget* chromatograms_{nullptr};
    DataLayersWidget* dataLayers_{nullptr};
    FaimsPanelWidget* faims_{nullptr};
    FeatureTableWidget* features_{nullptr};
    IdentificationTableWidget* identifications_{nullptr};
    SpectrumTableWidget* spectra_{nullptr};
    IonMobilityPanelWidget* ionMobility_{nullptr};
    ImagingPanelWidget* imaging_{nullptr};
    class OswPanel* osw_{nullptr};
    class ConsensusPanel* consensus_{nullptr};
    class MetadataBrowserWidget* metadata_{nullptr};
    LogWidget* log_{nullptr};
    TicWidget* tic_{nullptr};
    SpectrumWidget* spectrum_{nullptr};
    class ToastOverlay* toasts_{nullptr};
    RowStackWidget* rowStack_{nullptr};
    PanelHandle* peakMapHandle_{nullptr};
    PanelHandle* ticHandle_{nullptr};
    PanelHandle* spectrumHandle_{nullptr};
    PanelHandle* featuresHandle_{nullptr};
    PanelHandle* identificationsHandle_{nullptr};
    PanelHandle* spectraHandle_{nullptr};
    PanelHandle* chromatogramsHandle_{nullptr};
    PanelHandle* dataLayersHandle_{nullptr};
    PanelHandle* faimsHandle_{nullptr};
    PanelHandle* ionMobilityHandle_{nullptr};
    PanelHandle* imagingHandle_{nullptr};
    PanelHandle* oswHandle_{nullptr};
    PanelHandle* consensusHandle_{nullptr};
    PanelHandle* metadataHandle_{nullptr};
    PanelHandle* logHandle_{nullptr};
    QProgressBar* progress_{nullptr};
    QLabel* runContext_{nullptr};
    QLabel* selectionContext_{nullptr};
    QLabel* cursorContext_{nullptr};
    QLabel* viewContext_{nullptr};

    QAction* openAction_{nullptr};
    QAction* openBrukerAction_{nullptr};
    QAction* reloadAction_{nullptr};
    QAction* closeDataAction_{nullptr};
    QAction* exportMzMLAction_{nullptr};
    QAction* saveFeaturesAction_{nullptr};
    QAction* saveFeaturesAsAction_{nullptr};
    QAction* saveIdentificationsAction_{nullptr};
    QAction* saveConsensusAction_{nullptr};
    QAction* zoomBackAction_{nullptr};
    QAction* resetViewAction_{nullptr};
    QAction* spectrumFirstAction_{nullptr};
    QAction* spectrumPreviousAction_{nullptr};
    QAction* spectrumNextAction_{nullptr};
    QAction* spectrumLastAction_{nullptr};
    QAction* swapAxesAction_{nullptr};
    QAction* surface3DAction_{nullptr};
    QAction* themeSystemAction_{nullptr};
    QAction* themeLightAction_{nullptr};
    QAction* themeDarkAction_{nullptr};
    QAction* showMinimapAction_{nullptr};
    QAction* snapToPeakAction_{nullptr};
    QAction* rtInMinutesAction_{nullptr};
    QAction* relativeIntensityAction_{nullptr};
    QAction* autoYScaleAction_{nullptr};
    QAction* showCentroidsAction_{nullptr};
    QAction* showFeatureBoundsAction_{nullptr};
    QAction* showFeatureHullsAction_{nullptr};
    QAction* showIdentificationsAction_{nullptr};
    QAction* showIdentificationSequencesAction_{nullptr};
    QAction* showConsensusAction_{nullptr};
    QAction* showPrecursorsAction_{nullptr};
    QAction* clearFeatureOverlayAction_{nullptr};
    QAction* clearIdentificationOverlayAction_{nullptr};
    QAction* annotateSpectrumAction_{nullptr};
    QAction* mirrorSpectrumAction_{nullptr};
    QAction* showUnmatchedIonsAction_{nullptr};
    QAction* measureSpectrumAction_{nullptr};
    QAction* labelSpectrumAction_{nullptr};
    QAction* showMzLabelsAction_{nullptr};
    QAction* showSpectrumGridAction_{nullptr};
    QAction* resetSpectrumViewAction_{nullptr};
    QAction* clearSpectrumMeasurementsAction_{nullptr};
    QAction* clearSpectrumLabelsAction_{nullptr};
    QAction* undoFeatureAction_{nullptr};
    QAction* redoFeatureAction_{nullptr};
    QAction* editFeaturesModeAction_{nullptr};
    QMenu* recentFilesMenu_{nullptr};
    QComboBox* spectrumLevel_{nullptr};
    QSpinBox* spectrumIndex_{nullptr};
    QSpinBox* peakMapRasterWidth_{nullptr};
    QLineEdit* spectrumSearch_{nullptr};
    QDoubleSpinBox* annotationTolerance_{nullptr};
    std::shared_ptr<ImagingStore> imagingStore_;
    ImagingSummary imagingSummary_;
    QStringList recentFiles_;
    QString lastPrimaryPath_;
    QString featureSavePath_;
    QUndoStack* featureUndoStack_{nullptr};
    QMap<QString, bool> panelVisibilityPreference_;
    // The arrangement the user built, including panels whose data is not loaded
    // right now. The row stack shows this filtered by availability; persisting
    // the filtered view instead would forget a two-panel row on every launch
    // that starts without its data.
    LayoutModel desiredLayout_;
    bool applyingLayout_{false};
    bool updatingSpectrumIndex_{false};
    bool hasOswData_{false};
    bool hasConsensusData_{false};
    // Retained for the peak-map overlay drill-down (resolve a consensus feature's
    // per-map handle back to a scan in the loaded raw run) and coherent titling.
    std::shared_ptr<OpenMS::ConsensusMap> consensusMap_;
    std::vector<ConsensusColumn> consensusColumns_;
    QString consensusSourcePath_;
    QString oswSourcePath_;

    enum Operation
    {
      NoOperation,
      MzMLOperation,
      ImagingOperation,
      FeatureOperation,
      IdentificationOperation,
      ExportOperation,
      OswOperation,
      ConsensusOperation
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
    QFutureWatcher<OswLoadResult> oswLoadWatcher_;
    QFutureWatcher<ConsensusLoadResult> consensusLoadWatcher_;
  };
}
