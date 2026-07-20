#pragma once

#include "plot/PeakMapRasterizer.h"
#include "plot/PlotRange.h"

#include <OpenMS/KERNEL/MSExperiment.h>

#include <QtGlobal>

#include <QFutureWatcher>
#include <QPoint>

#include <cstdint>
#include <memory>
#include <vector>

// The 3-D surface is GPU-rendered through QRhiWidget on Qt 6.7+ (QRhi picks
// Vulkan/Metal/D3D/OpenGL at runtime); on older Qt it falls back to the CPU
// QPainter painter's-algorithm renderer. The public API is identical either way.
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#  define OPENMS_VIEWER_SURFACE3D_RHI 1
#  include <QRhiWidget>
#else
#  define OPENMS_VIEWER_SURFACE3D_RHI 0
#  include <QWidget>
#endif

class QLabel;
class QMouseEvent;
class QWheelEvent;
#if OPENMS_VIEWER_SURFACE3D_RHI
class QRhi;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;
class QRhiCommandBuffer;
#endif

namespace OpenMSViewer
{
  // Height-map grid of a zoomed RT×m/z region, computed off-thread. Values are
  // normalized [0,1] intensities (same pipeline as the 2-D peak map).
  struct SurfaceGrid
  {
    std::vector<float> values;   // rows (m/z) × cols (RT), row-major
    int rows{0};
    int cols{0};
    float maximum{0.0F};
  };

#if OPENMS_VIEWER_SURFACE3D_RHI
  using Surface3DBase = QRhiWidget;
#else
  using Surface3DBase = QWidget;
#endif

  // A 3-D intensity surface of the peak map. Only meaningful when zoomed in
  // (see kMaxRtSpan/kMaxMzSpan).
  class PeakSurface3DWidget final : public Surface3DBase
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
                 PeakMapColorMap colorMap, unsigned int msLevel = 1,
                 PeakMapIntensityScale intensityScale = PeakMapIntensityScale::Equalized);
    void clear();
    void resetOrientation();
    void setRtInMinutes(bool minutes);

  protected:
#if OPENMS_VIEWER_SURFACE3D_RHI
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;
#else
    void paintEvent(QPaintEvent* event) override;
#endif
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

  private:
    void startRender();
    void requestFrame();          // schedule a repaint (update() on both backends)
    void updateOverlay();         // title + status labels
    void layoutOverlay();
    [[nodiscard]] bool hasSurface() const noexcept;

    std::shared_ptr<const OpenMS::MSExperiment> experiment_;
    PlotRange range_;
    PeakMapColorMap colorMap_{PeakMapColorMap::Plasma};
    PeakMapIntensityScale intensityScale_{PeakMapIntensityScale::Equalized};
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
    QLabel* titleLabel_{nullptr};
    QLabel* statusLabel_{nullptr};

#if OPENMS_VIEWER_SURFACE3D_RHI
    void ensurePipeline();
    void rebuildMesh();
    QRhi* rhi_{nullptr};                                 // non-owning, tracks device changes
    std::unique_ptr<QRhiBuffer> vertexBuffer_;
    std::unique_ptr<QRhiBuffer> indexBuffer_;
    std::unique_ptr<QRhiBuffer> uniformBuffer_;
    std::unique_ptr<QRhiShaderResourceBindings> bindings_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    std::vector<float> vertexData_;
    std::vector<quint32> indexData_;
    quint32 indexCount_{0};
    bool meshDirty_{false};
#endif
  };
}
