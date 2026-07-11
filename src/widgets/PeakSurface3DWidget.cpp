#include "widgets/PeakSurface3DWidget.h"

#include "plot/RasterShading.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>

namespace OpenMSViewer
{
  namespace
  {
    constexpr int kCols = 80;   // RT samples
    constexpr int kRows = 60;   // m/z samples
    constexpr double kHeight = 0.85;

    SurfaceGrid computeGrid(const std::shared_ptr<const OpenMS::MSExperiment>& experiment,
                            const PlotRange& range, unsigned int msLevel)
    {
      SurfaceGrid grid;
      if (!experiment || !range.isValid()) return grid;
      grid.rows = kRows;
      grid.cols = kCols;
      grid.values.assign(static_cast<std::size_t>(kRows) * kCols, 0.0F);
      // Layout matches the peak-map rasterizer: values[mzIndex * rtBins + rtIndex].
      experiment->rasterizeRTMZ(grid.values.data(), kCols, kRows,
                                range.rtMin, range.rtMax, range.mzMin, range.mzMax,
                                msLevel, OpenMS::MSExperiment::RasterAggregation::SUM);
      for (float& value : grid.values)   // sanitize non-finite / negative before use
      {
        if (!std::isfinite(value) || value < 0.0F) value = 0.0F;
        grid.maximum = std::max(grid.maximum, value);
      }
      return grid;
    }
  }

  PeakSurface3DWidget::PeakSurface3DWidget(QWidget* parent) : QWidget(parent)
  {
    setMinimumSize(360, 300);
    setMouseTracking(true);
    setAccessibleName(tr("3-D peak surface"));
    setAccessibleDescription(tr("Intensity surface of the zoomed region. Drag to rotate, wheel to zoom."));
    connect(&watcher_, &QFutureWatcher<SurfaceGrid>::finished, this, [this]
    {
      if (activeGeneration_ != desiredGeneration_) { startRender(); return; }
      grid_ = watcher_.result();
      update();
    });
  }

  PeakSurface3DWidget::~PeakSurface3DWidget()
  {
    if (watcher_.isRunning()) watcher_.waitForFinished();
  }

  bool PeakSurface3DWidget::viewFitsForSurface(const PlotRange& range) noexcept
  {
    return range.isValid() && range.rtSpan() <= kMaxRtSpan && range.mzSpan() <= kMaxMzSpan;
  }

  void PeakSurface3DWidget::setView(std::shared_ptr<const OpenMS::MSExperiment> experiment,
                                    const PlotRange& range, PeakMapColorMap colorMap,
                                    unsigned int msLevel)
  {
    experiment_ = std::move(experiment);
    range_ = range;
    colorMap_ = colorMap;
    msLevel_ = msLevel;
    grid_ = {};
    if (!experiment_ || !viewFitsForSurface(range_))
    {
      ++desiredGeneration_;   // cancel any pending render
      update();
      return;
    }
    startRender();
  }

  void PeakSurface3DWidget::clear()
  {
    experiment_.reset();
    grid_ = {};
    ++desiredGeneration_;
    update();
  }

  void PeakSurface3DWidget::resetOrientation()
  {
    yaw_ = 0.6;
    pitch_ = 0.9;
    zoom_ = 1.0;
    update();
  }

  void PeakSurface3DWidget::startRender()
  {
    ++desiredGeneration_;
    // Don't (re)launch a rasterization for a view that isn't offered in 3-D.
    if (!experiment_ || !viewFitsForSurface(range_) || watcher_.isRunning()) { update(); return; }
    activeGeneration_ = desiredGeneration_;
    const auto experiment = experiment_;
    const PlotRange range = range_;
    const unsigned int msLevel = msLevel_;
    watcher_.setFuture(QtConcurrent::run([experiment, range, msLevel]
    {
      return computeGrid(experiment, range, msLevel);
    }));
    update();
  }

