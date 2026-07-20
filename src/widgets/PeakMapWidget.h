#pragma once

#include "model/ViewerDocument.h"
#include "model/ConsensusDocument.h"
#include "plot/PeakMapRasterizer.h"

#include <OpenMS/KERNEL/MSExperiment.h>

#include <QFutureWatcher>
#include <QImage>
#include <QPoint>
#include <QTimer>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QEnterEvent;
class QKeyEvent;

namespace OpenMSViewer
{
  enum class PeakMapInteractionMode { Zoom, Pan, Measure, Edit };

  // One MS/MS precursor as an overlay marker: the isolation window (m/z low..high)
  // at the fragmentation scan's RT, plus the selected precursor m/z and charge.
  struct PrecursorMarker
  {
    std::size_t spectrumIndex{0};
    double rt{0.0};
    double mz{0.0};
    double lowerMz{0.0};  ///< isolation window low edge (== mz when no window defined)
    double upperMz{0.0};  ///< isolation window high edge
    int charge{0};
    unsigned int msLevel{2};
  };

  class PeakMapWidget final : public QWidget
  {
    Q_OBJECT

  public:
    static constexpr int DefaultRasterWidth = 1024;
    static constexpr int MinimumRasterWidth = 256;
    static constexpr int MaximumRasterWidth = 4096;

    explicit PeakMapWidget(QWidget* parent = nullptr);
    ~PeakMapWidget() override;

    void setExperiment(std::shared_ptr<const OpenMS::MSExperiment> experiment,
                       const PlotRange& bounds);
    void clear();
    void setSpectrumMarker(double rt, int msLevel, std::optional<double> precursorMz);
    void setAxesSwapped(bool swapped);
    void setFeatures(const std::vector<FeatureRecord>& features);
    void setSelectedFeature(std::optional<std::size_t> featureIndex);
    void setIdentifications(const std::vector<IdentificationRecord>& identifications);
    void setSelectedIdentification(std::optional<std::size_t> identificationIndex);
    // Consensus features are a multi-run overlay: an aligned/averaged centroid plus
    // the "alignment envelope" bounding the per-map handle centroids. Drawn distinctly
    // (dashed envelope + diamond) from single-run features to signal the approximation.
    void setConsensusFeatures(const std::vector<ConsensusFeatureRecord>& features);
    void setSelectedConsensus(std::optional<std::size_t> consensusIndex);
    void setPrecursorMarkers(std::vector<PrecursorMarker> markers);

    [[nodiscard]] bool axesSwapped() const noexcept;
    [[nodiscard]] PeakMapColorMap colorMap() const noexcept;
    [[nodiscard]] PeakMapIntensityScale intensityScale() const noexcept;
    [[nodiscard]] int rasterWidth() const noexcept;
    [[nodiscard]] const PlotRange& viewRange() const noexcept;
    [[nodiscard]] bool canZoomBack() const noexcept;
    [[nodiscard]] bool hasExperiment() const noexcept;
    [[nodiscard]] std::shared_ptr<const OpenMS::MSExperiment> experiment() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedFeature() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedIdentification() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedConsensus() const noexcept;
    [[nodiscard]] bool hasConsensusFeatures() const noexcept;
    [[nodiscard]] QPointF mapDataToWidget(double rt, double mz) const;
    [[nodiscard]] const QImage& rasterImage() const noexcept;
    [[nodiscard]] const QImage& minimapImage() const noexcept;
    [[nodiscard]] QRect minimapRect() const;

  public slots:
    void resetView();
    void zoomBack();
    void zoomIn();
    void zoomOut();
    void panLeft();
    void panRight();
    void panUp();
    void panDown();
    void setRtRange(double minimumRt, double maximumRt);
    void setColorMap(int colorMapIndex);
    void setIntensityScale(int intensityScaleIndex);
    void setRasterWidth(int width);
    void setInteractionMode(int modeIndex);
    void setShowMinimap(bool show);
    void setShowFeatureCentroids(bool show);
    void setShowFeatureBounds(bool show);
    void setShowFeatureHulls(bool show);
    void zoomToFeature(std::size_t featureIndex);
    void setShowIdentifications(bool show);
    void setShowIdentificationSequences(bool show);
    void zoomToIdentification(std::size_t identificationIndex);
    void setShowConsensus(bool show);
    void zoomToConsensus(std::size_t consensusIndex);
    void setShowPrecursors(bool show);
    void setRtInMinutes(bool minutes);

  signals:
    void viewRangeChanged(const OpenMSViewer::PlotRange& range);
    void rtActivated(double rt);
    void featureActivated(std::size_t featureIndex);
    void identificationActivated(std::size_t identificationIndex);
    void precursorActivated(std::size_t spectrumIndex);
    // Feature-edit gestures (only emitted in Edit interaction mode).
    void featureCreateRequested(double rt, double mz);
    void featureMoveRequested(std::size_t featureIndex, double rt, double mz);
    void featureEditRequested(std::size_t featureIndex);
    void featureDeleteRequested(std::size_t featureIndex);
    void zoomHistoryChanged(bool canGoBack);
    void interactionModeChanged(int modeIndex);
    void cursorPositionChanged(double rt, double mz, double intensity);
    void cursorLeft();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    enum class DragMode { None, Zoom, Pan, Measure, Edit };

