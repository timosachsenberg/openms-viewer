#include "widgets/ConsensusQuantChart.h"

#include "plot/PlotAxis.h"

#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <map>

namespace OpenMSViewer
{
  namespace
  {
    constexpr int kBarThreshold = 24;  // more columns → compact dot strip

    double scaled(double intensity, bool logScale)
    {
      if (!(intensity > 0.0)) return 0.0;
      return logScale ? std::log10(1.0 + intensity) : intensity;
    }

    QString columnLabel(const ConsensusColumn& column)
    {
      if (!column.label.isEmpty()) return column.label;
      if (!column.filename.isEmpty()) return QFileInfo(column.filename).fileName();
      return QStringLiteral("map %1").arg(column.mapIndex);
    }
  }

  ConsensusQuantChart::ConsensusQuantChart(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("consensusQuantChart"));
    setMinimumHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(tr("Per-map intensity"));
    // Hover to identify a column: the dot strip (many maps) drops the rotated
    // axis labels, so a tooltip is the only way to read which run a slot is.
    setMouseTracking(true);
  }

  void ConsensusQuantChart::setData(std::vector<ConsensusHandle> handles,
                                    std::vector<ConsensusColumn> columns)
  {
    handles_ = std::move(handles);
    columns_ = std::move(columns);
    update();
  }

  void ConsensusQuantChart::setLogScale(bool logScale)
  {
    if (logScale_ == logScale) return;
    logScale_ = logScale;
    update();
  }

  void ConsensusQuantChart::clear()
  {
    handles_.clear();
    columns_.clear();
    update();
  }

  QRectF ConsensusQuantChart::plotRect() const
  {
    return QRectF(rect()).adjusted(60.0, 10.0, -10.0, -40.0);
  }

  void ConsensusQuantChart::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    const QRectF area = plotRect();
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(area);

    if (columns_.empty())
    {
      painter.setPen(palette().color(QPalette::Text));
      painter.drawText(area, Qt::AlignCenter, tr("Select a consensus feature"));
      return;
    }

    // Sum handle intensities per input map (multiple handles per map are legal);
    // ignore non-finite samples so they can't poison the axis or coordinates.
    std::map<std::int64_t, double> perMap;
    for (const ConsensusHandle& handle : handles_)
      if (std::isfinite(handle.intensity)) perMap[handle.mapIndex] += handle.intensity;
    double maximum = 0.0;
    for (const auto& [mapIndex, intensity] : perMap)
      if (std::isfinite(intensity)) maximum = std::max(maximum, intensity);
    const double axisMax = scaled(maximum, logScale_);

    const auto mapY = [&](double intensity)
    {
      if (!(axisMax > 0.0)) return area.bottom();
      return area.bottom() - scaled(intensity, logScale_) / axisMax * area.height() * 0.95;
    };

    // Intensity gridlines + labels. Nice ticks are chosen on the raw INTENSITY
    // domain (so labels are round — 0/500k/1M) and forward-transformed to y via
    // mapY, which applies the log/linear scaling; picking ticks in log space would
    // give arbitrary, sparse labels.
    if (maximum > 0.0)
    {
      const auto ticks = PlotAxis::niceTicks(0.0, maximum, 4);
      for (const double tick : ticks)
      {
        const double y = mapY(tick);
        if (y < area.top() - 0.5 || y > area.bottom() + 0.5) continue;
        painter.setPen(QPen(palette().color(QPalette::Mid), 1.0, Qt::DotLine));
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRectF(0.0, y - 8.0, area.left() - 4.0, 16.0),
                         Qt::AlignRight | Qt::AlignVCenter, QString::number(tick, 'g', 3));
      }
    }

    const int columnCount = static_cast<int>(columns_.size());
    const double slot = area.width() / columnCount;
    const bool bars = columnCount <= kBarThreshold;
    const bool showLabels = columnCount <= kBarThreshold;

    for (int index = 0; index < columnCount; ++index)
    {
      const ConsensusColumn& column = columns_[static_cast<std::size_t>(index)];
      const double centre = area.left() + (index + 0.5) * slot;
      const auto it = perMap.find(column.mapIndex);
      const bool present = it != perMap.end();

      if (!present)
      {
        // Missing map: a small hollow marker at the baseline — never a zero bar.
        painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(centre, area.bottom() - 3.0), 2.5, 2.5);
      }
      else if (bars)
      {
        // A present map is always ≥2 px tall, so a present-but-tiny/zero value is
        // still visible (and distinguishable from a missing map's hollow marker).
        const double barWidth = std::min(slot * 0.6, 26.0);
        const double top = std::min(mapY(it->second), area.bottom() - 2.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(52, 152, 219));
        painter.drawRect(QRectF(centre - barWidth / 2.0, top, barWidth, area.bottom() - top));
      }
      else
      {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(52, 152, 219));
        painter.drawEllipse(QPointF(centre, mapY(it->second)), 2.5, 2.5);
      }

      if (showLabels)
      {
        painter.save();
        painter.setPen(palette().color(QPalette::Text));
        painter.translate(centre, area.bottom() + 4.0);
        painter.rotate(45.0);
        const QString label = painter.fontMetrics().elidedText(columnLabel(column), Qt::ElideRight, 60);
        painter.drawText(QPointF(0.0, 8.0), label);
        painter.restore();
      }
    }
  }

  void ConsensusQuantChart::mouseMoveEvent(QMouseEvent* event)
  {
    const QRectF area = plotRect();
    const int columnCount = static_cast<int>(columns_.size());
    if (columnCount == 0 || area.width() <= 0.0)
    {
      QToolTip::hideText();
      QWidget::mouseMoveEvent(event);
      return;
    }

    // Map the cursor to the nearest column slot; this is the only per-map
    // identification available in the dot strip, where axis labels are dropped.
    const double x = event->position().x();
    int index = static_cast<int>((x - area.left()) / (area.width() / columnCount));
    index = std::clamp(index, 0, columnCount - 1);
    const ConsensusColumn& column = columns_[static_cast<std::size_t>(index)];

    double intensity = 0.0;
    bool present = false;
    for (const ConsensusHandle& handle : handles_)
      if (handle.mapIndex == column.mapIndex && std::isfinite(handle.intensity))
      {
        intensity += handle.intensity;
        present = true;
      }

    const QString value = present ? QString::number(intensity, 'g', 6) : tr("not quantified");
    QToolTip::showText(event->globalPosition().toPoint(),
                       QStringLiteral("%1\n%2").arg(columnLabel(column), value), this);
    QWidget::mouseMoveEvent(event);
  }
}
