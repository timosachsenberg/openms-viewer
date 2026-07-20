#include "widgets/PeakSurface3DWidget.h"

#include "model/RtUnit.h"

#include "plot/RasterShading.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#if OPENMS_VIEWER_SURFACE3D_RHI
#  include <QFile>
#  include <QMatrix4x4>
#  include <QVector3D>
#  include <rhi/qrhi.h>
#  include <cstring>
#else
#  include <QPainter>
#  include <QPainterPath>
#  include <QPolygonF>
#  include <array>
#endif

#include <algorithm>
#include <cmath>

namespace OpenMSViewer
{
  namespace
  {
    constexpr int kCols = 80;      // RT samples
    constexpr int kRows = 60;      // m/z samples
    constexpr double kHeight = 0.85;

    SurfaceGrid computeGrid(const std::shared_ptr<const OpenMS::MSExperiment>& experiment,
                            const PlotRange& range, unsigned int msLevel,
                            PeakMapIntensityScale intensityScale)
    {
      SurfaceGrid grid;
      if (!experiment || !range.isValid()) return grid;
      grid.rows = kRows;
      grid.cols = kCols;
      // Reuse the peak-map pipeline: fast raster when zoomed out, exact per-peak
      // points when zoomed in, then the same dynspread + intensity scaling. Values
      // come back normalized to [0,1] in layout values[mzIndex * rtBins + rtIndex].
      grid.values = PeakMapRasterizer::heightGrid(*experiment, range,
                                                  static_cast<std::size_t>(kCols),
                                                  static_cast<std::size_t>(kRows),
                                                  msLevel, intensityScale);
      for (const float value : grid.values) grid.maximum = std::max(grid.maximum, value);
      return grid;
    }
  }

  PeakSurface3DWidget::PeakSurface3DWidget(QWidget* parent) : Surface3DBase(parent)
  {
    setMinimumSize(360, 300);
    setMouseTracking(true);
    setAccessibleName(tr("3-D peak surface"));
    setAccessibleDescription(tr("Intensity surface of the zoomed region. Drag to rotate, wheel to zoom."));
#if OPENMS_VIEWER_SURFACE3D_RHI
    setSampleCount(4);   // 4x MSAA so the surface edges stay smooth
#endif

    // Overlay text (title + status) as child labels: works identically over the
    // GPU surface and the CPU fallback, so neither renderer draws text itself.
    titleLabel_ = new QLabel(this);
    titleLabel_->setStyleSheet(QStringLiteral(
      "color: rgb(222,224,232); background: transparent;"));
    titleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    statusLabel_ = new QLabel(this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet(QStringLiteral(
      "color: rgb(150,152,166); background: transparent;"));
    statusLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);

    connect(&watcher_, &QFutureWatcher<SurfaceGrid>::finished, this, [this]
    {
      if (activeGeneration_ != desiredGeneration_) { startRender(); return; }
      grid_ = watcher_.result();
#if OPENMS_VIEWER_SURFACE3D_RHI
      meshDirty_ = true;
#endif
      updateOverlay();
      requestFrame();
    });
    updateOverlay();
  }

  PeakSurface3DWidget::~PeakSurface3DWidget()
  {
    if (watcher_.isRunning()) watcher_.waitForFinished();
  }

  bool PeakSurface3DWidget::viewFitsForSurface(const PlotRange& range) noexcept
  {
    return range.isValid() && range.rtSpan() <= kMaxRtSpan && range.mzSpan() <= kMaxMzSpan;
  }

  bool PeakSurface3DWidget::hasSurface() const noexcept
  {
    return experiment_ && viewFitsForSurface(range_)
        && grid_.rows >= 2 && grid_.cols >= 2 && grid_.maximum > 0.0F;
  }