  void PeakSurface3DWidget::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().window());
    const QColor floor = QColor::fromRgb(PeakMapRasterizer::color(0.0, colorMap_));

    if (!experiment_ || !viewFitsForSurface(range_))
    {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(rect(), Qt::AlignCenter,
        tr("Zoom the peak map in (RT ≤ %1 s, m/z ≤ %2) to view the 3-D surface")
          .arg(kMaxRtSpan, 0, 'f', 0).arg(kMaxMzSpan, 0, 'f', 0));
      return;
    }
    if (grid_.rows < 2 || grid_.cols < 2 || grid_.maximum <= 0.0F)
    {
      painter.fillRect(rect(), floor);
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(rect(), watcher_.isRunning() ? Qt::AlignCenter : Qt::AlignCenter,
                       watcher_.isRunning() ? tr("Building surface…") : tr("No signal in this region"));
      return;
    }

    const int rows = grid_.rows;
    const int cols = grid_.cols;
    const double maximum = grid_.maximum;
    const double sinYaw = std::sin(yaw_), cosYaw = std::cos(yaw_);
    const double sinPitch = std::sin(pitch_), cosPitch = std::cos(pitch_);
    const double scale = std::min(width(), height()) * 0.62 * zoom_;
    const double centerX = width() * 0.5;
    const double centerY = height() * 0.54;
    const std::array<double, 3> light{-0.4, -0.5, 0.75};   // toward upper-left-front

    // Project every vertex once; quads reference four of them.
    std::vector<QPointF> screen(static_cast<std::size_t>(rows) * cols);
    std::vector<double> heightAt(static_cast<std::size_t>(rows) * cols);
    const auto normAt = [&](int r, int c)
    {
      const double value = grid_.values[static_cast<std::size_t>(r) * cols + c];
      return std::isfinite(value) ? std::clamp(value / maximum, 0.0, 1.0) : 0.0;
    };
    for (int r = 0; r < rows; ++r)
      for (int c = 0; c < cols; ++c)
      {
        const double x = c / static_cast<double>(cols - 1) - 0.5;   // RT
        const double y = r / static_cast<double>(rows - 1) - 0.5;   // m/z
        const double h = std::sqrt(normAt(r, c)) * kHeight;
        heightAt[static_cast<std::size_t>(r) * cols + c] = h;
        const double rx = x * cosYaw - y * sinYaw;
        const double ry = x * sinYaw + y * cosYaw;
        const double sx = centerX + rx * scale;
        const double sy = centerY + (ry * sinPitch - h * cosPitch) * scale;
        screen[static_cast<std::size_t>(r) * cols + c] = QPointF(sx, sy);
      }

    // Build quads with a depth key and painter's-algorithm order (far first).
    struct Quad { std::array<int, 4> v; double depth; QRgb color; };
    std::vector<Quad> quads;
    quads.reserve(static_cast<std::size_t>(rows - 1) * (cols - 1));
    for (int r = 0; r + 1 < rows; ++r)
      for (int c = 0; c + 1 < cols; ++c)
      {
        const int i00 = r * cols + c, i01 = r * cols + c + 1;
        const int i11 = (r + 1) * cols + c + 1, i10 = (r + 1) * cols + c;
        // Colour by the tallest corner so peaks read bright against the dark floor
        // (a mean would wash a thin spike back into the valley colour).
        const double peakNorm = std::max({normAt(r, c), normAt(r, c + 1),
                                          normAt(r + 1, c), normAt(r + 1, c + 1)});
        // Surface normal (model space) for simple diffuse shading.
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
        // Match the sqrt height transform so colour tracks height and mid-intensity
        // structure is visible rather than washing into the floor.
        const QRgb base = RasterShading::sample(colorMap_, std::sqrt(peakNorm));
        const QRgb color = qRgb(static_cast<int>(qRed(base) * shade),
                                static_cast<int>(qGreen(base) * shade),
                                static_cast<int>(qBlue(base) * shade));
        // Depth along the camera axis (bigger = farther). Must include height, or at
        // high pitch (near top-down) tall and low overlapping quads sort wrongly.
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
              [](const Quad& a, const Quad& b) { return a.depth > b.depth; });   // far -> near

    const QColor edge(0, 0, 0, 40);
    for (const Quad& quad : quads)
    {
      QPolygonF poly;
      for (const int index : quad.v) poly << screen[static_cast<std::size_t>(index)];
      painter.setPen(QPen(edge, 0.4));
      painter.setBrush(QColor::fromRgb(quad.color));
      painter.drawPolygon(poly);
    }

    // Title + orientation hint.
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRect(8, 6, width() - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
      tr("3-D surface · RT %1–%2 s · m/z %3–%4")
        .arg(range_.rtMin, 0, 'f', 1).arg(range_.rtMax, 0, 'f', 1)
        .arg(range_.mzMin, 0, 'f', 2).arg(range_.mzMax, 0, 'f', 2));
    if (watcher_.isRunning())
    {
      painter.setPen(QColor(230, 230, 235));
      painter.setBrush(QColor(0, 0, 0, 150));
      const QRect badge(width() - 96, 8, 86, 24);
      painter.drawRoundedRect(badge, 5, 5);
      painter.drawText(badge, Qt::AlignCenter, tr("Building…"));
    }
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
    update();
  }

  void PeakSurface3DWidget::wheelEvent(QWheelEvent* event)
  {
    const int delta = event->angleDelta().y();
    if (delta == 0) return;   // horizontal / trackpad-phase events aren't zoom
    zoom_ = std::clamp(zoom_ * (delta > 0 ? 1.1 : 1.0 / 1.1), 0.3, 4.0);
    update();
    event->accept();
  }
}
