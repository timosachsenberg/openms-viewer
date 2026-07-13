#include "widgets/FaimsPanelWidget.h"

#include "plot/PeakMapRasterizer.h"
#include "plot/PlotTheme.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QResizeEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace OpenMSViewer
{
  namespace
  {
    constexpr std::array<QColor, 8> channelColors{
      QColor(142, 68, 173), QColor(52, 152, 219), QColor(46, 204, 113), QColor(241, 196, 15),
      QColor(231, 76, 60), QColor(26, 188, 156), QColor(230, 126, 34), QColor(120, 144, 156)};
  }

  FaimsPeakMapsWidget::FaimsPeakMapsWidget(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("faimsPeakMaps"));
    setMinimumHeight(190);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("FAIMS peak-map comparison"));
    setAccessibleDescription(tr("Small-multiple peak maps for each compensation voltage."));
    renderTimer_.setSingleShot(true);
    renderTimer_.setInterval(45);
    connect(&renderTimer_, &QTimer::timeout, this, &FaimsPeakMapsWidget::startRender);
    connect(&renderWatcher_, &QFutureWatcher<std::vector<QImage>>::finished, this, [this]
    {
      if (activeGeneration_ == desiredGeneration_)
      {
        images_ = renderWatcher_.result();
        update();
      }
      else renderTimer_.start(0);
    });
  }

  FaimsPeakMapsWidget::~FaimsPeakMapsWidget()
  {
    if (renderWatcher_.isRunning()) renderWatcher_.waitForFinished();
  }

  void FaimsPeakMapsWidget::setData(
    const std::vector<std::shared_ptr<const OpenMS::MSExperiment>>& experiments,
    const std::vector<FaimsChannelRecord>& channels, const PlotRange& bounds)
  {
    experiments_ = experiments;
    channels_ = channels;
    bounds_ = bounds;
    view_ = bounds;
    images_.clear();
    selectedChannel_ = -1;
    scheduleRender();
  }

  void FaimsPeakMapsWidget::setViewRange(const PlotRange& range)
  {
    if (experiments_.empty()) return;
    view_ = range.normalized().clampedTo(bounds_);
    scheduleRender();
  }

  void FaimsPeakMapsWidget::setSelectedChannel(int channelIndex)
  {
    selectedChannel_ = channelIndex;
    update();
  }

  const std::vector<QImage>& FaimsPeakMapsWidget::images() const noexcept { return images_; }

  QRect FaimsPeakMapsWidget::cellRect(std::size_t index) const
  {
    const int columns = std::max(1, std::min(4, static_cast<int>(channels_.size())));
    const int rows = std::max(1, (static_cast<int>(channels_.size()) + columns - 1) / columns);
    const int width = this->width() / columns;
    const int height = this->height() / rows;
    return {static_cast<int>(index % static_cast<std::size_t>(columns)) * width,
            static_cast<int>(index / static_cast<std::size_t>(columns)) * height,
            width, height};
  }

  void FaimsPeakMapsWidget::scheduleRender()
  {
    ++desiredGeneration_;
    if (experiments_.empty())
    {
      images_.clear();
      update();
      return;
    }
    renderTimer_.start();
  }

  void FaimsPeakMapsWidget::startRender()
  {
    if (experiments_.empty() || renderWatcher_.isRunning()) return;
    activeGeneration_ = desiredGeneration_;
    const auto experiments = experiments_;
    const PlotRange range = view_;
    std::vector<QSize> sizes;
    sizes.reserve(experiments.size());
    for (std::size_t index = 0; index < experiments.size(); ++index)
      sizes.push_back(cellRect(index).adjusted(8, 27, -8, -8).size());
    renderWatcher_.setFuture(QtConcurrent::run([experiments, range, sizes]
    {
      std::vector<QImage> images;
      images.reserve(experiments.size());
      for (std::size_t index = 0; index < experiments.size(); ++index)
      {
        images.push_back(experiments[index]
          ? PeakMapRasterizer::render(*experiments[index], range, sizes[index], true, 1)
          : QImage{});
      }
      return images;
    }));
  }

  void FaimsPeakMapsWidget::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    if (channels_.empty())
    {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(rect(), Qt::AlignCenter, tr("No FAIMS peak maps"));
      return;
    }
    for (std::size_t index = 0; index < channels_.size(); ++index)
    {
      const QRect cell = cellRect(index).adjusted(4, 3, -4, -3);
      const QRect imageArea = cell.adjusted(4, 24, -4, -4);
      painter.setPen(index == static_cast<std::size_t>(selectedChannel_)
        ? QPen(QColor(180, 90, 255), 3.0) : QPen(palette().color(QPalette::Mid), 1.0));
      painter.drawRect(cell);
      painter.setPen(index == static_cast<std::size_t>(selectedChannel_)
        ? QColor(205, 140, 255) : palette().color(QPalette::Text));
      painter.drawText(QRect(cell.left() + 5, cell.top() + 2, cell.width() - 10, 20),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       tr("CV %1 V · %2 scans").arg(channels_[index].compensationVoltage, 0, 'f', 1)
                                                 .arg(channels_[index].tic.size()));
      painter.fillRect(imageArea, QColor::fromRgb(PeakMapRasterizer::color(0.0, PeakMapColorMap::Viridis)));
      if (index < images_.size() && !images_[index].isNull())
        painter.drawImage(imageArea, images_[index]);
    }
  }

  void FaimsPeakMapsWidget::resizeEvent(QResizeEvent* event)
  {
    QWidget::resizeEvent(event);
    scheduleRender();
  }

  void FaimsPeakMapsWidget::mousePressEvent(QMouseEvent* event)
  {
    if (event->button() != Qt::LeftButton) return;
    for (std::size_t index = 0; index < channels_.size(); ++index)
    {
      if (cellRect(index).contains(event->pos()))
      {
        emit channelActivated(static_cast<int>(index));
        return;
      }
    }
  }

  FaimsTracePlotWidget::FaimsTracePlotWidget(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("faimsTracePlot"));
    setMinimumHeight(220);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("FAIMS chromatogram comparison"));
    setAccessibleDescription(tr("TIC traces grouped by FAIMS compensation voltage."));
  }

  void FaimsTracePlotWidget::setChannels(const std::vector<FaimsChannelRecord>& channels)
  {
    channels_ = channels;
    selectedChannel_ = -1;
    update();
  }

  void FaimsTracePlotWidget::setSelectedChannel(int channelIndex)
  {
    selectedChannel_ = channelIndex >= 0 && static_cast<std::size_t>(channelIndex) < channels_.size()
      ? channelIndex : -1;
    update();
  }

  void FaimsTracePlotWidget::setPeakMapRange(const PlotRange& range)
  {
    peakMapRange_ = range;
    hasPeakMapRange_ = true;
    update();
  }

  int FaimsTracePlotWidget::selectedChannel() const noexcept { return selectedChannel_; }

  QRect FaimsTracePlotWidget::plotRect() const
  {
    return rect().adjusted(64, 34, -18, -40);
  }

  void FaimsTracePlotWidget::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    const QRect area = plotRect();
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(area);
    if (channels_.empty())
    {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(area, Qt::AlignCenter, tr("No multi-CV FAIMS data"));
      return;
    }

    double rtMin = std::numeric_limits<double>::infinity();
    double rtMax = -std::numeric_limits<double>::infinity();
    double intensityMax = 0.0;
    for (std::size_t channel = 0; channel < channels_.size(); ++channel)
    {
      if (selectedChannel_ >= 0 && channel != static_cast<std::size_t>(selectedChannel_)) continue;
      if (channels_[channel].tic.empty()) continue;
      rtMin = std::min(rtMin, channels_[channel].tic.front().rt);
      rtMax = std::max(rtMax, channels_[channel].tic.back().rt);
      for (const auto& point : channels_[channel].tic)
        intensityMax = std::max(intensityMax, point.intensity);
    }
    if (!std::isfinite(rtMin)) return;
    if (rtMax <= rtMin) rtMax = rtMin + 1.0;
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
      const double leftRt = std::max(rtMin, peakMapRange_.rtMin);
      const double rightRt = std::min(rtMax, peakMapRange_.rtMax);
      if (rightRt >= leftRt)
        painter.fillRect(QRectF(QPointF(xForRt(leftRt), area.top()),
                                QPointF(xForRt(rightRt), area.bottom())), PlotTheme::rangeHighlight(palette()));
    }
    int legendX = area.left();
    for (std::size_t channel = 0; channel < channels_.size(); ++channel)
    {
      if (selectedChannel_ >= 0 && channel != static_cast<std::size_t>(selectedChannel_)) continue;
      QPainterPath path;
      bool first = true;
      for (const auto& point : channels_[channel].tic)
      {
        const QPointF position(xForRt(point.rt), yForIntensity(point.intensity));
        first ? path.moveTo(position) : path.lineTo(position);
        first = false;
      }
      const QColor color = channelColors[channel % channelColors.size()];
      painter.setPen(QPen(color, selectedChannel_ == static_cast<int>(channel) ? 2.4 : 1.6));
      painter.drawPath(path);
      const QString label = tr("%1 V").arg(channels_[channel].compensationVoltage, 0, 'f', 1);
      painter.drawLine(legendX, 18, legendX + 17, 18);
      painter.setPen(palette().color(QPalette::Text));
      painter.drawText(legendX + 22, 23, label);
      legendX += 35 + painter.fontMetrics().horizontalAdvance(label);
    }
    painter.setPen(palette().color(QPalette::Text));
    for (int tick = 0; tick <= 5; ++tick)
    {
      const double fraction = tick / 5.0;
      painter.drawText(QRect(area.left() + static_cast<int>(fraction * area.width()) - 35,
                             area.bottom() + 4, 70, 18), Qt::AlignHCenter | Qt::AlignTop,
                       QString::number(rtMin + fraction * (rtMax - rtMin), 'f', 1));
    }
    painter.drawText(QRect(area.left(), height() - 22, area.width(), 18), Qt::AlignCenter,
                     tr("Retention time (s)"));
  }

  void FaimsTracePlotWidget::mousePressEvent(QMouseEvent* event)
  {
    if (event->button() != Qt::LeftButton || !plotRect().contains(event->pos())) return;
    double bestDistance = std::numeric_limits<double>::infinity();
    std::optional<std::size_t> bestSpectrum;
    double rtMin = std::numeric_limits<double>::infinity();
    double rtMax = -std::numeric_limits<double>::infinity();
    for (std::size_t channel = 0; channel < channels_.size(); ++channel)
    {
      if (selectedChannel_ >= 0 && channel != static_cast<std::size_t>(selectedChannel_)) continue;
      if (!channels_[channel].tic.empty())
      {
        rtMin = std::min(rtMin, channels_[channel].tic.front().rt);
        rtMax = std::max(rtMax, channels_[channel].tic.back().rt);
      }
    }
    if (!std::isfinite(rtMin)) return;
    if (rtMax <= rtMin) rtMax = rtMin + 1.0;
    const double fraction = std::clamp((event->position().x() - plotRect().left())
                                         / plotRect().width(), 0.0, 1.0);
    const double targetRt = rtMin + fraction * (rtMax - rtMin);
    for (std::size_t channel = 0; channel < channels_.size(); ++channel)
    {
      if (selectedChannel_ >= 0 && channel != static_cast<std::size_t>(selectedChannel_)) continue;
      for (const auto& point : channels_[channel].tic)
      {
        const double distance = std::abs(point.rt - targetRt);
        if (distance < bestDistance)
        {
          bestDistance = distance;
          bestSpectrum = point.spectrumIndex;
        }
      }
    }
    if (bestSpectrum) emit spectrumActivated(*bestSpectrum);
  }

  FaimsPanelWidget::FaimsPanelWidget(QWidget* parent) : QWidget(parent)
  {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Peak-map CV filter"), this));
    selector_ = new QComboBox(this);
    selector_->setObjectName(QStringLiteral("faimsChannelSelector"));
    selector_->setMinimumWidth(180);
    controls->addWidget(selector_);
    controls->addStretch();
    controls->addWidget(new QLabel(tr("Separate per-CV TIC traces; click a trace to select a scan"), this));
    layout->addLayout(controls);

    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("faimsChannelTable"));
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({tr("CV (V)"), tr("MS1 scans"), tr("RT range (s)"), tr("Total intensity")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setMaximumHeight(150);
    layout->addWidget(table_);
    plot_ = new FaimsTracePlotWidget(this);
    layout->addWidget(plot_, 1);
    peakMaps_ = new FaimsPeakMapsWidget(this);
    layout->addWidget(peakMaps_, 1);

    connect(selector_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) { applySelection(index, true); });
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int)
    {
      selector_->setCurrentIndex(row + 1);
    });
    connect(plot_, &FaimsTracePlotWidget::spectrumActivated,
            this, &FaimsPanelWidget::spectrumActivated);
    connect(peakMaps_, &FaimsPeakMapsWidget::channelActivated, this, [this](int channel)
    {
      selector_->setCurrentIndex(channel + 1);
    });
  }

  void FaimsPanelWidget::setChannels(const std::vector<FaimsChannelRecord>& channels)
  {
    channels_ = channels;
    const QSignalBlocker blocker(selector_);
    selector_->clear();
    selector_->addItem(tr("All compensation voltages"), -1);
    table_->setRowCount(static_cast<int>(channels_.size()));
    for (std::size_t index = 0; index < channels_.size(); ++index)
    {
      const auto& channel = channels_[index];
      selector_->addItem(tr("CV %1 V").arg(channel.compensationVoltage, 0, 'f', 1),
                         static_cast<int>(index));
      const QString rtRange = channel.tic.empty() ? QStringLiteral("-")
        : QStringLiteral("%1–%2").arg(channel.tic.front().rt, 0, 'f', 2)
                                      .arg(channel.tic.back().rt, 0, 'f', 2);
      table_->setItem(static_cast<int>(index), 0,
                      new QTableWidgetItem(QString::number(channel.compensationVoltage, 'f', 1)));
      table_->setItem(static_cast<int>(index), 1,
                      new QTableWidgetItem(QString::number(channel.tic.size())));
      table_->setItem(static_cast<int>(index), 2, new QTableWidgetItem(rtRange));
      table_->setItem(static_cast<int>(index), 3,
                      new QTableWidgetItem(QString::number(channel.totalIntensity, 'e', 3)));
    }
    selector_->setCurrentIndex(0);
    table_->clearSelection();
    plot_->setChannels(channels_);
    applySelection(0, false);
  }

  void FaimsPanelWidget::clear()
  {
    setChannels({});
    peakMaps_->setData({}, {}, {});
  }
  void FaimsPanelWidget::setExperiments(
    const std::vector<std::shared_ptr<const OpenMS::MSExperiment>>& experiments,
    const PlotRange& bounds)
  {
    peakMaps_->setData(experiments, channels_, bounds);
  }

  void FaimsPanelWidget::setPeakMapRange(const PlotRange& range)
  {
    plot_->setPeakMapRange(range);
    peakMaps_->setViewRange(range);
  }

  void FaimsPanelWidget::setSelectedChannel(int channelIndex)
  {
    const int selectorIndex = channelIndex >= 0
      && static_cast<std::size_t>(channelIndex) < channels_.size() ? channelIndex + 1 : 0;
    const QSignalBlocker blocker(selector_);
    selector_->setCurrentIndex(selectorIndex);
    applySelection(selectorIndex, false);
  }

  std::size_t FaimsPanelWidget::channelCount() const noexcept { return channels_.size(); }
  int FaimsPanelWidget::selectedChannel() const noexcept { return plot_->selectedChannel(); }
  FaimsTracePlotWidget* FaimsPanelWidget::plot() const noexcept { return plot_; }
  FaimsPeakMapsWidget* FaimsPanelWidget::peakMaps() const noexcept { return peakMaps_; }

  void FaimsPanelWidget::applySelection(int selectorIndex, bool emitSignal)
  {
    const int channelIndex = selectorIndex > 0 ? selectorIndex - 1 : -1;
    plot_->setSelectedChannel(channelIndex);
    peakMaps_->setSelectedChannel(channelIndex);
    if (channelIndex >= 0) table_->selectRow(channelIndex); else table_->clearSelection();
    if (emitSignal) emit channelSelected(channelIndex);
  }
}