  void PeakSurface3DWidget::setView(std::shared_ptr<const OpenMS::MSExperiment> experiment,
                                    const PlotRange& range, PeakMapColorMap colorMap,
                                    unsigned int msLevel, PeakMapIntensityScale intensityScale)
  {
    experiment_ = std::move(experiment);
    range_ = range;
    colorMap_ = colorMap;
    intensityScale_ = intensityScale;
    msLevel_ = msLevel;
    grid_ = {};
#if OPENMS_VIEWER_SURFACE3D_RHI
    meshDirty_ = true;
#endif
    if (!experiment_ || !viewFitsForSurface(range_))
    {
      ++desiredGeneration_;   // cancel any pending render
      updateOverlay();
      requestFrame();
      return;
    }
    startRender();
  }

  void PeakSurface3DWidget::clear()
  {
    experiment_.reset();
    grid_ = {};
    ++desiredGeneration_;
#if OPENMS_VIEWER_SURFACE3D_RHI
    meshDirty_ = true;
#endif
    updateOverlay();
    requestFrame();
  }

  void PeakSurface3DWidget::setRtInMinutes(bool minutes)
  {
    if (rtInMinutes_ == minutes) return;
    rtInMinutes_ = minutes;
    updateOverlay();
  }

  void PeakSurface3DWidget::resetOrientation()
  {
    yaw_ = 0.6;
    pitch_ = 0.9;
    zoom_ = 1.0;
    requestFrame();
  }

  void PeakSurface3DWidget::startRender()
  {
    ++desiredGeneration_;
    // Don't (re)launch a rasterization for a view that isn't offered in 3-D.
    if (!experiment_ || !viewFitsForSurface(range_) || watcher_.isRunning())
    {
      updateOverlay();
      requestFrame();
      return;
    }
    activeGeneration_ = desiredGeneration_;
    const auto experiment = experiment_;
    const PlotRange range = range_;
    const unsigned int msLevel = msLevel_;
    const PeakMapIntensityScale intensityScale = intensityScale_;
    watcher_.setFuture(QtConcurrent::run([experiment, range, msLevel, intensityScale]
    {
      return computeGrid(experiment, range, msLevel, intensityScale);
    }));
    updateOverlay();
    requestFrame();
  }

  void PeakSurface3DWidget::requestFrame()
  {
    update();
  }

  void PeakSurface3DWidget::updateOverlay()
  {
    if (!titleLabel_ || !statusLabel_) return;
    const bool fits = experiment_ && viewFitsForSurface(range_);
    if (!fits)
    {
      titleLabel_->hide();
      statusLabel_->setText(
        tr("Zoom the peak map in (RT ≤ %1 s, m/z ≤ %2) to view the 3-D surface")
          .arg(kMaxRtSpan, 0, 'f', 0).arg(kMaxMzSpan, 0, 'f', 0));
      statusLabel_->show();
    }
    else
    {
      titleLabel_->setText(
        tr("3-D surface · RT %1–%2 %3 · m/z %4–%5")
          .arg(RtUnit::format(range_.rtMin, rtInMinutes_, 1),
               RtUnit::format(range_.rtMax, rtInMinutes_, 1), RtUnit::unit(rtInMinutes_))
          .arg(range_.mzMin, 0, 'f', 2).arg(range_.mzMax, 0, 'f', 2));
      titleLabel_->show();
      if (hasSurface())
      {
        statusLabel_->hide();
      }
      else
      {
        statusLabel_->setText(watcher_.isRunning() ? tr("Building surface…")
                                                   : tr("No signal in this region"));
        statusLabel_->show();
      }
    }
    layoutOverlay();
  }

  void PeakSurface3DWidget::layoutOverlay()
  {
    if (titleLabel_)
    {
      titleLabel_->adjustSize();
      titleLabel_->move(10, 6);
    }
    if (statusLabel_)
      statusLabel_->setGeometry(20, height() / 2 - 20, std::max(40, width() - 40), 40);
  }

