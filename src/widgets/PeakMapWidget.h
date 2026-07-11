#pragma once

#include "model/ViewerDocument.h"
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

class QKeyEvent;

namespace OpenMSViewer
{
  enum class PeakMapInteractionMode { Zoom, Pan, Measure };

  class PeakMapWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit PeakMapWidget(QWidget* parent = nullptr);
    ~PeakMapWidget() override;

    void setExperiment(std::shared_ptr<const OpenMS::MSExperiment> experiment,
                       const PlotRange& bounds);
    void clear();
    void setSelectedRt(double rt);
    void setSpectrumMarker(double rt, int msLevel, std::optional<double> precursorMz);
    void setAxesSwapped(bool swapped);
    void setFeatures(const std::vector<FeatureRecord>& features);
    void setSelectedFeature(std::optional<std::size_t> featureIndex);
    void setIdentifications(const std::vector<IdentificationRecord>& identifications);
    void setSelectedIdentification(std::optional<std::size_t> identificationIndex);

    [[nodiscard]] bool axesSwapped() const noexcept;
    [[nodiscard]] const PlotRange& viewRange() const noexcept;
    [[nodiscard]] bool canZoomBack() const noexcept;
    [[nodiscard]] bool hasExperiment() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedFeature() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedIdentification() const noexcept;
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
    void setInteractionMode(int modeIndex);
    void setShowMinimap(bool show);
    void setShowFeatureCentroids(bool show);
    void setShowFeatureBounds(bool show);
    void setShowFeatureHulls(bool show);
    void zoomToFeature(std::size_t featureIndex);
    void setShowIdentifications(bool show);
    void setShowIdentificationSequences(bool show);
    void zoomToIdentification(std::size_t identificationIndex);

  signals:
    void viewRangeChanged(const OpenMSViewer::PlotRange& range);
    void rtActivated(double rt);
    void featureActivated(std::size_t featureIndex);
    void identificationActivated(std::size_t identificationIndex);
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
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    enum class DragMode { None, Zoom, Pan, Measure };

    [[nodiscard]] QRect plotRect() const;
    [[nodiscard]] QPointF dataAt(const QPointF& position) const;
    [[nodiscard]] QPointF pixelFor(double rt, double mz) const;
    [[nodiscard]] QSize densityAwareRenderSize() const;
    void scheduleRender();
    void startRender();
    void startMinimapRender();
    void showGoToRangeDialog();
    void applyRange(const PlotRange& range, bool remember);
    void rememberCurrentRange();
    void drawAxes(QPainter& painter, const QRect& area) const;
    void drawFeatures(QPainter& painter) const;
    void drawIdentifications(QPainter& painter) const;
    void drawLegend(QPainter& painter, const QRect& area) const;
    void updateInteractionCursor();
    [[nodiscard]] std::optional<std::size_t> nearestFeature(const QPointF& position) const;
    [[nodiscard]] std::optional<std::size_t> nearestIdentification(const QPointF& position) const;
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

    QTimer renderTimer_;
    QFutureWatcher<QImage> renderWatcher_;
    QFutureWatcher<QImage> minimapWatcher_;
    std::uint64_t desiredGeneration_{0};
    std::uint64_t activeGeneration_{0};
    std::uint64_t desiredMinimapGeneration_{0};
    std::uint64_t activeMinimapGeneration_{0};

    DragMode dragMode_{DragMode::None};
    QPoint dragStart_;
    QPoint dragCurrent_;
    QPoint dragPrevious_;
    PlotRange dragStartRange_;
    std::vector<PlotRange> history_;
    QImage minimap_;
    PeakMapColorMap colorMap_{PeakMapColorMap::Viridis};
    PeakMapIntensityScale intensityScale_{PeakMapIntensityScale::Equalized};
    PeakMapInteractionMode interactionMode_{PeakMapInteractionMode::Zoom};
    bool showMinimap_{true};
  };
}

Q_DECLARE_METATYPE(OpenMSViewer::PlotRange)