    [[nodiscard]] QRect plotRect() const;
    // The overview minimap only earns its screen space (and the data it covers)
    // when the view is a strict subset of the full data bounds; when fully zoomed
    // out it is redundant, so it is hidden and non-interactive.
    [[nodiscard]] bool isZoomedIn() const;
    void recenterFromMinimap(const QPointF& position);
    [[nodiscard]] QPointF dataAt(const QPointF& position) const;
    [[nodiscard]] QPointF pixelFor(double rt, double mz) const;
    [[nodiscard]] QSize maximumRasterSize() const;
    [[nodiscard]] QSize boundedRenderSize() const;
    void updateCanvasSizeLimits();
    void scheduleRender();
    void startRender();
    void startMinimapRender();
    void showGoToRangeDialog();
    void applyRange(const PlotRange& range, bool remember);
    void rememberCurrentRange();
    void drawAxes(QPainter& painter, const QRect& area) const;
    void drawFeatures(QPainter& painter) const;
    void drawIdentifications(QPainter& painter) const;
    void drawConsensus(QPainter& painter) const;
    void drawPrecursors(QPainter& painter) const;
    void drawLegend(QPainter& painter, const QRect& area) const;
    void updateInteractionCursor();
    [[nodiscard]] std::optional<std::size_t> nearestFeature(const QPointF& position) const;
    [[nodiscard]] std::optional<std::size_t> nearestIdentification(const QPointF& position) const;
    [[nodiscard]] std::optional<std::size_t> nearestPrecursor(const QPointF& position) const;
    [[nodiscard]] double nearestIntensity(double rt, double mz) const;

    std::shared_ptr<const OpenMS::MSExperiment> experiment_;
    PlotRange dataBounds_;
    PlotRange view_;
    QImage raster_;
    PlotRange rasterRange_;          // data footprint the current raster_ was rendered for
    PlotRange pendingRasterRange_;   // range of the render currently in flight
    bool rasterAxesSwapped_{true};   // axis orientation the current raster_ was rendered with
    bool pendingAxesSwapped_{true};  // axis orientation of the render currently in flight
    bool axesSwapped_{true};
    double selectedRt_{0.0};
    bool hasSelectedRt_{false};
    int selectedMsLevel_{1};
    std::optional<double> selectedMarkerMz_;   // precursor m/z for an MS2+ selection
    std::vector<FeatureRecord> features_;
    std::optional<std::size_t> selectedFeature_;
    std::optional<std::size_t> hoveredFeature_;
    bool showFeatureCentroids_{true};
    bool showFeatureBounds_{false};
    bool showFeatureHulls_{false};
    std::vector<IdentificationRecord> identifications_;
    std::optional<std::size_t> selectedIdentification_;
    std::optional<std::size_t> hoveredIdentification_;
    bool showIdentifications_{true};
    bool showIdentificationSequences_{false};
    std::vector<ConsensusFeatureRecord> consensusFeatures_;
    std::optional<std::size_t> selectedConsensus_;
    bool showConsensus_{true};
    std::vector<PrecursorMarker> precursorMarkers_;
    std::optional<std::size_t> hoveredPrecursor_;  // index into precursorMarkers_
    bool showPrecursors_{false};

    QTimer renderTimer_;
    QFutureWatcher<QImage> renderWatcher_;
    QFutureWatcher<QImage> minimapWatcher_;
    std::uint64_t desiredGeneration_{0};
    std::uint64_t activeGeneration_{0};
    std::uint64_t desiredMinimapGeneration_{0};
    std::uint64_t activeMinimapGeneration_{0};

    DragMode dragMode_{DragMode::None};
    bool draggingMinimap_{false};  // left button held down inside the minimap -> live pan
    std::optional<std::size_t> editFeatureIndex_;  // feature grabbed in Edit mode (else create)
    // Empty-space feature creation is deferred by the double-click interval so a
    // double-click (which Qt delivers as press+release+dblclick+release) opens the
    // edit dialog / resets the view instead of leaving a phantom feature.
    QTimer editCreateTimer_;
    QPointF pendingCreateData_;
    QPoint dragStart_;
    QPoint dragCurrent_;
    QPoint dragPrevious_;
    PlotRange dragStartRange_;
    std::vector<PlotRange> history_;
    QImage minimap_;
    PeakMapColorMap colorMap_{PeakMapColorMap::Plasma};
    PeakMapIntensityScale intensityScale_{PeakMapIntensityScale::Equalized};
    int rasterWidth_{DefaultRasterWidth};
    PeakMapInteractionMode interactionMode_{PeakMapInteractionMode::Zoom};
    bool showMinimap_{true};
    bool rtInMinutes_{false};
  };
}

Q_DECLARE_METATYPE(OpenMSViewer::PlotRange)