  void PeakSurface3DWidget::resizeEvent(QResizeEvent* event)
  {
    Surface3DBase::resizeEvent(event);
    layoutOverlay();
  }

  void PeakSurface3DWidget::mousePressEvent(QMouseEvent* event)
  {
    if (event->button() == Qt::LeftButton)
    {
      dragging_ = true;
      dragLast_ = event->pos();
      event->accept();
    }
  }

  void PeakSurface3DWidget::mouseMoveEvent(QMouseEvent* event)
  {
    if (!dragging_ || !(event->buttons() & Qt::LeftButton)) { dragging_ = false; return; }
    const QPoint delta = event->pos() - dragLast_;
    dragLast_ = event->pos();
    yaw_ += delta.x() * 0.01;
    pitch_ = std::clamp(pitch_ - delta.y() * 0.01, 0.15, 1.55);   // keep above the plane
    requestFrame();
  }

  void PeakSurface3DWidget::wheelEvent(QWheelEvent* event)
  {
    const int delta = event->angleDelta().y();
    if (delta == 0) return;   // horizontal / trackpad-phase events aren't zoom
    zoom_ = std::clamp(zoom_ * (delta > 0 ? 1.1 : 1.0 / 1.1), 0.3, 4.0);
    requestFrame();
    event->accept();
  }

#if OPENMS_VIEWER_SURFACE3D_RHI
  namespace
  {
    QShader loadShader(const QString& path)
    {
      QFile file(path);
      if (file.open(QIODevice::ReadOnly)) return QShader::fromSerialized(file.readAll());
      return {};
    }
  }

  void PeakSurface3DWidget::initialize(QRhiCommandBuffer*)
  {
    if (rhi_ != rhi())
    {
      // Device (re)created: everything GPU-side is invalid.
      rhi_ = rhi();
      pipeline_.reset();
      bindings_.reset();
      uniformBuffer_.reset();
      vertexBuffer_.reset();
      indexBuffer_.reset();
      meshDirty_ = true;
    }
    ensurePipeline();
  }

