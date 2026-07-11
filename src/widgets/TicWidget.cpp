#include "widgets/TicWidget.h"

#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace OpenMSViewer
{
  TicWidget::TicWidget(QWidget* parent) : QWidget(parent)
  {
    setMinimumSize(400, 180);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Total ion or base peak chromatogram"));
    setAccessibleDescription(
      tr("Click a scan, drag or use the wheel to change the RT range, and double-click to reset."));
  }

  void TicWidget::setTrace(std::vector<TicPoint> points, QString label)
  {
    points_ = std::move(points);
    label_ = std::move(label);
    selectedSpectrum_.reset();
    selectedRt_.reset();
    update();
  }

  void TicWidget::setSelectedSpectrum(std::size_t spectrumIndex)
  {
    selectedSpectrum_ = spectrumIndex;
    const auto found = std::find_if(points_.cbegin(), points_.cend(), [spectrumIndex](const TicPoint& point)
    {
      return point.spectrumIndex == spectrumIndex;
    });
    if (found != points_.cend()) selectedRt_ = found->rt;
    update();
  }

  void TicWidget::setSelectedRt(double rt)
  {
    selectedRt_ = rt;
    update();
  }

  const std::optional<double>& TicWidget::selectedRt() const noexcept
  {
    return selectedRt_;
  }

  void TicWidget::setPeakMapRange(const PlotRange& range)
  {
    peakMapRange_ = range;
    hasPeakMapRange_ = true;
    update();
  }

  void TicWidget::clear()
  {
    points_.clear();
    label_.clear();
    selectedSpectrum_.reset();
    selectedRt_.reset();
    hasPeakMapRange_ = false;
    update();
  }

  void TicWidget::resetView()
  {
    if (!points_.empty()) emit rtRangeSelected(points_.front().rt, points_.back().rt);
  }

  QRect TicWidget::plotRect() const
  {
    return rect().adjusted(54, 30, -16, -35);
  }

  std::optional<std::size_t> TicWidget::pointAtX(double x) const
  {
    if (points_.empty()) return std::nullopt;
    const QRect area = plotRect();
    const double fraction = std::clamp((x - area.left()) / static_cast<double>(area.width()), 0.0, 1.0);
    const double targetRt = points_.front().rt + fraction * (points_.back().rt - points_.front().rt);
    auto iterator = std::lower_bound(points_.begin(), points_.end(), targetRt,
      [](const TicPoint& point, double rt) { return point.rt < rt; });
    if (iterator == points_.end()) return points_.size() - 1;
    if (iterator == points_.begin()) return 0;
    const auto previous = iterator - 1;
    return std::abs(iterator->rt - targetRt) < std::abs(previous->rt - targetRt)
             ? static_cast<std::size_t>(std::distance(points_.begin(), iterator))
             : static_cast<std::size_t>(std::distance(points_.begin(), previous));
  }

  void TicWidget::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().window());
    const QRect area = plotRect();
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawLine(area.bottomLeft(), area.bottomRight());
    painter.drawLine(area.bottomLeft(), area.topLeft());
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRect(area.left(), 4, area.width(), 22), Qt::AlignLeft | Qt::AlignVCenter,
                     label_.isEmpty() ? tr("Chromatogram") : label_);

    if (points_.empty())
    {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(area, Qt::AlignCenter, tr("No chromatogram data"));
      return;
    }

    const double rtMin = points_.front().rt;
    double rtMax = points_.back().rt;
    if (rtMax <= rtMin) rtMax = rtMin + 1.0;
    double intensityMax = 0.0;
    for (const auto& point : points_) intensityMax = std::max(intensityMax, point.intensity);
    if (intensityMax <= 0.0) intensityMax = 1.0;
    const auto xForRt = [&](double rt)
    {
      return area.left() + (rt - rtMin) / (rtMax - rtMin) * area.width();
    };
    const auto yForIntensity = [&](double intensity)
    {
      return area.bottom() - intensity / intensityMax * area.height() * 0.95;
    };

    if (hasPeakMapRange_)
    {
      const double left = std::clamp(xForRt(peakMapRange_.rtMin), static_cast<double>(area.left()), static_cast<double>(area.right()));
      const double right = std::clamp(xForRt(peakMapRange_.rtMax), static_cast<double>(area.left()), static_cast<double>(area.right()));
      painter.fillRect(QRectF(QPointF(left, area.top()), QPointF(right, area.bottom())),
                       QColor(255, 210, 40, 30));
    }

    QPainterPath line;
    QPainterPath fill;
    const QPointF first(xForRt(points_.front().rt), yForIntensity(points_.front().intensity));
    line.moveTo(first);
    fill.moveTo(first.x(), area.bottom());
    fill.lineTo(first);
    for (std::size_t index = 1; index < points_.size(); ++index)
    {
      const QPointF point(xForRt(points_[index].rt), yForIntensity(points_[index].intensity));
      line.lineTo(point);
      fill.lineTo(point);
    }
    fill.lineTo(xForRt(points_.back().rt), area.bottom());
    fill.closeSubpath();
    painter.fillPath(fill, QColor(35, 190, 225, 38));
    painter.setPen(QPen(QColor(35, 190, 225), 1.5));
    painter.drawPath(line);

    if (selectedRt_)
    {
      painter.setPen(QPen(QColor(255, 94, 94), 1.5, Qt::DashLine));
      const double x = std::clamp(xForRt(*selectedRt_),
                                  static_cast<double>(area.left()),
                                  static_cast<double>(area.right()));
      painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }

    if (dragging_)
    {
      QRect selection(QPoint(dragStart_.x(), area.top()), QPoint(dragCurrent_.x(), area.bottom()));
      painter.setPen(QPen(QColor(255, 220, 70), 1.2));
      painter.setBrush(QColor(255, 220, 70, 38));
      painter.drawRect(selection.normalized());
    }

    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRect(area.left(), area.bottom() + 10, area.width(), 20), Qt::AlignCenter,
                     tr("Retention time (s)"));
  }

  void TicWidget::mousePressEvent(QMouseEvent* event)
  {
    if (event->button() == Qt::LeftButton && plotRect().contains(event->position().toPoint()))
    {
      dragging_ = true;
      dragStart_ = event->pos();
      dragCurrent_ = dragStart_;
      event->accept();
      update();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void TicWidget::mouseMoveEvent(QMouseEvent* event)
  {
    if (!dragging_) return;
    dragCurrent_ = event->pos();
    update();
  }

  void TicWidget::mouseReleaseEvent(QMouseEvent* event)
  {
    if (!dragging_ || event->button() != Qt::LeftButton) return;
    dragging_ = false;
    dragCurrent_ = event->pos();
    if (std::abs(dragCurrent_.x() - dragStart_.x()) < 6)
    {
      if (const auto index = pointAtX(event->position().x()))
        emit spectrumActivated(points_[*index].spectrumIndex);
      update();
      return;
    }
    if (points_.empty()) return;
    const QRect area = plotRect();
    const double fullMin = points_.front().rt;
    const double fullMax = std::max(fullMin + 1.0, points_.back().rt);
    const double left = std::clamp((std::min(dragStart_.x(), dragCurrent_.x()) - area.left())
      / static_cast<double>(area.width()), 0.0, 1.0);
    const double right = std::clamp((std::max(dragStart_.x(), dragCurrent_.x()) - area.left())
      / static_cast<double>(area.width()), 0.0, 1.0);
    emit rtRangeSelected(fullMin + left * (fullMax - fullMin),
                         fullMin + right * (fullMax - fullMin));
    update();
  }

  void TicWidget::mouseDoubleClickEvent(QMouseEvent*)
  {
    resetView();
  }

  void TicWidget::wheelEvent(QWheelEvent* event)
  {
    if (points_.empty() || !plotRect().contains(event->position().toPoint())) return;
    const double fullMin = points_.front().rt;
    const double fullMax = std::max(fullMin + 1.0, points_.back().rt);
    const double currentMin = hasPeakMapRange_ ? peakMapRange_.rtMin : fullMin;
    const double currentMax = hasPeakMapRange_ ? peakMapRange_.rtMax : fullMax;
    const double fraction = std::clamp((event->position().x() - plotRect().left())
      / plotRect().width(), 0.0, 1.0);
    const double cursor = currentMin + fraction * (currentMax - currentMin);
    const double span = (currentMax - currentMin) * (event->angleDelta().y() > 0 ? 0.8 : 1.25);
    emit rtRangeSelected(std::max(fullMin, cursor - fraction * span),
                         std::min(fullMax, cursor + (1.0 - fraction) * span));
    event->accept();
  }

  void TicWidget::keyPressEvent(QKeyEvent* event)
  {
    if (points_.empty())
    {
      QWidget::keyPressEvent(event);
      return;
    }
    if (event->key() == Qt::Key_Home)
    {
      resetView();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
    {
      std::size_t index = 0;
      if (selectedSpectrum_)
      {
        const auto found = std::find_if(points_.cbegin(), points_.cend(), [this](const TicPoint& point)
        {
          return point.spectrumIndex == *selectedSpectrum_;
        });
        if (found != points_.cend())
          index = static_cast<std::size_t>(std::distance(points_.cbegin(), found));
      }
      if (event->key() == Qt::Key_Left && index > 0) --index;
      else if (event->key() == Qt::Key_Right && index + 1 < points_.size()) ++index;
      emit spectrumActivated(points_[index].spectrumIndex);
      event->accept();
      return;
    }
    QWidget::keyPressEvent(event);
  }
}
