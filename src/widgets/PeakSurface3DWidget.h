#pragma once

#include "plot/PeakMapRasterizer.h"
#include "plot/PlotRange.h"

#include <OpenMS/KERNEL/MSExperiment.h>

#include <QFutureWatcher>
#include <QPoint>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <vector>

class QMouseEvent;
class QWheelEvent;

namespace OpenMSViewer
{
  // Height-map grid of a zoomed RT×m/z region, computed off-thread.
  struct SurfaceGrid
  {
    std::vector<float> values;   // rows (m/z) × cols (RT), row-major
    int rows{0};
    int cols{0};
    float maximum{0.0F};
  };

  // A 3-D intensity surface of the peak map, drawn with QPainter (painter's-algorithm
  // quads) so it renders anywhere and keeps the documented upgrade path to
  // QOpenGLWidget. Only meaningful when zoomed in (see kMaxRtSpan/kMaxMzSpan).
  class PeakSurface3DWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit PeakSurface3DWidget(QWidget* parent = nullptr);
    ~PeakSurface3DWidget() override;

    // RT/m-z spans above which the 3-D view is not offered (matches pyopenms-viewer).
    static constexpr double kMaxRtSpan = 120.0;
    static constexpr double kMaxMzSpan = 50.0;
    [[nodiscard]] static bool viewFitsForSurface(const PlotRange& range) noexcept;

    void setView(std::shared_ptr<const OpenMS::MSExperiment> experiment, const PlotRange& range,
                 PeakMapColorMap colorMap, unsigned int msLevel = 1);
    void clear();
    void resetOrientation();
    void setRtInMinutes(bool minutes);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

  private:
    void startRender();

    std::shared_ptr<const OpenMS::MSExperiment> experiment_;
    PlotRange range_;
    PeakMapColorMap colorMap_{PeakMapColorMap::Viridis};
    unsigned int msLevel_{1};
    SurfaceGrid grid_;
    double yaw_{0.6};      // radians, rotation about the vertical axis
    double pitch_{0.9};    // radians, elevation of the camera
    double zoom_{1.0};
    bool dragging_{false};
    bool rtInMinutes_{false};
    QPoint dragLast_;
    QFutureWatcher<SurfaceGrid> watcher_;
    std::uint64_t desiredGeneration_{0};
    std::uint64_t activeGeneration_{0};
  };
}