  void PeakSurface3DWidget::ensurePipeline()
  {
    if (!rhi_) return;
    if (!uniformBuffer_)
    {
      // std140: mat4 (64) + vec4 (16) = 80 bytes.
      uniformBuffer_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
      uniformBuffer_->create();
    }
    if (!bindings_)
    {
      bindings_.reset(rhi_->newShaderResourceBindings());
      bindings_->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
          0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
          uniformBuffer_.get())
      });
      bindings_->create();
    }
    // Rebuild the pipeline against the current render target (sample count / render
    // pass can change when the widget is resized or reparented).
    pipeline_.reset(rhi_->newGraphicsPipeline());
    pipeline_->setShaderStages({
      { QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/surface3d.vert.qsb")) },
      { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/surface3d.frag.qsb")) }
    });
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { 9 * sizeof(float) } });
    inputLayout.setAttributes({
      { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
      { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) },
      { 0, 2, QRhiVertexInputAttribute::Float3, 6 * sizeof(float) },
    });
    pipeline_->setVertexInputLayout(inputLayout);
    pipeline_->setShaderResourceBindings(bindings_.get());
    pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    pipeline_->setSampleCount(renderTarget()->sampleCount());
    pipeline_->setTopology(QRhiGraphicsPipeline::Triangles);
    pipeline_->setCullMode(QRhiGraphicsPipeline::None);   // open sheet: both faces show
    pipeline_->setDepthTest(true);
    pipeline_->setDepthWrite(true);
    pipeline_->create();
  }

  void PeakSurface3DWidget::rebuildMesh()
  {
    vertexData_.clear();
    indexData_.clear();
    indexCount_ = 0;
    if (!hasSurface()) return;

    const int rows = grid_.rows;
    const int cols = grid_.cols;
    const float maximum = grid_.maximum;
    const double dx = 1.0 / (cols - 1);
    const double dz = 1.0 / (rows - 1);

    const auto heightAt = [&](int r, int c)
    {
      const int rr = std::clamp(r, 0, rows - 1);
      const int cc = std::clamp(c, 0, cols - 1);
      return grid_.values[static_cast<std::size_t>(rr) * cols + cc] / maximum * kHeight;
    };

    vertexData_.reserve(static_cast<std::size_t>(rows) * cols * 9);
    for (int r = 0; r < rows; ++r)
      for (int c = 0; c < cols; ++c)
      {
        const double x = c * dx - 0.5;
        const double z = r * dz - 0.5;
        const double h = heightAt(r, c);
        // Central-difference normal of the height field y = h(x, z).
        const double hX = (heightAt(r, c + 1) - heightAt(r, c - 1)) / (2.0 * dx);
        const double hZ = (heightAt(r + 1, c) - heightAt(r - 1, c)) / (2.0 * dz);
        QVector3D normal(static_cast<float>(-hX), 1.0F, static_cast<float>(-hZ));
        normal.normalize();

        const double norm = std::clamp(h / kHeight, 0.0, 1.0);
        const QRgb rgb = RasterShading::sample(colorMap_, norm);

        vertexData_.push_back(static_cast<float>(x));
        vertexData_.push_back(static_cast<float>(h));
        vertexData_.push_back(static_cast<float>(z));
        vertexData_.push_back(normal.x());
        vertexData_.push_back(normal.y());
        vertexData_.push_back(normal.z());
        vertexData_.push_back(qRed(rgb) / 255.0F);
        vertexData_.push_back(qGreen(rgb) / 255.0F);
        vertexData_.push_back(qBlue(rgb) / 255.0F);
      }

    indexData_.reserve(static_cast<std::size_t>(rows - 1) * (cols - 1) * 6);
    for (int r = 0; r + 1 < rows; ++r)
      for (int c = 0; c + 1 < cols; ++c)
      {
        const quint32 i00 = static_cast<quint32>(r * cols + c);
        const quint32 i01 = static_cast<quint32>(r * cols + c + 1);
        const quint32 i10 = static_cast<quint32>((r + 1) * cols + c);
        const quint32 i11 = static_cast<quint32>((r + 1) * cols + c + 1);
        indexData_.insert(indexData_.end(), { i00, i10, i11, i00, i11, i01 });
      }
    indexCount_ = static_cast<quint32>(indexData_.size());

    // (Re)size the immutable geometry buffers to the current mesh.
    vertexBuffer_.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                        vertexData_.size() * sizeof(float)));
    vertexBuffer_->create();
    indexBuffer_.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                       indexData_.size() * sizeof(quint32)));
    indexBuffer_->create();
  }

  void PeakSurface3DWidget::render(QRhiCommandBuffer* cb)
  {
    if (!rhi_) return;
    QRhiResourceUpdateBatch* batch = rhi_->nextResourceUpdateBatch();

    const bool draw = hasSurface();
    if (draw && meshDirty_)
    {
      rebuildMesh();
      if (indexCount_ > 0)
      {
        batch->uploadStaticBuffer(vertexBuffer_.get(), vertexData_.data());
        batch->uploadStaticBuffer(indexBuffer_.get(), indexData_.data());
      }
      meshDirty_ = false;
    }

    // Camera orbit around the surface centre; project with the backend-correct clip
    // space so Vulkan/Metal/D3D/GL all frame identically.
    const QSize pixelSize = renderTarget()->pixelSize();
    const float aspect = pixelSize.height() > 0
      ? static_cast<float>(pixelSize.width()) / static_cast<float>(pixelSize.height()) : 1.0F;
    const QVector3D center(0.0F, static_cast<float>(kHeight) * 0.4F, 0.0F);
    const float radius = 2.4F / static_cast<float>(zoom_);
    const QVector3D eye = center + radius * QVector3D(
      static_cast<float>(std::cos(pitch_) * std::sin(yaw_)),
      static_cast<float>(std::sin(pitch_)),
      static_cast<float>(std::cos(pitch_) * std::cos(yaw_)));
    QMatrix4x4 projection;
    projection.perspective(38.0F, aspect, 0.05F, 50.0F);
    QMatrix4x4 view;
    view.lookAt(eye, center, QVector3D(0.0F, 1.0F, 0.0F));
    const QMatrix4x4 mvp = rhi_->clipSpaceCorrMatrix() * projection * view;

    float uniforms[20];
    std::memcpy(uniforms, mvp.constData(), 16 * sizeof(float));   // column-major, RHI order
    QVector3D lightDir(-0.4F, 0.85F, 0.5F);
    lightDir.normalize();
    uniforms[16] = lightDir.x();
    uniforms[17] = lightDir.y();
    uniforms[18] = lightDir.z();
    uniforms[19] = 0.0F;
    batch->updateDynamicBuffer(uniformBuffer_.get(), 0, sizeof(uniforms), uniforms);

    const QColor clear(15, 16, 23);   // dark backdrop, like the 2-D peak map
    cb->beginPass(renderTarget(), clear, { 1.0F, 0 }, batch);
    if (draw && indexCount_ > 0 && pipeline_)
    {
      cb->setGraphicsPipeline(pipeline_.get());
      cb->setViewport(QRhiViewport(0, 0, static_cast<float>(pixelSize.width()),
                                   static_cast<float>(pixelSize.height())));
      cb->setShaderResources(bindings_.get());
      const QRhiCommandBuffer::VertexInput vertexInput(vertexBuffer_.get(), 0);
      cb->setVertexInput(0, 1, &vertexInput, indexBuffer_.get(), 0,
                         QRhiCommandBuffer::IndexUInt32);
      cb->drawIndexed(indexCount_);
    }
    cb->endPass(batch);
  }

  void PeakSurface3DWidget::releaseResources()
  {
    pipeline_.reset();
    bindings_.reset();
    uniformBuffer_.reset();
    vertexBuffer_.reset();
    indexBuffer_.reset();
    rhi_ = nullptr;
    meshDirty_ = true;
  }
