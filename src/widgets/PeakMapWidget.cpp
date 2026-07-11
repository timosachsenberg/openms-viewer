#include "widgets/PeakMapWidget.h"

#include "plot/PeakMapRasterizer.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenMSViewer
{
  namespace
  {
    std::vector<double> niceTicks(double minimum, double maximum, int target = 7)
    {
      std::vector<double> ticks;
      const double span = maximum - minimum;
      if (!(span > 0.0)) return ticks;
      const double rawStep = span / std::max(1, target);
      const double magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));
      const double residual = rawStep / magnitude;
      const double factor = residual <= 1.0 ? 1.0 : residual <= 2.0 ? 2.0 : residual <= 5.0 ? 5.0 : 10.0;
      const double step = factor * magnitude;
      const double first = std::ceil(minimum / step) * step;
      for (double value = first; value <= maximum + step * 1e-9; value += step)
      {
        ticks.push_back(value);
      }
      return ticks;
    }

    QString tickLabel(double value, double span)
    {
      const int precision = span < 0.1 ? 4 : span < 1.0 ? 3 : span < 10.0 ? 2 : span < 100.0 ? 1 : 0;
      return QString::number(value, 'f', precision);
    }
  }

  PeakMapWidget::PeakMapWidget(QWidget* parent) : QWidget(parent)
  {
    setMinimumSize(480, 300);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("RT and m/z peak map"));
    setAccessibleDescription(
      tr("Interactive peak map. Use the mouse wheel to zoom, drag in the selected mode, "
         "Home to reset, and Alt+Left to return to the previous view."));
    renderTimer_.setSingleShot(true);
    renderTimer_.setInterval(45);
    connect(&renderTimer_, &QTimer::timeout, this, &PeakMapWidget::startRender);
    connect(&renderWatcher_, &QFutureWatcher<QImage>::finished, this, [this]
    {
      if (activeGeneration_ == desiredGeneration_)
      {
        raster_ = renderWatcher_.result();
        update();
      }
      else if (experiment_)
      {
        renderTimer_.start(0);
      }
    });
    connect(&minimapWatcher_, &QFutureWatcher<QImage>::finished, this, [this]
    {
      if (activeMinimapGeneration_ == desiredMinimapGeneration_)
      {
        minimap_ = minimapWatcher_.result();
        update();
      }
      else startMinimapRender();
    });
  }

  PeakMapWidget::~PeakMapWidget()
  {
    if (renderWatcher_.isRunning()) renderWatcher_.waitForFinished();
    if (minimapWatcher_.isRunning()) minimapWatcher_.waitForFinished();
  }

  void PeakMapWidget::setExperiment(std::shared_ptr<const OpenMS::MSExperiment> experiment,
                                    const PlotRange& bounds)
  {
    experiment_ = std::move(experiment);
    dataBounds_ = bounds.normalized();
    view_ = dataBounds_;
    history_.clear();
    raster_ = {};
    minimap_ = {};
    ++desiredMinimapGeneration_;
    hasSelectedRt_ = false;
    emit zoomHistoryChanged(false);
    emit viewRangeChanged(view_);
    scheduleRender();
    startMinimapRender();
  }

  void PeakMapWidget::clear()
  {
    experiment_.reset();
    raster_ = {};
    minimap_ = {};
    ++desiredMinimapGeneration_;
    history_.clear();
    hasSelectedRt_ = false;
    ++desiredGeneration_;
    emit zoomHistoryChanged(false);
    update();
  }

  void PeakMapWidget::setSelectedRt(double rt)
  {
    selectedRt_ = rt;
    hasSelectedRt_ = true;
    update();
  }

  void PeakMapWidget::setAxesSwapped(bool swapped)
  {
    if (axesSwapped_ == swapped) return;
    axesSwapped_ = swapped;
    minimap_ = {};
    ++desiredMinimapGeneration_;
    scheduleRender();
    startMinimapRender();
    update();
  }

  void PeakMapWidget::setFeatures(const std::vector<FeatureRecord>& features)
  {
    features_ = features;
    selectedFeature_.reset();
    hoveredFeature_.reset();
    update();
  }

  void PeakMapWidget::setSelectedFeature(std::optional<std::size_t> featureIndex)
  {
    if (featureIndex && *featureIndex >= features_.size()) featureIndex.reset();
    selectedFeature_ = featureIndex;
    update();
  }

  void PeakMapWidget::setIdentifications(const std::vector<IdentificationRecord>& identifications)
  {
    identifications_ = identifications;
    selectedIdentification_.reset();
    hoveredIdentification_.reset();
    update();
  }

  void PeakMapWidget::setSelectedIdentification(std::optional<std::size_t> identificationIndex)
  {
    if (identificationIndex && *identificationIndex >= identifications_.size()) identificationIndex.reset();
    selectedIdentification_ = identificationIndex;
    update();
  }

  bool PeakMapWidget::axesSwapped() const noexcept { return axesSwapped_; }
  const PlotRange& PeakMapWidget::viewRange() const noexcept { return view_; }
  bool PeakMapWidget::canZoomBack() const noexcept { return !history_.empty(); }
  bool PeakMapWidget::hasExperiment() const noexcept { return experiment_ != nullptr; }
  std::optional<std::size_t> PeakMapWidget::selectedFeature() const noexcept { return selectedFeature_; }
  std::optional<std::size_t> PeakMapWidget::selectedIdentification() const noexcept
  {
    return selectedIdentification_;
  }
  QPointF PeakMapWidget::mapDataToWidget(double rt, double mz) const { return pixelFor(rt, mz); }
  const QImage& PeakMapWidget::rasterImage() const noexcept { return raster_; }
  const QImage& PeakMapWidget::minimapImage() const noexcept { return minimap_; }

  QRect PeakMapWidget::minimapRect() const
  {
    const QRect area = plotRect();
    const int width = std::min(220, std::max(90, area.width() / 4));
    const int height = std::min(130, std::max(65, area.height() / 4));
    return QRect(area.right() - width - 10, area.bottom() - height - 10, width, height);
  }

  void PeakMapWidget::resetView()
  {
    if (!experiment_) return;
    if (view_.rtMin != dataBounds_.rtMin || view_.rtMax != dataBounds_.rtMax
        || view_.mzMin != dataBounds_.mzMin || view_.mzMax != dataBounds_.mzMax)
    {
      rememberCurrentRange();
      applyRange(dataBounds_, false);
    }
  }

  void PeakMapWidget::zoomBack()
  {
    if (history_.empty()) return;
    const PlotRange previous = history_.back();
    history_.pop_back();
    applyRange(previous, false);
    emit zoomHistoryChanged(!history_.empty());
  }

  void PeakMapWidget::zoomIn()
  {
    applyRange(view_.zoomed((view_.rtMin + view_.rtMax) / 2.0,
                            (view_.mzMin + view_.mzMax) / 2.0, 0.8), true);
  }

  void PeakMapWidget::zoomOut()
  {
    applyRange(view_.zoomed((view_.rtMin + view_.rtMax) / 2.0,
                            (view_.mzMin + view_.mzMax) / 2.0, 1.25), true);
  }

  void PeakMapWidget::panLeft()
  {
    const PlotRange shifted = axesSwapped_
      ? view_.translated(0.0, -view_.mzSpan() * 0.1)
      : view_.translated(-view_.rtSpan() * 0.1, 0.0);
    applyRange(shifted, true);
  }

  void PeakMapWidget::panRight()
  {
    const PlotRange shifted = axesSwapped_
      ? view_.translated(0.0, view_.mzSpan() * 0.1)
      : view_.translated(view_.rtSpan() * 0.1, 0.0);
    applyRange(shifted, true);
  }

  void PeakMapWidget::panUp()
  {
    const PlotRange shifted = axesSwapped_
      ? view_.translated(view_.rtSpan() * 0.1, 0.0)
      : view_.translated(0.0, view_.mzSpan() * 0.1);
    applyRange(shifted, true);
  }

  void PeakMapWidget::panDown()
  {
    const PlotRange shifted = axesSwapped_
      ? view_.translated(-view_.rtSpan() * 0.1, 0.0)
      : view_.translated(0.0, -view_.mzSpan() * 0.1);
    applyRange(shifted, true);
  }

  void PeakMapWidget::setRtRange(double minimumRt, double maximumRt)
  {
    if (!experiment_ || maximumRt <= minimumRt) return;
    PlotRange next = view_;
    next.rtMin = minimumRt;
    next.rtMax = maximumRt;
    applyRange(next, true);
  }

  void PeakMapWidget::setColorMap(int colorMapIndex)
  {
    const auto next = static_cast<PeakMapColorMap>(std::clamp(colorMapIndex, 0, 6));
    if (colorMap_ == next) return;
    colorMap_ = next;
    minimap_ = {};
    ++desiredMinimapGeneration_;
    scheduleRender();
    startMinimapRender();
  }

  void PeakMapWidget::setIntensityScale(int intensityScaleIndex)
  {
    const auto next = static_cast<PeakMapIntensityScale>(std::clamp(intensityScaleIndex, 0, 3));
    if (intensityScale_ == next) return;
    intensityScale_ = next;
    minimap_ = {};
    ++desiredMinimapGeneration_;
    scheduleRender();
    startMinimapRender();
  }

  void PeakMapWidget::setInteractionMode(int modeIndex)
  {
    const auto mode = static_cast<PeakMapInteractionMode>(std::clamp(modeIndex, 0, 2));
    if (mode == interactionMode_) return;
    interactionMode_ = mode;
    updateInteractionCursor();
    update();
    emit interactionModeChanged(static_cast<int>(interactionMode_));
  }

  void PeakMapWidget::setShowMinimap(bool show)
  {
    showMinimap_ = show;
    update();
  }

  void PeakMapWidget::setShowFeatureCentroids(bool show)
  {
    showFeatureCentroids_ = show;
    if (!show) hoveredFeature_.reset();
    update();
  }

  void PeakMapWidget::setShowFeatureBounds(bool show)
  {
    showFeatureBounds_ = show;
    update();
  }

  void PeakMapWidget::setShowFeatureHulls(bool show)
  {
    showFeatureHulls_ = show;
    update();
  }

  void PeakMapWidget::zoomToFeature(std::size_t featureIndex)
  {
    if (!experiment_ || featureIndex >= features_.size()) return;
    const FeatureRecord& feature = features_[featureIndex];
    const double rtSpan = std::max(feature.bounds.rtSpan(), 20.0);
    const double mzSpan = std::max(feature.bounds.mzSpan(), 4.0);
    const double rtCenter = (feature.bounds.rtMin + feature.bounds.rtMax) / 2.0;
    const double mzCenter = (feature.bounds.mzMin + feature.bounds.mzMax) / 2.0;
    selectedFeature_ = featureIndex;
    applyRange({rtCenter - rtSpan * 0.7, rtCenter + rtSpan * 0.7,
                mzCenter - mzSpan * 0.7, mzCenter + mzSpan * 0.7}, true);
  }

  void PeakMapWidget::setShowIdentifications(bool show)
  {
    showIdentifications_ = show;
    if (!show) hoveredIdentification_.reset();
    update();
  }

  void PeakMapWidget::setShowIdentificationSequences(bool show)
  {
    showIdentificationSequences_ = show;
    update();
  }

  void PeakMapWidget::zoomToIdentification(std::size_t identificationIndex)
  {
    if (!experiment_ || identificationIndex >= identifications_.size()) return;
    const IdentificationRecord& identification = identifications_[identificationIndex];
    if (!std::isfinite(identification.rt) || !std::isfinite(identification.mz)) return;
    selectedIdentification_ = identificationIndex;
    applyRange({identification.rt - 14.0, identification.rt + 14.0,
                identification.mz - 2.8, identification.mz + 2.8}, true);
  }

  QRect PeakMapWidget::plotRect() const
  {
    return rect().adjusted(68, 20, -22, -52);
  }

  QPointF PeakMapWidget::dataAt(const QPointF& position) const
  {
    const QRect area = plotRect();
    const double x = std::clamp((position.x() - area.left()) / static_cast<double>(area.width()), 0.0, 1.0);
    const double y = std::clamp((area.bottom() - position.y()) / static_cast<double>(area.height()), 0.0, 1.0);
    if (axesSwapped_)
    {
      return {view_.rtMin + y * view_.rtSpan(), view_.mzMin + x * view_.mzSpan()};
    }
    return {view_.rtMin + x * view_.rtSpan(), view_.mzMin + y * view_.mzSpan()};
  }

  QPointF PeakMapWidget::pixelFor(double rt, double mz) const
  {
    const QRect area = plotRect();
    const double rtFraction = (rt - view_.rtMin) / view_.rtSpan();
    const double mzFraction = (mz - view_.mzMin) / view_.mzSpan();
    if (axesSwapped_)
    {
      return {area.left() + mzFraction * area.width(), area.bottom() - rtFraction * area.height()};
    }
    return {area.left() + rtFraction * area.width(), area.bottom() - mzFraction * area.height()};
  }

  QSize PeakMapWidget::densityAwareRenderSize() const
  {
    // Render at full canvas resolution. The rasterizer grows sparse points into
    // small blobs (adaptive spread), so deep zooms show crisp peaks instead of a
    // low-resolution grid stretched to fill the view.
    return plotRect().size();
  }

  void PeakMapWidget::scheduleRender()
  {
    ++desiredGeneration_;
    if (!experiment_)
    {
      raster_ = {};
      update();
      return;
    }
    renderTimer_.start();
    update();
  }

  void PeakMapWidget::startRender()
  {
    if (!experiment_ || renderWatcher_.isRunning()) return;
    const QSize renderSize = densityAwareRenderSize();
    if (renderSize.width() <= 0 || renderSize.height() <= 0) return;

    activeGeneration_ = desiredGeneration_;
    const auto experiment = experiment_;
    const PlotRange range = view_;
    const bool swapped = axesSwapped_;
    const PeakMapColorMap colorMap = colorMap_;
    const PeakMapIntensityScale intensityScale = intensityScale_;
    renderWatcher_.setFuture(QtConcurrent::run([experiment, range, renderSize, swapped, colorMap, intensityScale]
    {
      return PeakMapRasterizer::render(*experiment, range, renderSize, swapped, 1, colorMap, intensityScale);
    }));
  }

  void PeakMapWidget::startMinimapRender()
  {
    if (!experiment_ || minimapWatcher_.isRunning()) return;
    const auto experiment = experiment_;
    const PlotRange bounds = dataBounds_;
    const bool swapped = axesSwapped_;
    const PeakMapColorMap colorMap = colorMap_;
    const PeakMapIntensityScale intensityScale = intensityScale_;
    activeMinimapGeneration_ = desiredMinimapGeneration_;
    minimapWatcher_.setFuture(QtConcurrent::run([experiment, bounds, swapped, colorMap, intensityScale]
    {
      return PeakMapRasterizer::render(*experiment, bounds, QSize(220, 130), swapped, 1, colorMap, intensityScale);
    }));
  }

  void PeakMapWidget::applyRange(const PlotRange& range, bool remember)
  {
    if (!experiment_) return;
    PlotRange next = range.normalized().clampedTo(dataBounds_);
    const double minRtSpan = dataBounds_.rtSpan() * 1e-8;
    const double minMzSpan = dataBounds_.mzSpan() * 1e-8;
    if (next.rtSpan() < minRtSpan || next.mzSpan() < minMzSpan) return;
    if (remember) rememberCurrentRange();
    view_ = next;
    emit viewRangeChanged(view_);
    scheduleRender();
  }

  void PeakMapWidget::rememberCurrentRange()
  {
    if (!history_.empty())
    {
      const PlotRange& last = history_.back();
      if (last.rtMin == view_.rtMin && last.rtMax == view_.rtMax
          && last.mzMin == view_.mzMin && last.mzMax == view_.mzMax) return;
    }
    history_.push_back(view_);
    if (history_.size() > 10) history_.erase(history_.begin());
    emit zoomHistoryChanged(true);
  }

  void PeakMapWidget::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), palette().window());
    const QRect area = plotRect();
    // Colormap floor so the plot ground matches the raster's "no data" colour and
    // there is no seam between the widget background and the rendered image.
    painter.fillRect(area, QColor::fromRgb(PeakMapRasterizer::color(0.0, colorMap_)));

    if (!raster_.isNull()) painter.drawImage(area, raster_);
    drawAxes(painter, area);

    if (!experiment_)
    {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(area, Qt::AlignCenter,
                       tr("Open or drop an mzML file to begin"));
      return;
    }

    if (renderWatcher_.isRunning() || renderTimer_.isActive())
    {
      painter.setPen(QColor(230, 230, 235));
      painter.setBrush(QColor(0, 0, 0, 150));
      const QRect badge(area.right() - 94, area.top() + 8, 84, 26);
      painter.drawRoundedRect(badge, 5, 5);
      painter.drawText(badge, Qt::AlignCenter, tr("Rendering…"));
    }

    if (hasSelectedRt_ && selectedRt_ >= view_.rtMin && selectedRt_ <= view_.rtMax)
    {
      painter.setPen(QPen(QColor(255, 94, 94), 1.5, Qt::DashLine));
      const QPointF point = pixelFor(selectedRt_, (view_.mzMin + view_.mzMax) / 2.0);
      if (axesSwapped_) painter.drawLine(QPointF(area.left(), point.y()), QPointF(area.right(), point.y()));
      else painter.drawLine(QPointF(point.x(), area.top()), QPointF(point.x(), area.bottom()));
    }

    drawFeatures(painter);
    drawIdentifications(painter);
    drawLegend(painter, area);

    if (showMinimap_ && !minimap_.isNull())
    {
      const QRect mini = minimapRect();
      painter.setOpacity(0.94);
      painter.drawImage(mini, minimap_);
      painter.setOpacity(1.0);
      painter.setPen(QPen(QColor(225, 225, 235), 1.2));
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(mini);
      QRectF viewport;
      if (axesSwapped_)
      {
        const double left = mini.left() + (view_.mzMin - dataBounds_.mzMin)
          / dataBounds_.mzSpan() * mini.width();
        const double right = mini.left() + (view_.mzMax - dataBounds_.mzMin)
          / dataBounds_.mzSpan() * mini.width();
        const double top = mini.bottom() - (view_.rtMax - dataBounds_.rtMin)
          / dataBounds_.rtSpan() * mini.height();
        const double bottom = mini.bottom() - (view_.rtMin - dataBounds_.rtMin)
          / dataBounds_.rtSpan() * mini.height();
        viewport = QRectF(QPointF(left, top), QPointF(right, bottom));
      }
      else
      {
        const double left = mini.left() + (view_.rtMin - dataBounds_.rtMin)
          / dataBounds_.rtSpan() * mini.width();
        const double right = mini.left() + (view_.rtMax - dataBounds_.rtMin)
          / dataBounds_.rtSpan() * mini.width();
        const double top = mini.bottom() - (view_.mzMax - dataBounds_.mzMin)
          / dataBounds_.mzSpan() * mini.height();
        const double bottom = mini.bottom() - (view_.mzMin - dataBounds_.mzMin)
          / dataBounds_.mzSpan() * mini.height();
        viewport = QRectF(QPointF(left, top), QPointF(right, bottom));
      }
      painter.setPen(QPen(QColor(255, 225, 70), 2.0));
      painter.setBrush(QColor(255, 225, 70, 25));
      painter.drawRect(viewport.normalized());
    }

    if (dragMode_ == DragMode::Zoom)
    {
      painter.setPen(QPen(QColor(80, 210, 255), 1.5, Qt::DashLine));
      painter.setBrush(QColor(80, 210, 255, 35));
      painter.drawRect(QRect(dragStart_, dragCurrent_).normalized().intersected(area));
    }
    else if (dragMode_ == DragMode::Measure)
    {
      painter.setPen(QPen(QColor(255, 215, 70), 2));
      painter.drawLine(dragStart_, dragCurrent_);
      const QPointF start = dataAt(dragStart_);
      const QPointF end = dataAt(dragCurrent_);
      const QString label = tr("ΔRT %1 s   Δm/z %2")
                              .arg(std::abs(end.x() - start.x()), 0, 'f', 2)
                              .arg(std::abs(end.y() - start.y()), 0, 'f', 4);
      const QRect labelRect = painter.fontMetrics().boundingRect(label).adjusted(-7, -4, 7, 4);
      QRect placed = labelRect;
      placed.moveCenter((dragStart_ + dragCurrent_) / 2);
      painter.fillRect(placed, QColor(0, 0, 0, 190));
      painter.drawText(placed, Qt::AlignCenter, label);
    }
  }

  void PeakMapWidget::drawAxes(QPainter& painter, const QRect& area) const
  {
    painter.save();
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.drawRect(area);

    const auto xTicks = axesSwapped_ ? niceTicks(view_.mzMin, view_.mzMax)
                                     : niceTicks(view_.rtMin, view_.rtMax);
    const auto yTicks = axesSwapped_ ? niceTicks(view_.rtMin, view_.rtMax)
                                     : niceTicks(view_.mzMin, view_.mzMax);
    const double xMin = axesSwapped_ ? view_.mzMin : view_.rtMin;
    const double xSpan = axesSwapped_ ? view_.mzSpan() : view_.rtSpan();
    const double yMin = axesSwapped_ ? view_.rtMin : view_.mzMin;
    const double ySpan = axesSwapped_ ? view_.rtSpan() : view_.mzSpan();

    painter.setPen(palette().color(QPalette::Text));
    for (double value : xTicks)
    {
      const int x = area.left() + static_cast<int>((value - xMin) / xSpan * area.width());
      painter.drawLine(x, area.bottom(), x, area.bottom() + 5);
      painter.drawText(QRect(x - 42, area.bottom() + 7, 84, 20), Qt::AlignHCenter | Qt::AlignTop,
                       tickLabel(value, xSpan));
    }
    for (double value : yTicks)
    {
      const int y = area.bottom() - static_cast<int>((value - yMin) / ySpan * area.height());
      painter.drawLine(area.left() - 5, y, area.left(), y);
      painter.drawText(QRect(2, y - 10, area.left() - 10, 20), Qt::AlignRight | Qt::AlignVCenter,
                       tickLabel(value, ySpan));
    }

    painter.drawText(QRect(area.left(), area.bottom() + 30, area.width(), 20), Qt::AlignCenter,
                     axesSwapped_ ? tr("m/z") : tr("Retention time (s)"));
    painter.translate(16, area.center().y());
    painter.rotate(-90);
    painter.drawText(QRect(-area.height() / 2, -10, area.height(), 20), Qt::AlignCenter,
                     axesSwapped_ ? tr("Retention time (s)") : tr("m/z"));
    painter.restore();
  }

  void PeakMapWidget::drawFeatures(QPainter& painter) const
  {
    if (features_.empty() || (!showFeatureCentroids_ && !showFeatureBounds_ && !showFeatureHulls_)) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    std::size_t drawn = 0;
    for (const FeatureRecord& feature : features_)
    {
      if (drawn >= 10000) break;
      if (feature.bounds.rtMax < view_.rtMin || feature.bounds.rtMin > view_.rtMax
          || feature.bounds.mzMax < view_.mzMin || feature.bounds.mzMin > view_.mzMax) continue;
      ++drawn;

      const bool selected = selectedFeature_ && *selectedFeature_ == feature.index;
      const bool hovered = hoveredFeature_ && *hoveredFeature_ == feature.index;
      const QColor color = selected ? QColor(255, 100, 255)
                           : hovered ? QColor(255, 200, 0)
                                     : QColor(0, 255, 100);
      const int lineWidth = selected ? 3 : hovered ? 2 : 1;

      if (showFeatureHulls_)
      {
        for (const auto& hull : feature.hulls)
        {
          if (hull.size() < 3) continue;
          QPolygonF polygon;
          polygon.reserve(static_cast<int>(hull.size()));
          for (const FeaturePoint& point : hull) polygon << pixelFor(point.rt, point.mz);
          QColor hullColor = selected ? QColor(255, 100, 255) : hovered ? QColor(255, 200, 0)
                                                                    : QColor(0, 200, 255);
          QColor fill = hullColor;
          fill.setAlpha(selected ? 100 : 50);
          painter.setPen(QPen(hullColor, lineWidth));
          painter.setBrush(fill);
          painter.drawPolygon(polygon);
        }
      }

      if (showFeatureBounds_)
      {
        const QPointF first = pixelFor(feature.bounds.rtMin, feature.bounds.mzMin);
        const QPointF second = pixelFor(feature.bounds.rtMax, feature.bounds.mzMax);
        painter.setPen(QPen(selected || hovered ? color : QColor(255, 255, 0, 210), lineWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(first, second).normalized());
      }

      if (showFeatureCentroids_)
      {
        const QPointF center = pixelFor(feature.rt, feature.mz);
        if (hovered && !selected)
        {
          painter.setPen(QPen(QColor(255, 200, 0, 160), 2));
          painter.setBrush(Qt::NoBrush);
          painter.drawEllipse(center, 8, 8);
        }
        const double radius = selected ? 6.0 : hovered ? 5.0 : 3.0;
        painter.setPen(QPen(Qt::white, selected || hovered ? 2 : 1));
        painter.setBrush(color);
        painter.drawEllipse(center, radius, radius);
      }
    }
    painter.restore();
  }

  std::optional<std::size_t> PeakMapWidget::nearestFeature(const QPointF& position) const
  {
    if (!showFeatureCentroids_ || features_.empty() || !plotRect().contains(position.toPoint()))
      return std::nullopt;
    double bestSquaredDistance = 15.0 * 15.0;
    std::optional<std::size_t> best;
    const std::size_t limit = std::min<std::size_t>(features_.size(), 10000);
    for (std::size_t index = 0; index < limit; ++index)
    {
      const FeatureRecord& feature = features_[index];
      if (feature.rt < view_.rtMin || feature.rt > view_.rtMax
          || feature.mz < view_.mzMin || feature.mz > view_.mzMax) continue;
      const QPointF pixel = pixelFor(feature.rt, feature.mz);
      const double dx = position.x() - pixel.x();
      const double dy = position.y() - pixel.y();
      const double squaredDistance = dx * dx + dy * dy;
      if (squaredDistance < bestSquaredDistance)
      {
        bestSquaredDistance = squaredDistance;
        best = feature.index;
      }
    }
    return best;
  }

  void PeakMapWidget::drawIdentifications(QPainter& painter) const
  {
    if (!showIdentifications_ || identifications_.empty()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    std::size_t drawn = 0;
    std::vector<QRectF> labelRects;
    for (const IdentificationRecord& identification : identifications_)
    {
      if (drawn >= 5000) break;
      if (!std::isfinite(identification.rt) || !std::isfinite(identification.mz)
          || identification.rt < view_.rtMin || identification.rt > view_.rtMax
          || identification.mz < view_.mzMin || identification.mz > view_.mzMax) continue;
      ++drawn;
      const bool selected = selectedIdentification_ && *selectedIdentification_ == identification.index;
      const bool hovered = hoveredIdentification_ && *hoveredIdentification_ == identification.index;
      const QColor color = selected ? QColor(255, 50, 50) : hovered ? QColor(255, 210, 60)
                                                                  : QColor(255, 150, 50);
      const double radius = selected ? 7.0 : hovered ? 6.0 : 4.5;
      const QPointF center = pixelFor(identification.rt, identification.mz);
      QPolygonF diamond;
      diamond << QPointF(center.x(), center.y() - radius)
              << QPointF(center.x() + radius, center.y())
              << QPointF(center.x(), center.y() + radius)
              << QPointF(center.x() - radius, center.y());
      painter.setPen(QPen(Qt::white, selected || hovered ? 2 : 1));
      painter.setBrush(color);
      painter.drawPolygon(diamond);

      if (showIdentificationSequences_ && identification.bestHit())
      {
        QString sequence = identification.bestHit()->sequence;
        if (sequence.size() > 10) sequence = sequence.left(10) + QStringLiteral("…");
        const QSize labelSize = painter.fontMetrics().size(Qt::TextSingleLine, sequence);
        const QRectF labelRect(QPointF(center.x() + radius + 3.0,
                                      center.y() - labelSize.height()), labelSize);
        const bool overlaps = std::any_of(labelRects.cbegin(), labelRects.cend(),
          [&labelRect](const QRectF& used) { return used.intersects(labelRect.adjusted(-2, -1, 2, 1)); });
        if (!overlaps)
        {
          labelRects.push_back(labelRect);
          painter.setPen(color);
          painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, sequence);
        }
      }
    }
    painter.restore();
  }

  void PeakMapWidget::drawLegend(QPainter& painter, const QRect& area) const
  {
    painter.save();
    const QString mode = interactionMode_ == PeakMapInteractionMode::Pan ? tr("Pan")
      : interactionMode_ == PeakMapInteractionMode::Measure ? tr("Measure") : tr("Zoom");
    const QString hint = tr("%1 mode · wheel zoom · double-click reset").arg(mode);
    const int hintWidth = painter.fontMetrics().horizontalAdvance(hint) + 18;
    const QRect hintRect(area.left() + 8, area.top() + 8, hintWidth, 24);
    painter.setPen(QColor(230, 230, 235));
    painter.setBrush(QColor(0, 0, 0, 155));
    painter.drawRoundedRect(hintRect, 5, 5);
    painter.drawText(hintRect, Qt::AlignCenter, hint);

    int legendY = hintRect.bottom() + 8;
    if (showFeatureCentroids_ && !features_.empty())
    {
      painter.setPen(Qt::white);
      painter.setBrush(QColor(0, 255, 100));
      painter.drawEllipse(QPointF(area.left() + 17, legendY + 6), 4, 4);
      painter.drawText(area.left() + 28, legendY + 12, tr("Feature"));
      legendY += 18;
    }
    if (showIdentifications_ && !identifications_.empty())
    {
      painter.setPen(Qt::white);
      painter.setBrush(QColor(255, 150, 50));
      const QPointF center(area.left() + 17, legendY + 6);
      QPolygonF diamond;
      diamond << QPointF(center.x(), center.y() - 5) << QPointF(center.x() + 5, center.y())
              << QPointF(center.x(), center.y() + 5) << QPointF(center.x() - 5, center.y());
      painter.drawPolygon(diamond);
      painter.drawText(area.left() + 28, legendY + 12, tr("Identification"));
    }

    const int barHeight = std::min(120, std::max(70, area.height() / 4));
    const QRect colorBar(area.right() - 18, area.top() + 44, 10, barHeight);
    for (int y = 0; y < colorBar.height(); ++y)
    {
      const double value = 1.0 - y / static_cast<double>(std::max(1, colorBar.height() - 1));
      painter.setPen(QColor::fromRgb(PeakMapRasterizer::color(value, colorMap_)));
      painter.drawLine(colorBar.left(), colorBar.top() + y, colorBar.right(), colorBar.top() + y);
    }
    painter.setPen(QColor(235, 235, 240));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(colorBar);
    painter.drawText(QPoint(colorBar.left() - 20, colorBar.top() - 4), tr("High"));
    painter.drawText(QPoint(colorBar.left() - 18, colorBar.bottom() + 14), tr("Low"));
    painter.restore();
  }

  void PeakMapWidget::updateInteractionCursor()
  {
    if (interactionMode_ == PeakMapInteractionMode::Pan) setCursor(Qt::OpenHandCursor);
    else setCursor(Qt::CrossCursor);
  }

  std::optional<std::size_t> PeakMapWidget::nearestIdentification(const QPointF& position) const
  {
    if (!showIdentifications_ || identifications_.empty() || !plotRect().contains(position.toPoint()))
      return std::nullopt;
    double bestSquaredDistance = 15.0 * 15.0;
    std::optional<std::size_t> best;
    const std::size_t limit = std::min<std::size_t>(identifications_.size(), 5000);
    for (std::size_t index = 0; index < limit; ++index)
    {
      const IdentificationRecord& identification = identifications_[index];
      if (!std::isfinite(identification.rt) || !std::isfinite(identification.mz)
          || identification.rt < view_.rtMin || identification.rt > view_.rtMax
          || identification.mz < view_.mzMin || identification.mz > view_.mzMax) continue;
      const QPointF pixel = pixelFor(identification.rt, identification.mz);
      const double dx = position.x() - pixel.x();
      const double dy = position.y() - pixel.y();
      const double squaredDistance = dx * dx + dy * dy;
      if (squaredDistance < bestSquaredDistance)
      {
        bestSquaredDistance = squaredDistance;
        best = identification.index;
      }
    }
    return best;
  }

  double PeakMapWidget::nearestIntensity(double rt, double mz) const
  {
    if (!experiment_ || experiment_->empty()) return -1.0;
    const auto& spectra = experiment_->getSpectra();
    auto center = std::lower_bound(spectra.cbegin(), spectra.cend(), rt,
      [](const OpenMS::MSSpectrum& spectrum, double value) { return spectrum.getRT() < value; });
    const std::size_t centerIndex = static_cast<std::size_t>(std::distance(spectra.cbegin(), center));
    const OpenMS::MSSpectrum* nearest = nullptr;
    double nearestRtDistance = std::numeric_limits<double>::infinity();
    for (std::size_t offset = 0; offset < 64; ++offset)
    {
      for (const std::size_t index : {centerIndex >= offset ? centerIndex - offset : spectra.size(),
                                      centerIndex + offset})
      {
        if (index >= spectra.size() || spectra[index].getMSLevel() != 1) continue;
        const double distance = std::abs(spectra[index].getRT() - rt);
        if (distance < nearestRtDistance)
        {
          nearestRtDistance = distance;
          nearest = &spectra[index];
        }
      }
      if (nearest && offset > 4) break;
    }
    if (!nearest || nearest->empty()) return -1.0;
    auto peak = std::lower_bound(nearest->cbegin(), nearest->cend(), mz,
      [](const OpenMS::Peak1D& candidate, double value) { return candidate.getMZ() < value; });
    if (peak == nearest->cend()) --peak;
    else if (peak != nearest->cbegin())
    {
      const auto previous = peak - 1;
      if (std::abs(previous->getMZ() - mz) < std::abs(peak->getMZ() - mz)) peak = previous;
    }
    const double tolerance = view_.mzSpan() / std::max(1, plotRect().width()) * 5.0;
    return std::abs(peak->getMZ() - mz) <= tolerance ? peak->getIntensity() : -1.0;
  }

  void PeakMapWidget::resizeEvent(QResizeEvent* event)
  {
    QWidget::resizeEvent(event);
    scheduleRender();
  }

  void PeakMapWidget::wheelEvent(QWheelEvent* event)
  {
    if (!experiment_ || !plotRect().contains(event->position().toPoint())) return;
    const QPointF anchor = dataAt(event->position());
    const double factor = event->angleDelta().y() > 0 ? 0.8 : 1.25;
    applyRange(view_.zoomed(anchor.x(), anchor.y(), factor), true);
    event->accept();
  }

  void PeakMapWidget::mousePressEvent(QMouseEvent* event)
  {
    if (experiment_ && event->button() == Qt::LeftButton && showMinimap_
        && !minimap_.isNull() && minimapRect().contains(event->position().toPoint()))
    {
      const QRect mini = minimapRect();
      const double x = std::clamp((event->position().x() - mini.left())
        / mini.width(), 0.0, 1.0);
      const double y = std::clamp((mini.bottom() - event->position().y())
        / mini.height(), 0.0, 1.0);
      const double centerRt = axesSwapped_ ? dataBounds_.rtMin + y * dataBounds_.rtSpan()
                                            : dataBounds_.rtMin + x * dataBounds_.rtSpan();
      const double centerMz = axesSwapped_ ? dataBounds_.mzMin + x * dataBounds_.mzSpan()
                                            : dataBounds_.mzMin + y * dataBounds_.mzSpan();
      applyRange({centerRt - view_.rtSpan() / 2.0, centerRt + view_.rtSpan() / 2.0,
                  centerMz - view_.mzSpan() / 2.0, centerMz + view_.mzSpan() / 2.0}, true);
      event->accept();
      return;
    }
    if (!experiment_ || event->button() != Qt::LeftButton || !plotRect().contains(event->position().toPoint()))
    {
      QWidget::mousePressEvent(event);
      return;
    }
    setFocus();
    dragStart_ = dragCurrent_ = dragPrevious_ = event->position().toPoint();
    dragStartRange_ = view_;
    if (event->modifiers().testFlag(Qt::ShiftModifier)) dragMode_ = DragMode::Measure;
    else if (event->modifiers().testFlag(Qt::AltModifier)
             || event->modifiers().testFlag(Qt::ControlModifier))
    {
      dragMode_ = DragMode::Pan;
      rememberCurrentRange();
      setCursor(Qt::ClosedHandCursor);
    }
    else if (interactionMode_ == PeakMapInteractionMode::Pan)
    {
      dragMode_ = DragMode::Pan;
      rememberCurrentRange();
      setCursor(Qt::ClosedHandCursor);
    }
    else if (interactionMode_ == PeakMapInteractionMode::Measure) dragMode_ = DragMode::Measure;
    else dragMode_ = DragMode::Zoom;
    event->accept();
  }

  void PeakMapWidget::mouseMoveEvent(QMouseEvent* event)
  {
    if (experiment_ && plotRect().contains(event->position().toPoint()))
    {
      const QPointF position = dataAt(event->position());
      emit cursorPositionChanged(position.x(), position.y(),
                                 nearestIntensity(position.x(), position.y()));
    }
    if (dragMode_ == DragMode::None)
    {
      const auto nearest = nearestFeature(event->position());
      const auto nearestId = nearest ? std::optional<std::size_t>{} : nearestIdentification(event->position());
      if (nearest != hoveredFeature_ || nearestId != hoveredIdentification_)
      {
        hoveredFeature_ = nearest;
        hoveredIdentification_ = nearestId;
        if (nearest && *nearest < features_.size())
        {
          const FeatureRecord& feature = features_[*nearest];
          setToolTip(tr("Feature #%1\nRT %2 s · m/z %3\nIntensity %4 · charge %5")
                       .arg(feature.index)
                       .arg(feature.rt, 0, 'f', 2)
                       .arg(feature.mz, 0, 'f', 4)
                       .arg(feature.intensity, 0, 'e', 2)
                       .arg(feature.charge));
          setCursor(Qt::PointingHandCursor);
        }
        else if (nearestId && *nearestId < identifications_.size())
        {
          const IdentificationRecord& identification = identifications_[*nearestId];
          const QString sequence = identification.bestHit() ? identification.bestHit()->sequence : tr("No hit");
          setToolTip(tr("Identification #%1\nRT %2 s · m/z %3\n%4")
                       .arg(identification.index)
                       .arg(identification.rt, 0, 'f', 2)
                       .arg(identification.mz, 0, 'f', 4)
                       .arg(sequence));
          setCursor(Qt::PointingHandCursor);
        }
        else
        {
          setToolTip({});
          updateInteractionCursor();
        }
        update();
      }
      QWidget::mouseMoveEvent(event);
      return;
    }
    dragCurrent_ = event->position().toPoint();
    if (dragMode_ == DragMode::Pan)
    {
      const QPoint delta = dragCurrent_ - dragPrevious_;
      const QRect area = plotRect();
      PlotRange shifted = view_;
      if (axesSwapped_)
      {
        shifted = view_.translated(delta.y() / static_cast<double>(area.height()) * view_.rtSpan(),
                                   -delta.x() / static_cast<double>(area.width()) * view_.mzSpan());
      }
      else
      {
        shifted = view_.translated(-delta.x() / static_cast<double>(area.width()) * view_.rtSpan(),
                                   delta.y() / static_cast<double>(area.height()) * view_.mzSpan());
      }
      view_ = shifted.clampedTo(dataBounds_);
      emit viewRangeChanged(view_);
      scheduleRender();
      dragPrevious_ = dragCurrent_;
    }
    update();
    event->accept();
  }

  void PeakMapWidget::mouseReleaseEvent(QMouseEvent* event)
  {
    if (event->button() != Qt::LeftButton || dragMode_ == DragMode::None)
    {
      QWidget::mouseReleaseEvent(event);
      return;
    }
    dragCurrent_ = event->position().toPoint();
    const DragMode completedMode = dragMode_;
    dragMode_ = DragMode::None;
    updateInteractionCursor();

    if (completedMode == DragMode::Zoom)
    {
      const QRect selection = QRect(dragStart_, dragCurrent_).normalized().intersected(plotRect());
      if (selection.width() >= 8 && selection.height() >= 8)
      {
        const QPointF first = dataAt(selection.topLeft());
        const QPointF second = dataAt(selection.bottomRight());
        applyRange({std::min(first.x(), second.x()), std::max(first.x(), second.x()),
                    std::min(first.y(), second.y()), std::max(first.y(), second.y())}, true);
      }
      else if ((dragCurrent_ - dragStart_).manhattanLength() < 6)
      {
        bool identificationWasActivated = false;
        if (const auto feature = nearestFeature(dragCurrent_))
        {
          selectedFeature_ = feature;
          emit featureActivated(*feature);
        }
        else if (const auto identification = nearestIdentification(dragCurrent_))
        {
          selectedIdentification_ = identification;
          emit identificationActivated(*identification);
          identificationWasActivated = true;
        }
        // Identification activation already navigates to its specifically linked
        // MS/MS spectrum. A generic nearest-RT activation would often replace it
        // with an adjacent MS1 scan.
        if (!identificationWasActivated) emit rtActivated(dataAt(dragCurrent_).x());
      }
    }
    update();
    event->accept();
  }

  void PeakMapWidget::mouseDoubleClickEvent(QMouseEvent* event)
  {
    if (event->button() == Qt::LeftButton && plotRect().contains(event->position().toPoint()))
    {
      resetView();
      event->accept();
      return;
    }
    QWidget::mouseDoubleClickEvent(event);
  }

  void PeakMapWidget::leaveEvent(QEvent* event)
  {
    hoveredFeature_.reset();
    hoveredIdentification_.reset();
    setToolTip({});
    unsetCursor();
    emit cursorLeft();
    update();
    QWidget::leaveEvent(event);
  }

  void PeakMapWidget::keyPressEvent(QKeyEvent* event)
  {
    if (event->key() == Qt::Key_Home)
    {
      resetView();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Z) setInteractionMode(0);
    else if (event->key() == Qt::Key_P) setInteractionMode(1);
    else if (event->key() == Qt::Key_M) setInteractionMode(2);
    else
    {
      QWidget::keyPressEvent(event);
      return;
    }
    event->accept();
  }
}
