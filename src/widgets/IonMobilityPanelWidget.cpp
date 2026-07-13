#include "widgets/IonMobilityPanelWidget.h"

#include "model/TraceSmoothing.h"
#include "plot/PlotAxis.h"
#include "plot/PlotTheme.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace OpenMSViewer
{
  IonMobilityPlotWidget::IonMobilityPlotWidget(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("ionMobilityPlot"));
    setMinimumSize(520, 300);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Ion-mobility frame"));
    setAccessibleDescription(tr("Interactive m/z and ion-mobility map with a linked mobilogram."));
    renderTimer_.setSingleShot(true);
    renderTimer_.setInterval(35);
    connect(&renderTimer_, &QTimer::timeout, this, &IonMobilityPlotWidget::startRender);
    connect(&renderWatcher_, &QFutureWatcher<IonMobilityRaster>::finished, this, [this]
    {
      if (activeGeneration_ == desiredGeneration_)
      {
        raster_ = renderWatcher_.result();
        emit renderCompleted();
        update();
      }
      else if (framePosition_)
      {
        renderTimer_.start(0);
      }
    });
  }

  IonMobilityPlotWidget::~IonMobilityPlotWidget()
  {
    if (renderWatcher_.isRunning()) renderWatcher_.waitForFinished();
  }

  void IonMobilityPlotWidget::setData(
    std::shared_ptr<const OpenMS::MSExperiment> experiment,
    const std::vector<IonMobilityFrameRecord>& frames)
  {
    experiment_ = std::move(experiment);
    frames_ = frames;
    framePosition_.reset();
    raster_ = {};
    rebuildDiaWindows();
    ++desiredGeneration_;
    update();
  }

  void IonMobilityPlotWidget::setShowDiaWindows(bool show)
  {
    if (showDiaWindows_ == show) return;
    showDiaWindows_ = show;
    update();
  }

  void IonMobilityPlotWidget::rebuildDiaWindows()
  {
    // Reconstruct the diaPASEF isolation windows (m/z × ion-mobility boxes) from the
    // MS2 frames. The m/z window is exact (precursor isolation offsets), but the
    // ion-mobility range here is the OBSERVED peak-array extent (not the method's
    // acquisition limits), so it jitters cycle to cycle. Key each window on its exact
    // m/z window + a coarse IM-MIDPOINT bucket (the midpoint is far steadier than the
    // extrema), and AGGREGATE the observed extents across cycles into one box — so
    // repeated cycles collapse instead of inflating the count, while distinct IM
    // windows at the same m/z stay separate. Frames are RT-sorted, so a window's first
    // occurrence is in cycle 1; windows first seen at the same frame RT (one TIMS
    // frame) share a "window group" (and colour).
    diaWindows_.clear();
    constexpr std::size_t kMaxWindows = 2048;  // guard against non-DIA / pathological input
    std::map<std::tuple<long long, long long, long long>, std::size_t> keyToIndex;
    std::map<long long, int> groupOfRt;
    for (const IonMobilityFrameRecord& frame : frames_)
    {
      if (frame.msLevel <= 1 || !frame.isolationWindowLower || !frame.isolationWindowUpper) continue;
      const double lower = *frame.isolationWindowLower;
      const double upper = *frame.isolationWindowUpper;
      if (!std::isfinite(lower) || !std::isfinite(upper) || !std::isfinite(frame.mobilityMin)
          || !std::isfinite(frame.mobilityMax)) continue;
      const auto q = [](double value, double scale) { return llround(value * scale); };
      const double mobilityMid = (frame.mobilityMin + frame.mobilityMax) / 2.0;
      const std::tuple key{q(lower, 100.0), q(upper, 100.0), q(mobilityMid, 20.0)};  // IM bucket 0.05
      const auto it = keyToIndex.find(key);
      if (it == keyToIndex.end())
      {
        if (diaWindows_.size() >= kMaxWindows) break;
        const int group = groupOfRt.try_emplace(q(frame.rt, 100.0),
                                                static_cast<int>(groupOfRt.size())).first->second;
        keyToIndex.emplace(key, diaWindows_.size());
        diaWindows_.push_back({lower, upper, frame.mobilityMin, frame.mobilityMax, group});
      }
      else
      {
        DiaWindow& window = diaWindows_[it->second];  // union the observed extents
        window.mzLow = std::min(window.mzLow, lower);
        window.mzHigh = std::max(window.mzHigh, upper);
        window.mobilityLow = std::min(window.mobilityLow, frame.mobilityMin);
        window.mobilityHigh = std::max(window.mobilityHigh, frame.mobilityMax);
      }
    }
  }

  void IonMobilityPlotWidget::drawDiaWindows(QPainter& painter, const QRect& area) const
  {
    // Needs a selected frame (so view_ is initialized) and a non-degenerate view,
    // else the m/z / mobility mappings below would divide by a zero span.
    if (!showDiaWindows_ || diaWindows_.empty() || !framePosition_) return;
    if (!(view_.mzSpan() > 0.0) || !(view_.mobilitySpan() > 0.0)) return;
    // Distinct, stable colours per window group.
    static const std::array<QColor, 10> kGroupColors{
      QColor(52, 152, 219), QColor(231, 76, 60), QColor(46, 204, 113), QColor(155, 89, 182),
      QColor(241, 196, 15), QColor(26, 188, 156), QColor(230, 126, 34), QColor(52, 73, 94),
      QColor(214, 90, 190), QColor(120, 144, 156)};
    const auto xForMz = [&](double mz)
    { return area.left() + (mz - view_.mzMin) / view_.mzSpan() * area.width(); };
    const auto yForMobility = [&](double mobility)
    { return area.bottom() - (mobility - view_.mobilityMin) / view_.mobilitySpan() * area.height(); };

    painter.save();
    painter.setClipRect(area);
    for (const DiaWindow& window : diaWindows_)
    {
      // Cull windows entirely outside the current view.
      if (window.mzHigh < view_.mzMin || window.mzLow > view_.mzMax
          || window.mobilityHigh < view_.mobilityMin || window.mobilityLow > view_.mobilityMax)
        continue;
      const QRectF box(QPointF(xForMz(window.mzLow), yForMobility(window.mobilityHigh)),
                       QPointF(xForMz(window.mzHigh), yForMobility(window.mobilityLow)));
      QColor color = kGroupColors[static_cast<std::size_t>(window.group) % kGroupColors.size()];
      QColor fill = color;
      fill.setAlpha(40);
      painter.setPen(QPen(color, 1.2));
      painter.setBrush(fill);
      painter.drawRect(box.normalized());
    }
    painter.restore();
  }

  void IonMobilityPlotWidget::setFramePosition(std::optional<std::size_t> position)
  {
    if (position && *position >= frames_.size()) position.reset();
    framePosition_ = position;
    raster_ = {};
    if (position)
    {
      const auto& frame = frames_[*position];
      bounds_ = {frame.mzMin, frame.mzMax, frame.mobilityMin, frame.mobilityMax};
      if (bounds_.mzMax <= bounds_.mzMin) bounds_.mzMax = bounds_.mzMin + 1.0;
      if (bounds_.mobilityMax <= bounds_.mobilityMin)
        bounds_.mobilityMax = bounds_.mobilityMin + 1.0;
      view_ = bounds_;
      emit viewRangeChanged(view_);
    }
    scheduleRender();
  }

  void IonMobilityPlotWidget::setShowMobilogram(bool show)
  {
    if (showMobilogram_ == show) return;
    showMobilogram_ = show;
    scheduleRender();
  }

  void IonMobilityPlotWidget::setSmoothMobilogram(bool smooth)
  {
    if (smoothMobilogram_ == smooth) return;
    smoothMobilogram_ = smooth;
    update();  // presentation-only: no re-render needed
  }

  void IonMobilityPlotWidget::setColorMap(PeakMapColorMap colorMap)
  {
    if (colorMap_ == colorMap) return;
    colorMap_ = colorMap;
    raster_.image = QImage();   // don't show the old-colormap frame while re-rendering
    scheduleRender();
    update();  // repaint the floor background immediately, even before the re-render lands
  }

  void IonMobilityPlotWidget::resetView()
  {
    if (!framePosition_) return;
    view_ = bounds_;
    emit viewRangeChanged(view_);
    scheduleRender();
  }

  void IonMobilityPlotWidget::setMzRange(double minimumMz, double maximumMz, bool reset)
  {
    if (!framePosition_) return;
    IonMobilityRange next = view_;
    if (reset)
    {
      next.mzMin = bounds_.mzMin;
      next.mzMax = bounds_.mzMax;
    }
    else
    {
      next.mzMin = minimumMz;
      next.mzMax = maximumMz;
    }
    applyRange(next);
  }

  std::optional<std::size_t> IonMobilityPlotWidget::framePosition() const noexcept
  {
    return framePosition_;
  }

  const IonMobilityRange& IonMobilityPlotWidget::viewRange() const noexcept { return view_; }
  const IonMobilityRaster& IonMobilityPlotWidget::raster() const noexcept { return raster_; }

  QRect IonMobilityPlotWidget::mainPlotRect() const
  {
    const int right = showMobilogram_ ? 178 : 22;
    return rect().adjusted(74, 24, -right, -48);
  }

  QRect IonMobilityPlotWidget::mobilogramRect() const
  {
    const QRect main = mainPlotRect();
    return {main.right() + 17, main.top(), 132, main.height()};
  }

  void IonMobilityPlotWidget::scheduleRender()
  {
    ++desiredGeneration_;
    if (!experiment_ || !framePosition_)
    {
      renderTimer_.stop();
      raster_ = {};
      update();
      return;
    }
    renderTimer_.start();
    update();
  }

  void IonMobilityPlotWidget::startRender()
  {
    if (!experiment_ || !framePosition_ || renderWatcher_.isRunning()) return;
    const auto& frame = frames_[*framePosition_];
    if (frame.spectrumIndex >= experiment_->size()) return;
    const QSize size = mainPlotRect().size();
    if (size.width() <= 0 || size.height() <= 0) return;
    activeGeneration_ = desiredGeneration_;
    const auto experiment = experiment_;
    const std::size_t spectrumIndex = frame.spectrumIndex;
    const IonMobilityRange range = view_;
    const PeakMapColorMap colorMap = colorMap_;
    renderWatcher_.setFuture(QtConcurrent::run([experiment, spectrumIndex, range, size, colorMap]
    {
      return IonMobilityRasterizer::render((*experiment)[spectrumIndex], range, size, colorMap);
    }));
  }

  void IonMobilityPlotWidget::applyRange(const IonMobilityRange& rawRange)
  {
    if (!framePosition_) return;
    IonMobilityRange next = rawRange.clampedTo(bounds_);
    if (!next.isValid()
        || next.mzSpan() < bounds_.mzSpan() * 1e-7
        || next.mobilitySpan() < bounds_.mobilitySpan() * 1e-7) return;
    view_ = next;
    emit viewRangeChanged(view_);
    scheduleRender();
  }

  void IonMobilityPlotWidget::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    const QRect area = mainPlotRect();
    painter.fillRect(area, QColor::fromRgb(PeakMapRasterizer::color(0.0, colorMap_)));
    if (!raster_.image.isNull()) painter.drawImage(area, raster_.image);
    drawAxes(painter, area);
    drawDiaWindows(painter, area);

    if (!framePosition_)
    {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(area, Qt::AlignCenter, tr("Select an ion-mobility frame"));
      return;
    }

    const auto& frame = frames_[*framePosition_];
    if (frame.isolationWindowLower && frame.isolationWindowUpper)
    {
      const double lower = std::max(view_.mzMin, *frame.isolationWindowLower);
      const double upper = std::min(view_.mzMax, *frame.isolationWindowUpper);
      if (upper >= lower)
      {
        const double x1 = area.left() + (lower - view_.mzMin) / view_.mzSpan() * area.width();
        const double x2 = area.left() + (upper - view_.mzMin) / view_.mzSpan() * area.width();
        painter.fillRect(QRectF(QPointF(x1, area.top()), QPointF(x2, area.bottom())),
                         QColor(255, 153, 51, 52));
        painter.setPen(QPen(QColor(255, 164, 65), 1.2, Qt::DashLine));
        painter.drawLine(QPointF(x1, area.top()), QPointF(x1, area.bottom()));
        painter.drawLine(QPointF(x2, area.top()), QPointF(x2, area.bottom()));
      }
    }
    if (showMobilogram_) drawMobilogram(painter, mobilogramRect());

    if (dragging_)
    {
      QRect selection(dragStart_, dragCurrent_);
      selection = selection.normalized();
      painter.setPen(QPen(draggingMobilogram_ ? QColor(0, 205, 255) : QColor(255, 220, 70), 1.2));
      painter.setBrush(draggingMobilogram_ ? QColor(0, 205, 255, 42) : QColor(255, 220, 70, 38));
      if (draggingMobilogram_)
      {
        selection.setLeft(mobilogramRect().left());
        selection.setRight(mobilogramRect().right());
      }
      painter.drawRect(selection);
    }

    if (renderWatcher_.isRunning() || renderTimer_.isActive())
    {
      painter.setPen(Qt::white);
      painter.setBrush(QColor(0, 0, 0, 155));
      const QRect badge(area.right() - 92, area.top() + 8, 82, 25);
      painter.drawRoundedRect(badge, 5, 5);
      painter.drawText(badge, Qt::AlignCenter, tr("Rendering…"));
    }
  }

  void IonMobilityPlotWidget::drawAxes(QPainter& painter, const QRect& area)
  {
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(area);

    // Round "nice" ticks + gridlines on both axes (m/z across, mobility up),
    // instead of arbitrary evenly-spaced fractions of the view.
    const auto mzTicks = PlotAxis::niceTicks(view_.mzMin, view_.mzMax, 6);
    const auto mobilityTicks = PlotAxis::niceTicks(view_.mobilityMin, view_.mobilityMax, 5);
    const auto xForMz = [&](double mz)
    {
      return area.left() + (mz - view_.mzMin) / view_.mzSpan() * area.width();
    };
    const auto yForMobility = [&](double mobility)
    {
      return area.bottom() - (mobility - view_.mobilityMin) / view_.mobilitySpan() * area.height();
    };
    const int mzDecimals = view_.mzSpan() < 10.0 ? 2 : 1;
    const double mobilityStep = mobilityTicks.size() > 1
      ? std::abs(mobilityTicks[1] - mobilityTicks[0]) : 0.1;
    const int mobilityDecimals = std::clamp(static_cast<int>(std::ceil(-std::log10(mobilityStep))), 0, 4);

    painter.setPen(QPen(palette().color(QPalette::Mid), 1.0, Qt::DotLine));
    for (const double mz : mzTicks)
    {
      const double x = xForMz(mz);
      if (x >= area.left() && x <= area.right())
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    for (const double mobility : mobilityTicks)
    {
      const double y = yForMobility(mobility);
      if (y >= area.top() && y <= area.bottom())
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    painter.setPen(palette().color(QPalette::Text));
    for (const double mz : mzTicks)
    {
      const double x = xForMz(mz);
      if (x < area.left() - 0.5 || x > area.right() + 0.5) continue;
      painter.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + 4));
      painter.drawText(QRectF(x - 38, area.bottom() + 5, 76, 16), Qt::AlignHCenter | Qt::AlignTop,
                       QString::number(mz, 'f', mzDecimals));
    }
    for (const double mobility : mobilityTicks)
    {
      const double y = yForMobility(mobility);
      if (y < area.top() - 0.5 || y > area.bottom() + 0.5) continue;
      painter.drawLine(QPointF(area.left() - 4, y), QPointF(area.left(), y));
      painter.drawText(QRectF(0, y - 9, area.left() - 8.0, 18), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(mobility, 'f', mobilityDecimals));
    }
    painter.drawText(QRect(area.left(), height() - 24, area.width(), 20), Qt::AlignCenter,
                     QStringLiteral("m/z"));
    painter.save();
    painter.translate(18, area.center().y());
    painter.rotate(-90.0);
    const QString unit = framePosition_ ? frames_[*framePosition_].unit : QString();
    const QString title = unit.isEmpty() || unit == QStringLiteral("none")
      ? tr("Ion mobility") : tr("Ion mobility (%1)").arg(unit);
    painter.drawText(QRect(-area.height() / 2, -10, area.height(), 20), Qt::AlignCenter, title);
    painter.restore();
  }

  void IonMobilityPlotWidget::drawMobilogram(QPainter& painter, const QRect& area)
  {
    painter.fillRect(area, palette().color(QPalette::Base));
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(area);
    if (raster_.mobilogram.empty()) return;
    const float maximum = *std::max_element(raster_.mobilogram.begin(), raster_.mobilogram.end());
    if (maximum <= 0.0F) return;

    const QColor trace = PlotTheme::primaryTrace(palette());
    const auto withAlpha = [](QColor colour, int alpha) { colour.setAlpha(alpha); return colour; };

    // Intensity values to draw as the profile: raw, or a Savitzky-Golay smoothing.
    // Both are scaled by the RAW maximum so the smoothed profile stays comparable.
    std::vector<double> raw(raster_.mobilogram.begin(), raster_.mobilogram.end());
    const std::vector<double> smoothed = smoothMobilogram_
      ? TraceSmoothing::savitzkyGolay(raw) : std::vector<double>{};

    const auto buildPath = [&](const std::vector<double>& values, bool close)
    {
      QPainterPath path;
      if (close) path.moveTo(area.left(), area.bottom());
      for (std::size_t index = 0; index < values.size(); ++index)
      {
        const double fraction = values.size() == 1 ? 0.0
          : static_cast<double>(index) / static_cast<double>(values.size() - 1);
        const double x = area.left() + values[index] / maximum * area.width();
        const double y = area.bottom() - fraction * area.height();
        if (index == 0 && !close) path.moveTo(x, y); else path.lineTo(x, y);
      }
      if (close) { path.lineTo(area.left(), area.top()); path.closeSubpath(); }
      return path;
    };

    const bool haveSmoothed = smoothMobilogram_ && smoothed.size() == raw.size();
    // Clip to the mobilogram rect: Savitzky-Golay can overshoot above the raw max on
    // sharp edges, which would otherwise spill the fill/line past the right border.
    painter.save();
    painter.setClipRect(area);
    // When smoothing, draw the raw profile faintly underneath the smoothed one.
    if (haveSmoothed)
    {
      painter.setPen(QPen(withAlpha(trace, 70), 1.0));
      painter.drawPath(buildPath(raw, false));
    }
    const std::vector<double>& profile = haveSmoothed ? smoothed : raw;
    painter.fillPath(buildPath(profile, true), withAlpha(trace, 55));
    painter.setPen(QPen(trace, 1.7));
    painter.drawPath(buildPath(profile, false));
    painter.restore();
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRect(area.left(), 3, area.width(), 18), Qt::AlignCenter, tr("Mobilogram"));
  }

  void IonMobilityPlotWidget::resizeEvent(QResizeEvent* event)
  {
    QWidget::resizeEvent(event);
    scheduleRender();
  }

  void IonMobilityPlotWidget::wheelEvent(QWheelEvent* event)
  {
    if (!framePosition_) return;
    const QRect main = mainPlotRect();
    const QRect mobilogram = mobilogramRect();
    const QPointF position = event->position();
    const double factor = event->angleDelta().y() > 0 ? 0.8 : 1.25;
    IonMobilityRange next = view_;
    if (showMobilogram_ && mobilogram.contains(position.toPoint()))
    {
      const double y = std::clamp((position.y() - mobilogram.top()) / mobilogram.height(), 0.0, 1.0);
      const double cursor = view_.mobilityMax - y * view_.mobilitySpan();
      const double span = view_.mobilitySpan() * factor;
      next.mobilityMin = cursor - (1.0 - y) * span;
      next.mobilityMax = cursor + y * span;
    }
    else if (main.contains(position.toPoint()))
    {
      const double x = std::clamp((position.x() - main.left()) / main.width(), 0.0, 1.0);
      const double y = std::clamp((position.y() - main.top()) / main.height(), 0.0, 1.0);
      const double mz = view_.mzMin + x * view_.mzSpan();
      const double mobility = view_.mobilityMax - y * view_.mobilitySpan();
      const double mzSpan = view_.mzSpan() * factor;
      const double mobilitySpan = view_.mobilitySpan() * factor;
      next.mzMin = mz - x * mzSpan;
      next.mzMax = mz + (1.0 - x) * mzSpan;
      next.mobilityMin = mobility - (1.0 - y) * mobilitySpan;
      next.mobilityMax = mobility + y * mobilitySpan;
    }
    else return;
    applyRange(next);
    event->accept();
  }

  void IonMobilityPlotWidget::mousePressEvent(QMouseEvent* event)
  {
    if (event->button() != Qt::LeftButton || !framePosition_) return;
    const bool inMain = mainPlotRect().contains(event->pos());
    const bool inMobilogram = showMobilogram_ && mobilogramRect().contains(event->pos());
    if (!inMain && !inMobilogram) return;
    dragging_ = true;
    draggingMobilogram_ = inMobilogram;
    dragStart_ = event->pos();
    dragCurrent_ = dragStart_;
    update();
  }

  void IonMobilityPlotWidget::mouseMoveEvent(QMouseEvent* event)
  {
    if (!dragging_) return;
    dragCurrent_ = event->pos();
    update();
  }

  void IonMobilityPlotWidget::mouseReleaseEvent(QMouseEvent* event)
  {
    if (!dragging_ || event->button() != Qt::LeftButton) return;
    dragging_ = false;
    dragCurrent_ = event->pos();
    const int dx = std::abs(dragCurrent_.x() - dragStart_.x());
    const int dy = std::abs(dragCurrent_.y() - dragStart_.y());
    if ((draggingMobilogram_ && dy < 4) || (!draggingMobilogram_ && dx < 5 && dy < 5))
    {
      update();
      return;
    }
    IonMobilityRange next = view_;
    if (draggingMobilogram_)
    {
      const QRect area = mobilogramRect();
      const double top = std::clamp((std::min(dragStart_.y(), dragCurrent_.y()) - area.top())
                                      / static_cast<double>(area.height()), 0.0, 1.0);
      const double bottom = std::clamp((std::max(dragStart_.y(), dragCurrent_.y()) - area.top())
                                         / static_cast<double>(area.height()), 0.0, 1.0);
      next.mobilityMax = view_.mobilityMax - top * view_.mobilitySpan();
      next.mobilityMin = view_.mobilityMax - bottom * view_.mobilitySpan();
    }
    else
    {
      const QRect area = mainPlotRect();
      const double left = std::clamp((std::min(dragStart_.x(), dragCurrent_.x()) - area.left())
                                       / static_cast<double>(area.width()), 0.0, 1.0);
      const double right = std::clamp((std::max(dragStart_.x(), dragCurrent_.x()) - area.left())
                                        / static_cast<double>(area.width()), 0.0, 1.0);
      const double top = std::clamp((std::min(dragStart_.y(), dragCurrent_.y()) - area.top())
                                      / static_cast<double>(area.height()), 0.0, 1.0);
      const double bottom = std::clamp((std::max(dragStart_.y(), dragCurrent_.y()) - area.top())
                                         / static_cast<double>(area.height()), 0.0, 1.0);
      next.mzMin = view_.mzMin + left * view_.mzSpan();
      next.mzMax = view_.mzMin + right * view_.mzSpan();
      next.mobilityMax = view_.mobilityMax - top * view_.mobilitySpan();
      next.mobilityMin = view_.mobilityMax - bottom * view_.mobilitySpan();
    }
    applyRange(next);
  }

  void IonMobilityPlotWidget::mouseDoubleClickEvent(QMouseEvent*) { resetView(); }

  IonMobilityPanelWidget::IonMobilityPanelWidget(QWidget* parent) : QWidget(parent)
  {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* controls = new QHBoxLayout;
    info_ = new QLabel(tr("No ion-mobility data"), this);
    info_->setObjectName(QStringLiteral("ionMobilityInfo"));
    controls->addWidget(info_);
    controls->addStretch();
    previous_ = new QPushButton(QStringLiteral("<"), this);
    previous_->setToolTip(tr("Previous ion-mobility frame"));
    controls->addWidget(previous_);
    frameSelector_ = new QComboBox(this);
    frameSelector_->setObjectName(QStringLiteral("ionMobilityFrameSelector"));
    frameSelector_->setMinimumWidth(250);
    controls->addWidget(frameSelector_);
    next_ = new QPushButton(QStringLiteral(">"), this);
    next_->setToolTip(tr("Next ion-mobility frame"));
    controls->addWidget(next_);
    mobilogram_ = new QCheckBox(tr("Mobilogram"), this);
    mobilogram_->setObjectName(QStringLiteral("ionMobilityMobilogram"));
    mobilogram_->setChecked(true);
    controls->addWidget(mobilogram_);
    auto* smoothMobilogram = new QCheckBox(tr("Smooth"), this);
    smoothMobilogram->setObjectName(QStringLiteral("ionMobilitySmooth"));
    smoothMobilogram->setToolTip(tr("Overlay a Savitzky-Golay lowpass of the mobilogram"));
    controls->addWidget(smoothMobilogram);
    auto* diaWindows = new QCheckBox(tr("DIA windows"), this);
    diaWindows->setObjectName(QStringLiteral("ionMobilityDiaWindows"));
    diaWindows->setToolTip(tr("Overlay the diaPASEF isolation window groups (m/z × ion mobility)"));
    controls->addWidget(diaWindows);
    linkMz_ = new QCheckBox(tr("Link spectrum m/z"), this);
    linkMz_->setObjectName(QStringLiteral("ionMobilityLinkMz"));
    controls->addWidget(linkMz_);
    auto* reset = new QPushButton(tr("Reset view"), this);
    controls->addWidget(reset);
    layout->addLayout(controls);
    range_ = new QLabel(tr("m/z: — · IM: —"), this);
    range_->setObjectName(QStringLiteral("ionMobilityRange"));
    layout->addWidget(range_);
    plot_ = new IonMobilityPlotWidget(this);
    layout->addWidget(plot_, 1);

    connect(frameSelector_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int position) { selectPosition(position, true); });
    connect(previous_, &QPushButton::clicked, this, [this]
    {
      if (frameSelector_->currentIndex() > 0)
        frameSelector_->setCurrentIndex(frameSelector_->currentIndex() - 1);
    });
    connect(next_, &QPushButton::clicked, this, [this]
    {
      if (frameSelector_->currentIndex() + 1 < frameSelector_->count())
        frameSelector_->setCurrentIndex(frameSelector_->currentIndex() + 1);
    });
    connect(mobilogram_, &QCheckBox::toggled, plot_, &IonMobilityPlotWidget::setShowMobilogram);
    connect(smoothMobilogram, &QCheckBox::toggled, plot_, &IonMobilityPlotWidget::setSmoothMobilogram);
    connect(diaWindows, &QCheckBox::toggled, plot_, &IonMobilityPlotWidget::setShowDiaWindows);
    connect(linkMz_, &QCheckBox::toggled, this, [this](bool linked)
    {
      if (!linked) return;
      if (spectrumMzRange_)
        plot_->setMzRange(spectrumMzRange_->first, spectrumMzRange_->second, false);
    });
    connect(reset, &QPushButton::clicked, plot_, &IonMobilityPlotWidget::resetView);
    connect(plot_, &IonMobilityPlotWidget::viewRangeChanged, this,
            [this](const IonMobilityRange& view)
    {
      const QString unit = selectedSpectrumIndex()
        ? frames_[static_cast<std::size_t>(frameSelector_->currentIndex())].unit : QString();
      range_->setText(tr("m/z: %1–%2 · IM: %3–%4 %5")
        .arg(view.mzMin, 0, 'f', 2).arg(view.mzMax, 0, 'f', 2)
        .arg(view.mobilityMin, 0, 'f', 3).arg(view.mobilityMax, 0, 'f', 3).arg(unit));
    });
  }

  void IonMobilityPanelWidget::setData(
    std::shared_ptr<const OpenMS::MSExperiment> experiment,
    const std::vector<IonMobilityFrameRecord>& frames)
  {
    experiment_ = std::move(experiment);
    frames_ = frames;
    plot_->setData(experiment_, frames_);
    const QSignalBlocker blocker(frameSelector_);
    frameSelector_->clear();
    for (const auto& frame : frames_)
    {
      QString label = tr("MS%1 · #%2 · RT %3 s")
        .arg(frame.msLevel).arg(frame.spectrumIndex).arg(frame.rt, 0, 'f', 2);
      if (frame.precursorMz) label += tr(" · %1 m/z").arg(*frame.precursorMz, 0, 'f', 4);
      frameSelector_->addItem(label, QVariant::fromValue<qulonglong>(frame.spectrumIndex));
    }
    int first = -1;
    for (std::size_t position = 0; position < frames_.size(); ++position)
    {
      if (frames_[position].msLevel == 1)
      {
        first = static_cast<int>(position);
        break;
      }
    }
    if (first < 0 && !frames_.empty()) first = 0;
    frameSelector_->setCurrentIndex(first);
    selectPosition(first, false);
  }

  void IonMobilityPanelWidget::clear()
  {
    setData({}, {});
  }

  void IonMobilityPanelWidget::setSpectrumIndex(std::size_t spectrumIndex)
  {
    const auto found = std::find_if(frames_.begin(), frames_.end(), [spectrumIndex](const auto& frame)
    {
      return frame.spectrumIndex == spectrumIndex;
    });
    const int position = found == frames_.end() ? -1
      : static_cast<int>(std::distance(frames_.begin(), found));
    const QSignalBlocker blocker(frameSelector_);
    frameSelector_->setCurrentIndex(position);
    selectPosition(position, false);
    if (position >= 0 && linkMz_->isChecked() && spectrumMzRange_)
      plot_->setMzRange(spectrumMzRange_->first, spectrumMzRange_->second, false);
  }

  void IonMobilityPanelWidget::setSpectrumMzRange(double minimumMz, double maximumMz, bool reset)
  {
    spectrumMzRange_ = std::pair{minimumMz, maximumMz};
    if (linkMz_->isChecked()) plot_->setMzRange(minimumMz, maximumMz, reset);
  }

  void IonMobilityPanelWidget::setPeakMapMzRange(double minimumMz, double maximumMz)
  {
    // The ion-mobility map tracks the peak map's m/z viewport so the visible
    // region stays consistent across panels.
    plot_->setMzRange(minimumMz, maximumMz);
  }

  void IonMobilityPanelWidget::setColorMap(int colorMapIndex)
  {
    // Follows the main peak map's colormap selector (same PeakMapColorMap order).
    plot_->setColorMap(static_cast<PeakMapColorMap>(std::clamp(colorMapIndex, 0, 6)));
  }

  std::size_t IonMobilityPanelWidget::frameCount() const noexcept { return frames_.size(); }

  std::optional<std::size_t> IonMobilityPanelWidget::selectedSpectrumIndex() const noexcept
  {
    const int position = frameSelector_->currentIndex();
    if (position < 0 || static_cast<std::size_t>(position) >= frames_.size()) return std::nullopt;
    return frames_[static_cast<std::size_t>(position)].spectrumIndex;
  }

  IonMobilityPlotWidget* IonMobilityPanelWidget::plot() const noexcept { return plot_; }

  void IonMobilityPanelWidget::selectPosition(int position, bool activateSpectrum)
  {
    if (position < 0 || static_cast<std::size_t>(position) >= frames_.size())
    {
      plot_->setFramePosition(std::nullopt);
      previous_->setEnabled(false);
      next_->setEnabled(false);
      updateInfo();
      return;
    }
    plot_->setFramePosition(static_cast<std::size_t>(position));
    previous_->setEnabled(position > 0);
    next_->setEnabled(position + 1 < static_cast<int>(frames_.size()));
    updateInfo();
    if (activateSpectrum) emit spectrumActivated(frames_[static_cast<std::size_t>(position)].spectrumIndex);
  }

  void IonMobilityPanelWidget::updateInfo()
  {
    const int position = frameSelector_->currentIndex();
    if (position < 0 || static_cast<std::size_t>(position) >= frames_.size())
    {
      info_->setText(tr("No ion-mobility data for this spectrum"));
      range_->setText(tr("m/z: — · IM: —"));
      return;
    }
    const auto& frame = frames_[static_cast<std::size_t>(position)];
    QString text = tr("MS%1 frame #%2 · RT %3 s")
      .arg(frame.msLevel).arg(frame.spectrumIndex).arg(frame.rt, 0, 'f', 2);
    if (frame.isolationWindowLower && frame.isolationWindowUpper)
      text += tr(" · isolation %1–%2 m/z")
        .arg(*frame.isolationWindowLower, 0, 'f', 2)
        .arg(*frame.isolationWindowUpper, 0, 'f', 2);
    info_->setText(text);
  }
}