#else   // ---- CPU QPainter fallback (Qt < 6.7) ----------------------------------

  void PeakSurface3DWidget::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient backdrop(0, 0, 0, height());
    backdrop.setColorAt(0.0, QColor(36, 38, 48));
    backdrop.setColorAt(1.0, QColor(15, 16, 23));
    painter.fillRect(rect(), backdrop);
    if (!hasSurface()) return;   // status label carries the message

    const int rows = grid_.rows;
    const int cols = grid_.cols;
    const double maximum = grid_.maximum;
    const double sinYaw = std::sin(yaw_), cosYaw = std::cos(yaw_);
    const double sinPitch = std::sin(pitch_), cosPitch = std::cos(pitch_);
    const std::array<double, 3> light{-0.4, -0.5, 0.75};

    const auto normAt = [&](int r, int c)
    {
      const double value = grid_.values[static_cast<std::size_t>(r) * cols + c];
      return std::isfinite(value) ? std::clamp(value / maximum, 0.0, 1.0) : 0.0;
    };

    std::vector<QPointF> unit(static_cast<std::size_t>(rows) * cols);
    std::vector<double> heightAt(static_cast<std::size_t>(rows) * cols);
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (int r = 0; r < rows; ++r)
      for (int c = 0; c < cols; ++c)
      {
        const double x = c / static_cast<double>(cols - 1) - 0.5;
        const double y = r / static_cast<double>(rows - 1) - 0.5;
        const double h = normAt(r, c) * kHeight;
        heightAt[static_cast<std::size_t>(r) * cols + c] = h;
        const double rx = x * cosYaw - y * sinYaw;
        const double ry = x * sinYaw + y * cosYaw;
        const double py = ry * sinPitch - h * cosPitch;
        unit[static_cast<std::size_t>(r) * cols + c] = QPointF(rx, py);
        minX = std::min(minX, rx); maxX = std::max(maxX, rx);
        minY = std::min(minY, py); maxY = std::max(maxY, py);
      }

    const double marginX = 24.0, marginTop = 34.0, marginBottom = 22.0;
    const double availW = std::max(1.0, width() - 2.0 * marginX);
    const double availH = std::max(1.0, height() - marginTop - marginBottom);
    const double spanX = std::max(1e-6, maxX - minX), spanY = std::max(1e-6, maxY - minY);
    const double scale = std::min(availW / spanX, availH / spanY) * zoom_;
    const double centerX = width() * 0.5 - 0.5 * (minX + maxX) * scale;
    const double centerY = (marginTop + availH * 0.5) - 0.5 * (minY + maxY) * scale;

    std::vector<QPointF> screen(static_cast<std::size_t>(rows) * cols);
    for (std::size_t i = 0; i < screen.size(); ++i)
      screen[i] = QPointF(centerX + unit[i].x() * scale, centerY + unit[i].y() * scale);

    struct Quad { std::array<int, 4> v; double depth; QRgb color; };
    std::vector<Quad> quads;
    quads.reserve(static_cast<std::size_t>(rows - 1) * (cols - 1));
    for (int r = 0; r + 1 < rows; ++r)
      for (int c = 0; c + 1 < cols; ++c)
      {
        const int i00 = r * cols + c, i01 = r * cols + c + 1;
        const int i11 = (r + 1) * cols + c + 1, i10 = (r + 1) * cols + c;
        const double peakNorm = std::max({normAt(r, c), normAt(r, c + 1),
                                          normAt(r + 1, c), normAt(r + 1, c + 1)});
        const double dhx = heightAt[static_cast<std::size_t>(i01)] - heightAt[static_cast<std::size_t>(i00)];
        const double dhy = heightAt[static_cast<std::size_t>(i10)] - heightAt[static_cast<std::size_t>(i00)];
        const double du = 1.0 / (cols - 1), dv = 1.0 / (rows - 1);
        std::array<double, 3> normal{-dhx * dv, -dhy * du, du * dv};
        const double length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1]
                                        + normal[2] * normal[2]);
        double shade = 0.55;
        if (length > 0.0)
          shade = std::clamp(0.45 + 0.55 * ((normal[0] * light[0] + normal[1] * light[1]
                                             + normal[2] * light[2]) / length), 0.3, 1.0);
        const QRgb base = RasterShading::sample(colorMap_, peakNorm);
        const QRgb color = qRgb(static_cast<int>(qRed(base) * shade),
                                static_cast<int>(qGreen(base) * shade),
                                static_cast<int>(qBlue(base) * shade));
        const double avgY = ((r + 0.5) / (rows - 1) - 0.5);
        const double avgX = ((c + 0.5) / (cols - 1) - 0.5);
        const double avgH = 0.25 * (heightAt[static_cast<std::size_t>(i00)]
                                    + heightAt[static_cast<std::size_t>(i01)]
                                    + heightAt[static_cast<std::size_t>(i10)]
                                    + heightAt[static_cast<std::size_t>(i11)]);
        const double depth = (avgX * sinYaw + avgY * cosYaw) * cosPitch + avgH * sinPitch;
        quads.push_back({{i00, i01, i11, i10}, depth, color});
      }
    std::sort(quads.begin(), quads.end(),
              [](const Quad& a, const Quad& b) { return a.depth > b.depth; });

    for (const Quad& quad : quads)
    {
      QPolygonF poly;
      for (const int index : quad.v) poly << screen[static_cast<std::size_t>(index)];
      const QColor fill = QColor::fromRgb(quad.color);
      painter.setPen(QPen(fill, 0.6));
      painter.setBrush(fill);
      painter.drawPolygon(poly);
    }
  }
#endif
}
