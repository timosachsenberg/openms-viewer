#include "widgets/ImagingPanelWidget.h"

#include "plot/RasterShading.h"

#include <QtConcurrent/QtConcurrentRun>

#include "plot/PlotAxis.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace OpenMSViewer
{
  namespace
  {
    // Reuse the shared 256-entry viridis LUT so the image and colorbar match the
    // rest of the app instead of carrying a separate approximation.
    QRgb viridis(double value)
    {
      return RasterShading::sample(PeakMapColorMap::Viridis, value);
    }

    // Robust normalization scale: the `percentile`-th value of the positive,
    // in-mask intensities, so a single hot matrix pixel can't dim the whole image.
    // Returns 0 when there is no usable (finite, positive, in-mask) sample.
    double robustMaximum(const std::vector<double>& values, const std::vector<bool>& mask,
                         double percentile)
    {
      const bool useMask = !mask.empty() && mask.size() == values.size();
      std::vector<double> positive;
      positive.reserve(values.size());
      for (std::size_t index = 0; index < values.size(); ++index)
        if ((!useMask || mask[index]) && std::isfinite(values[index]) && values[index] > 0.0)
          positive.push_back(values[index]);
      if (positive.empty()) return 0.0;
      // Nearest-rank percentile (ceil(p·n)-1) so e.g. the 99th percentile of
      // [1,100] is 100, not the truncated lower value.
      const auto rank = static_cast<std::size_t>(std::clamp(
        std::ceil(percentile * static_cast<double>(positive.size())) - 1.0,
        0.0, static_cast<double>(positive.size() - 1)));
      std::nth_element(positive.begin(),
                       positive.begin() + static_cast<std::ptrdiff_t>(rank), positive.end());
      return positive[rank];
    }
  }

  ImagingImageWidget::ImagingImageWidget(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("imagingImage"));
    setMinimumSize(420, 360);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Mass-spectrometry ion image"));
    setAccessibleDescription(tr("Spatial ion image. Select a pixel to inspect its mass spectrum."));
  }

  void ImagingImageWidget::setGeometry(const ImagingSummary& summary)
  {
    summary_ = summary;
    spectrumByPixel_.assign(static_cast<std::size_t>(summary.width) * summary.height, std::nullopt);
    for (const ImagingPixelRecord& pixel : summary.pixels)
    {
      if (pixel.x < summary.width && pixel.y < summary.height)
        spectrumByPixel_[static_cast<std::size_t>(pixel.y) * summary.width + pixel.x]
          = pixel.spectrumIndex;
    }
    selectedSpectrum_.reset();
    rebuildImage();
  }

  void ImagingImageWidget::setImage(const std::vector<double>& intensities,
                                    const std::vector<bool>& mask, const QString& title)
  {
    intensities_ = intensities;
    mask_ = mask;
    title_ = title;
    composite_ = false;
    legend_.clear();
    rebuildImage();
  }

  void ImagingImageWidget::setCompositeImage(QImage image,
                                             const std::vector<double>& intensities,
                                             const std::vector<bool>& mask,
                                             const QString& title,
                                             std::vector<std::pair<QColor, QString>> legend)
  {
    image_ = std::move(image);
    intensities_ = intensities;
    mask_ = mask;
    title_ = title;
    composite_ = true;
    legend_ = std::move(legend);
    displayMax_ = 0.0;
    update();
  }

  void ImagingImageWidget::clear()
  {
    summary_ = {};
    intensities_.clear();
    mask_.clear();
    spectrumByPixel_.clear();
    title_.clear();
    image_ = {};
    composite_ = false;
    displayMax_ = 0.0;
    legend_.clear();
    selectedSpectrum_.reset();
    update();
  }

  void ImagingImageWidget::setSelectedSpectrum(std::optional<std::size_t> spectrumIndex)
  {
    selectedSpectrum_ = spectrumIndex;
    update();
  }

  const QImage& ImagingImageWidget::renderedImage() const noexcept { return image_; }
  std::optional<std::size_t> ImagingImageWidget::selectedSpectrum() const noexcept
  {
    return selectedSpectrum_;
  }

  QRect ImagingImageWidget::imageRect() const
  {
    const QRect available = rect().adjusted(54, 36, -24, -45);
    if (summary_.width == 0 || summary_.height == 0) return available;
    const double scale = std::min(available.width() / static_cast<double>(summary_.width),
                                  available.height() / static_cast<double>(summary_.height));
    const QSize size(std::max(1, static_cast<int>(std::floor(summary_.width * scale))),
                     std::max(1, static_cast<int>(std::floor(summary_.height * scale))));
    return QRect(QPoint(available.center().x() - size.width() / 2,
                        available.center().y() - size.height() / 2), size);
  }

  std::optional<std::pair<std::uint32_t, std::uint32_t>> ImagingImageWidget::pixelAt(
    const QPointF& position) const
  {
    const QRect area = imageRect();
    if (!area.contains(position.toPoint()) || summary_.width == 0 || summary_.height == 0)
      return std::nullopt;
    const auto x = static_cast<std::uint32_t>(std::clamp(
      static_cast<int>((position.x() - area.left()) / area.width() * summary_.width),
      0, static_cast<int>(summary_.width) - 1));
    const auto y = static_cast<std::uint32_t>(std::clamp(
      static_cast<int>((position.y() - area.top()) / area.height() * summary_.height),
      0, static_cast<int>(summary_.height) - 1));
    return std::pair{x, y};
  }

  void ImagingImageWidget::rebuildImage()
  {
    if (summary_.width == 0 || summary_.height == 0
        || intensities_.size() != static_cast<std::size_t>(summary_.width) * summary_.height)
    {
      image_ = {};
      update();
      return;
    }
    image_ = QImage(static_cast<int>(summary_.width), static_cast<int>(summary_.height),
                    QImage::Format_RGB32);
    image_.fill(viridis(0.0));  // colormap floor, so masked pixels match the data in both themes
    // Robust 99th-percentile scale (not the raw max) so one hot pixel doesn't dim
    // the whole image; values above it clamp to the top of the colormap. displayMax_
    // is the honest label (0 when there is no positive signal).
    displayMax_ = robustMaximum(intensities_, mask_, 0.99);
    const double divisor = displayMax_ > 0.0 ? displayMax_ : 1.0;
    const double logMaximum = std::log1p(divisor);
    const bool useMask = !mask_.empty() && mask_.size() == intensities_.size();
    for (std::uint32_t y = 0; y < summary_.height; ++y)
    {
      for (std::uint32_t x = 0; x < summary_.width; ++x)
      {
        const std::size_t index = static_cast<std::size_t>(y) * summary_.width + x;
        if (useMask && !mask_[index]) continue;
        const double normalized = std::log1p(std::max(0.0, intensities_[index])) / logMaximum;
        image_.setPixel(static_cast<int>(x), static_cast<int>(y), viridis(normalized));
      }
    }
    update();
  }

  void ImagingImageWidget::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    const QRect area = imageRect();
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRect(area.left(), 6, area.width(), 24), Qt::AlignCenter, title_);
    if (image_.isNull())
    {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(area, Qt::AlignCenter, tr("Open an imzML dataset to view ion images"));
      return;
    }
    painter.drawImage(area, image_);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(area);
    if (selectedSpectrum_)
    {
      for (const ImagingPixelRecord& pixel : summary_.pixels)
      {
        if (pixel.spectrumIndex != *selectedSpectrum_) continue;
        const double cellWidth = area.width() / static_cast<double>(summary_.width);
        const double cellHeight = area.height() / static_cast<double>(summary_.height);
        painter.setPen(QPen(QColor(255, 80, 80), 2.0));
        painter.drawRect(QRectF(area.left() + pixel.x * cellWidth,
                                area.top() + pixel.y * cellHeight, cellWidth, cellHeight));
        break;
      }
    }
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRect(area.left(), area.bottom() + 10, area.width(), 20), Qt::AlignCenter,
                     tr("Pixel x (0–%1)").arg(summary_.width - 1));
    painter.save();
    painter.translate(15, area.center().y());
    painter.rotate(-90);
    painter.drawText(QRect(-area.height() / 2, -10, area.height(), 20), Qt::AlignCenter,
                     tr("Pixel y (0–%1)").arg(summary_.height - 1));
    painter.restore();

    // Scale drawn as an overlay inside the top-right of the image (like the peak
    // map colorbar) so the image stays centred: a per-channel colour+m/z legend
    // for a multi-ion overlay, or a viridis intensity colorbar otherwise.
    const QColor backing(0, 0, 0, 150);
    const bool roomForScale = area.width() >= 90 && area.height() >= 80;
    if (roomForScale && composite_ && !legend_.empty())
    {
      int textWidth = 0;
      for (const auto& [color, label] : legend_)
        textWidth = std::max(textWidth, painter.fontMetrics().horizontalAdvance(label));
      const int rows = std::min(static_cast<int>(legend_.size()),
                                std::max(1, (area.height() - 12) / 18));   // cap to what fits
      const int boxWidth = std::clamp(29 + textWidth, 40, area.width() - 12);
      const int boxHeight = rows * 18 + 6;
      const QRect box(area.right() - boxWidth - 6, area.top() + 6, boxWidth, boxHeight);
      painter.setPen(Qt::NoPen);
      painter.setBrush(backing);
      painter.drawRoundedRect(box, 4, 4);
      int legendY = box.top() + 6;
      for (int row = 0; row < rows; ++row)
      {
        const auto& [color, label] = legend_[static_cast<std::size_t>(row)];
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRect(QRect(box.left() + 6, legendY, 11, 11));
        painter.setPen(QColor(235, 235, 240));
        painter.drawText(QRect(box.left() + 23, legendY - 2, boxWidth - 27, 15),
                         Qt::AlignLeft | Qt::AlignVCenter, label);
        legendY += 18;
      }
    }
    else if (roomForScale && !composite_ && !image_.isNull() && displayMax_ > 0.0)
    {
      const int barHeight = std::min(150, std::max(60, area.height() / 3));
      const QRect bar(area.right() - 18, area.top() + 22, 11, barHeight);
      const QString maxLabel = QString::number(displayMax_, 'g', 3);
      const int labelWidth = painter.fontMetrics().horizontalAdvance(maxLabel);
      painter.setPen(Qt::NoPen);
      painter.setBrush(backing);
      painter.drawRoundedRect(QRect(bar.left() - std::max(18, labelWidth) - 6, bar.top() - 18,
                                    bar.width() + std::max(18, labelWidth) + 12, bar.height() + 34),
                              4, 4);
      for (int row = 0; row < bar.height(); ++row)
      {
        const double value = 1.0 - row / static_cast<double>(std::max(1, bar.height() - 1));
        painter.setPen(QColor::fromRgb(viridis(value)));
        painter.drawLine(bar.left(), bar.top() + row, bar.right(), bar.top() + row);
      }
      painter.setPen(QColor(220, 220, 228));
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(bar);
      painter.setPen(QColor(235, 235, 240));
      painter.drawText(QRect(bar.left() - labelWidth - 4, bar.top() - 16, labelWidth + bar.width() + 4, 14),
                       Qt::AlignRight | Qt::AlignVCenter, maxLabel);
      painter.drawText(QRect(bar.left() - labelWidth - 4, bar.bottom() + 2, labelWidth + bar.width() + 4, 14),
                       Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0"));
    }
  }

  void ImagingImageWidget::mousePressEvent(QMouseEvent* event)
  {
    if (event->button() != Qt::LeftButton) return;
    const auto pixel = pixelAt(event->position());
    if (!pixel) return;
    const std::size_t offset = static_cast<std::size_t>(pixel->second) * summary_.width + pixel->first;
    if (offset < spectrumByPixel_.size() && spectrumByPixel_[offset])
      emit pixelActivated(*spectrumByPixel_[offset], pixel->first, pixel->second);
  }

  void ImagingImageWidget::mouseMoveEvent(QMouseEvent* event)
  {
    const auto pixel = pixelAt(event->position());
    if (!pixel) return;
    const std::size_t offset = static_cast<std::size_t>(pixel->second) * summary_.width + pixel->first;
    if (offset >= intensities_.size()) return;
    const QString spectrum = offset < spectrumByPixel_.size() && spectrumByPixel_[offset]
      ? QString::number(*spectrumByPixel_[offset]) : QStringLiteral("—");
    QToolTip::showText(event->globalPosition().toPoint(),
      tr("Pixel (%1, %2) · spectrum %3 · intensity %4")
        .arg(pixel->first).arg(pixel->second).arg(spectrum)
        .arg(intensities_[offset], 0, 'g', 6), this);
  }

  // ---------------------------------------------------------------------------
  //  AggregateSpectrumWidget — whole-image spectrum with click-to-browse.
  // ---------------------------------------------------------------------------
  AggregateSpectrumWidget::AggregateSpectrumWidget(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("imagingAggregateSpectrum"));
    setMinimumHeight(140);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Aggregate mass spectrum"));
    setAccessibleDescription(
      tr("Whole-image aggregate spectrum. Click a peak to image that m/z; wheel to zoom."));
  }

  void AggregateSpectrumWidget::setSpectrum(std::vector<double> mz, std::vector<double> intensity,
                                            QString title, bool keepView)
  {
    mz_ = std::move(mz);
    intensity_ = std::move(intensity);
    if (intensity_.size() != mz_.size()) intensity_.resize(mz_.size(), 0.0);   // keep paired
    title_ = std::move(title);
    intensityMax_ = 0.0;
    for (const double value : intensity_) intensityMax_ = std::max(intensityMax_, value);
    if (!keepView) view_.reset();   // keepView lets a Mean/Max swap stay at the same zoom
    hoverPos_.reset();
    update();
  }

  void AggregateSpectrumWidget::setMarkerMz(std::optional<double> mz) { markerMz_ = mz; update(); }

  void AggregateSpectrumWidget::clear()
  {
    mz_.clear();
    intensity_.clear();
    title_.clear();
    intensityMax_ = 0.0;
    view_.reset();
    markerMz_.reset();
    hoverPos_.reset();
    update();
  }

  QRect AggregateSpectrumWidget::plotRect() const { return rect().adjusted(58, 20, -12, -34); }

  std::pair<double, double> AggregateSpectrumWidget::viewRange() const
  {
    if (view_) return *view_;
    if (mz_.empty()) return {0.0, 1.0};
    return {mz_.front(), std::max(mz_.back(), mz_.front() + 1.0)};
  }

  std::optional<std::size_t> AggregateSpectrumWidget::peakAt(double x) const
  {
    if (mz_.empty()) return std::nullopt;
    const QRect area = plotRect();
    const auto [low, high] = viewRange();
    const double target = low + std::clamp((x - area.left()) / std::max(1, area.width()), 0.0, 1.0)
                                  * (high - low);
    auto it = std::lower_bound(mz_.begin(), mz_.end(), target);
    std::size_t best = 0;
    if (it == mz_.end()) best = mz_.size() - 1;
    else if (it == mz_.begin()) best = 0;
    else
    {
      const auto previous = it - 1;
      best = std::abs(*it - target) < std::abs(*previous - target)
               ? static_cast<std::size_t>(it - mz_.begin())
               : static_cast<std::size_t>(previous - mz_.begin());
    }
    const double px = area.left() + (mz_[best] - low) / (high - low) * area.width();
    return std::abs(px - x) > 8.0 ? std::nullopt : std::optional<std::size_t>(best);
  }

  void AggregateSpectrumWidget::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    const QRect area = plotRect();
    if (area.width() <= 0 || area.height() <= 0) return;   // too small to draw safely
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRect(area.left(), 2, area.width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
                     title_.isEmpty() ? tr("Aggregate spectrum") : title_);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawLine(area.bottomLeft(), area.bottomRight());
    painter.drawLine(area.topLeft(), area.bottomLeft());
    if (mz_.empty())
    {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(area, Qt::AlignCenter, tr("Computing aggregate spectrum…"));
      return;
    }

    const auto [low, high] = viewRange();
    const double span = high - low;
    double viewMax = 0.0;
    for (std::size_t i = 0; i < mz_.size(); ++i)
      if (mz_[i] >= low && mz_[i] <= high) viewMax = std::max(viewMax, intensity_[i]);
    if (viewMax <= 0.0) viewMax = intensityMax_ > 0.0 ? intensityMax_ : 1.0;
    const auto xFor = [&](double mz) { return area.left() + (mz - low) / span * area.width(); };
    const auto yFor = [&](double value) { return area.bottom() - value / viewMax * area.height() * 0.9; };

    const auto mzTicks = PlotAxis::niceTicks(low, high, 6);
    const auto intensityTicks = PlotAxis::niceTicks(0.0, viewMax, 4);
    painter.setPen(QPen(palette().color(QPalette::Mid), 1.0, Qt::DotLine));
    for (const double tick : mzTicks)
    {
      const double x = xFor(tick);
      if (x >= area.left() && x <= area.right()) painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    for (const double tick : intensityTicks)
      painter.drawLine(QPointF(area.left(), yFor(tick)), QPointF(area.right(), yFor(tick)));

    // Per-pixel-column max envelope so dense aggregates render without gaps.
    const bool darkTheme = palette().color(QPalette::Window).lightnessF() < 0.5;
    std::vector<double> envelope(static_cast<std::size_t>(area.width()) + 1, 0.0);
    for (std::size_t i = 0; i < mz_.size(); ++i)
    {
      if (mz_[i] < low || mz_[i] > high) continue;
      const int bin = std::clamp(static_cast<int>(xFor(mz_[i])) - area.left(), 0, area.width());
      envelope[static_cast<std::size_t>(bin)] = std::max(envelope[static_cast<std::size_t>(bin)], intensity_[i]);
    }
    painter.setPen(QPen(darkTheme ? QColor(35, 190, 225) : QColor(15, 110, 150), 1));
    for (int bin = 0; bin <= area.width(); ++bin)
    {
      if (envelope[static_cast<std::size_t>(bin)] <= 0.0) continue;
      const int x = area.left() + bin;
      painter.drawLine(x, area.bottom(), x, static_cast<int>(yFor(envelope[static_cast<std::size_t>(bin)])));
    }

    if (markerMz_ && *markerMz_ >= low && *markerMz_ <= high)
    {
      painter.setPen(QPen(QColor(255, 140, 40), 1.5, Qt::DashLine));
      const double x = xFor(*markerMz_);
      painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }

    painter.setPen(palette().color(QPalette::Text));
    for (const double tick : mzTicks)
    {
      const double x = xFor(tick);
      if (x < area.left() - 0.5 || x > area.right() + 0.5) continue;
      painter.drawText(QRectF(x - 40, area.bottom() + 3, 80, 13), Qt::AlignHCenter | Qt::AlignTop,
                       QString::number(tick, 'f', 1));
    }
    for (const double tick : intensityTicks)
      painter.drawText(QRectF(0, yFor(tick) - 8, area.left() - 6.0, 16), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(tick, 'g', 3));
    painter.drawText(QRect(area.left(), area.bottom() + 16, area.width(), 14), Qt::AlignCenter, tr("m/z"));

    if (hoverPos_ && area.contains(*hoverPos_))
    {
      if (const auto index = peakAt(hoverPos_->x()))
      {
        const double x = xFor(mz_[*index]);
        const double y = yFor(intensity_[*index]);
        painter.setPen(QPen(QColor(40, 170, 255), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(x, y), 3.5, 3.5);
        const QString text = tr("m/z %1 · %2").arg(mz_[*index], 0, 'f', 4).arg(intensity_[*index], 0, 'g', 4);
        const double textWidth = std::min<double>(
          painter.fontMetrics().horizontalAdvance(text) + 12.0, area.width());
        const QRectF box(std::clamp<double>(x + 8, area.left(),
                                            std::max<double>(area.left(), area.right() - textWidth)),
                         area.top() + 2, textWidth, 18);
        const QColor base = palette().color(QPalette::Base);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(base.red(), base.green(), base.blue(), 220));
        painter.drawRoundedRect(box, 3, 3);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(box.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
      }
    }
  }

  void AggregateSpectrumWidget::mousePressEvent(QMouseEvent* event)
  {
    if (event->button() == Qt::LeftButton && plotRect().contains(event->position().toPoint()))
    {
      if (const auto index = peakAt(event->position().x()))
      {
        emit peakSelected(mz_[*index]);
        event->accept();
        return;
      }
    }
    QWidget::mousePressEvent(event);
  }

  void AggregateSpectrumWidget::mouseMoveEvent(QMouseEvent* event)
  {
    hoverPos_ = event->pos();
    update();
  }

  void AggregateSpectrumWidget::wheelEvent(QWheelEvent* event)
  {
    if (mz_.empty() || !plotRect().contains(event->position().toPoint())) return;
    const double fullLow = mz_.front();
    const double fullHigh = std::max(mz_.back(), fullLow + 1.0);
    const auto [low, high] = viewRange();
    const double fraction = std::clamp((event->position().x() - plotRect().left())
      / plotRect().width(), 0.0, 1.0);
    const double cursor = low + fraction * (high - low);
    const double newSpan = (high - low) * (event->angleDelta().y() > 0 ? 0.8 : 1.25);
    const double newLow = std::max(fullLow, cursor - fraction * newSpan);
    const double newHigh = std::min(fullHigh, cursor + (1.0 - fraction) * newSpan);
    if (newHigh - newLow < (fullHigh - fullLow) * 1e-4) return;
    if (newLow <= fullLow && newHigh >= fullHigh) view_.reset();
    else view_ = std::pair{newLow, newHigh};
    update();
    event->accept();
  }

  void AggregateSpectrumWidget::mouseDoubleClickEvent(QMouseEvent*)
  {
    view_.reset();
    update();
  }

  void AggregateSpectrumWidget::leaveEvent(QEvent*)
  {
    hoverPos_.reset();
    update();
  }

  // ---------------------------------------------------------------------------
  ImagingPanelWidget::ImagingPanelWidget(QWidget* parent) : QWidget(parent)
  {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Display"), this));
    displayMode_ = new QComboBox(this);
    displayMode_->setObjectName(QStringLiteral("imagingDisplayMode"));
    displayMode_->addItems({tr("TIC image"), tr("Ion image"), tr("Multi-ion overlay")});
    controls->addWidget(displayMode_);
    controls->addWidget(new QLabel(tr("m/z"), this));
    mz_ = new QDoubleSpinBox(this);
    mz_->setObjectName(QStringLiteral("imagingMz"));
    mz_->setDecimals(5);
    mz_->setRange(0.0, 1e9);
    mz_->setMaximumWidth(130);
    controls->addWidget(mz_);
    controls->addWidget(new QLabel(tr("ppm"), this));
    tolerance_ = new QDoubleSpinBox(this);
    tolerance_->setObjectName(QStringLiteral("imagingPpm"));
    tolerance_->setDecimals(1);
    tolerance_->setRange(0.1, 10000.0);
    tolerance_->setValue(10.0);
    tolerance_->setMaximumWidth(90);
    controls->addWidget(tolerance_);
    extract_ = new QPushButton(tr("Extract"), this);
    extract_->setObjectName(QStringLiteral("imagingExtract"));
    controls->addWidget(extract_);
    addOverlay_ = new QPushButton(tr("Add to overlay"), this);
    controls->addWidget(addOverlay_);
    clearOverlay_ = new QPushButton(tr("Clear overlay"), this);
    controls->addWidget(clearOverlay_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Aggregate"), this));
    aggregateMode_ = new QComboBox(this);
    aggregateMode_->setObjectName(QStringLiteral("imagingAggregateMode"));
    aggregateMode_->addItems({tr("Mean"), tr("Max (skyline)")});
    controls->addWidget(aggregateMode_);
    controls->addStretch();
    layout->addLayout(controls);
    info_ = new QLabel(tr("No imaging dataset loaded"), this);
    info_->setObjectName(QStringLiteral("imagingInfo"));
    layout->addWidget(info_);

    // Image on top, aggregate spectrum below (resizable), so a peak clicked in the
    // aggregate spectrum can be imaged above it.
    auto* splitter = new QSplitter(Qt::Vertical, this);
    image_ = new ImagingImageWidget(this);
    splitter->addWidget(image_);
    aggregate_ = new AggregateSpectrumWidget(this);
    splitter->addWidget(aggregate_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    connect(displayMode_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int mode)
    {
      if (mode == 0) showTicImage();
      else if (mode == 1 && currentIonImage_)
        image_->setImage(currentIonImage_->intensities, currentIonImage_->mask,
          tr("Ion image · m/z %1 ± %2 ppm").arg(currentIonImage_->mz, 0, 'f', 5)
                                                .arg(currentIonImage_->tolerancePpm, 0, 'f', 1));
      else if (mode == 2) showOverlay();
    });
    connect(extract_, &QPushButton::clicked, this, &ImagingPanelWidget::extractIonImage);
    connect(addOverlay_, &QPushButton::clicked, this, &ImagingPanelWidget::addOverlay);
    connect(clearOverlay_, &QPushButton::clicked, this, &ImagingPanelWidget::clearOverlay);
    connect(image_, &ImagingImageWidget::pixelActivated, this,
            [this](std::size_t spectrum, std::uint32_t x, std::uint32_t y)
    {
      image_->setSelectedSpectrum(spectrum);
      info_->setText(tr("Pixel (%1, %2) · spectrum #%3").arg(x).arg(y).arg(spectrum));
      emit spectrumActivated(spectrum);
    });
    connect(&extractionWatcher_, &QFutureWatcher<ImagingImageResult>::finished,
            this, &ImagingPanelWidget::finishExtraction);
    connect(&aggregateWatcher_, &QFutureWatcher<AggregateSpectrum>::finished,
            this, &ImagingPanelWidget::finishAggregate);
    connect(aggregate_, &AggregateSpectrumWidget::peakSelected,
            this, &ImagingPanelWidget::browseToPeak);
    connect(aggregateMode_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateAggregateDisplay(true); });   // keep zoom on a Mean/Max swap
    updateControls();
  }

  ImagingPanelWidget::~ImagingPanelWidget()
  {
    if (extractionWatcher_.isRunning()) extractionWatcher_.waitForFinished();
    if (aggregateWatcher_.isRunning()) aggregateWatcher_.waitForFinished();
  }

  void ImagingPanelWidget::setData(std::shared_ptr<ImagingStore> store,
                                   const ImagingSummary& summary)
  {
    ++dataGeneration_;   // any in-flight extraction now belongs to a stale dataset
    store_ = std::move(store);
    summary_ = summary;
    ticImage_.assign(static_cast<std::size_t>(summary.width) * summary.height, 0.0);
    mask_.assign(ticImage_.size(), false);
    for (const ImagingPixelRecord& pixel : summary.pixels)
    {
      if (pixel.x >= summary.width || pixel.y >= summary.height) continue;
      const std::size_t index = static_cast<std::size_t>(pixel.y) * summary.width + pixel.x;
      ticImage_[index] = pixel.tic;
      mask_[index] = true;
    }
    currentIonImage_.reset();
    overlays_.clear();
    image_->setGeometry(summary_);
    mz_->setRange(summary_.mzMin, summary_.mzMax);
    mz_->setValue((summary_.mzMin + summary_.mzMax) / 2.0);
    displayMode_->setCurrentIndex(0);
    showTicImage();
    info_->setText(tr("%1 pixels · %2 × %3 grid · %4 mode · m/z %5–%6")
      .arg(summary_.pixels.size()).arg(summary_.width).arg(summary_.height)
      .arg(summary_.imagingMode.isEmpty() ? tr("unknown") : summary_.imagingMode)
      .arg(summary_.mzMin, 0, 'f', 4).arg(summary_.mzMax, 0, 'f', 4));

    // Compute the whole-image aggregate spectrum in the background.
    aggregateData_ = {};
    extractionPending_ = false;
    aggregate_->clear();
    launchAggregate();
    updateControls();
  }

  void ImagingPanelWidget::launchAggregate()
  {
    // Serialize: if a scan from a previous dataset is still running, don't start a
    // second full-dataset scan — finishAggregate() relaunches for the current data.
    if (!store_ || aggregateWatcher_.isRunning()) return;
    activeAggregate_ = dataGeneration_;
    const auto store = store_;
    const double low = summary_.mzMin;
    const double high = summary_.mzMax;
    const int bins = std::clamp(static_cast<int>((high - low) * 20.0), 2000, 12000);
    aggregateWatcher_.setFuture(QtConcurrent::run([store, low, high, bins]
    {
      // Never let an I/O exception rethrow through QFuture::result() on the GUI thread.
      try { return store->aggregateSpectrum(low, high, bins); }
      catch (...) { return AggregateSpectrum{}; }
    }));
  }

  void ImagingPanelWidget::clear()
  {
    ++dataGeneration_;
    store_.reset();
    summary_ = {};
    ticImage_.clear();
    mask_.clear();
    currentIonImage_.reset();
    overlays_.clear();
    aggregateData_ = {};
    extractionPending_ = false;
    image_->clear();
    aggregate_->clear();
    info_->setText(tr("No imaging dataset loaded"));
    updateControls();
  }

  void ImagingPanelWidget::setSelectedSpectrum(std::optional<std::size_t> spectrumIndex)
  {
    image_->setSelectedSpectrum(spectrumIndex);
  }

  bool ImagingPanelWidget::hasData() const noexcept { return store_ != nullptr; }
  const ImagingSummary& ImagingPanelWidget::summary() const noexcept { return summary_; }
  ImagingImageWidget* ImagingPanelWidget::imageWidget() const noexcept { return image_; }
  std::size_t ImagingPanelWidget::overlayCount() const noexcept { return overlays_.size(); }

  void ImagingPanelWidget::extractIonImage()
  {
    if (!store_) return;
    if (extractionWatcher_.isRunning()) { extractionPending_ = true; return; }  // coalesce to latest
    extractionPending_ = false;
    activeExtraction_ = dataGeneration_;
    const auto store = store_;
    const double mz = mz_->value();
    const double ppm = tolerance_->value();
    extract_->setEnabled(false);
    info_->setText(tr("Extracting m/z %1 ± %2 ppm from %3 pixels…")
      .arg(mz, 0, 'f', 5).arg(ppm, 0, 'f', 1).arg(summary_.pixels.size()));
    extractionWatcher_.setFuture(QtConcurrent::run([store, mz, ppm]
    {
      ImagingImageResult result;
      result.mz = mz;
      result.tolerancePpm = ppm;
      try
      {
        const OpenMS::IonImage image = store->extractIonImage(mz, ppm);
        result.width = image.getWidth();
        result.height = image.getHeight();
        result.intensities = image.getData();
        result.mask = image.getMask();
      }
      catch (const std::exception& error)
      {
        result.error = QString::fromLocal8Bit(error.what());
      }
      return result;
    }));
  }

  void ImagingPanelWidget::finishExtraction()
  {
    ImagingImageResult result = extractionWatcher_.result();
    // Reject a result whose dataset was replaced (or cleared) while it extracted —
    // a same-pixel-count dataset would otherwise pass the size check below.
    if (activeExtraction_ != dataGeneration_)
    {
      extractionPending_ = false;   // the queued click was for the old dataset
      updateControls();
      return;
    }
    extract_->setEnabled(true);
    if (!result.error.isEmpty())
    {
      info_->setText(tr("Ion-image extraction failed: %1").arg(result.error));
    }
    else if (result.intensities.size() != ticImage_.size())   // defensive geometry check
    {
      info_->setText(tr("Ion-image extraction did not match the current dataset"));
    }
    else
    {
      currentIonImage_ = std::move(result);
      displayMode_->setCurrentIndex(1);
      image_->setImage(currentIonImage_->intensities, currentIonImage_->mask,
        tr("Ion image · m/z %1 ± %2 ppm").arg(currentIonImage_->mz, 0, 'f', 5)
                                              .arg(currentIonImage_->tolerancePpm, 0, 'f', 1));
      info_->setText(tr("Extracted m/z %1 ± %2 ppm")
        .arg(currentIonImage_->mz, 0, 'f', 5).arg(currentIonImage_->tolerancePpm, 0, 'f', 1));
      aggregate_->setMarkerMz(currentIonImage_->mz);   // show which m/z is imaged
    }
    updateControls();
    if (extractionPending_) extractIonImage();   // run the most recent queued click
  }

  void ImagingPanelWidget::finishAggregate()
  {
    if (activeAggregate_ != dataGeneration_)
    {
      launchAggregate();   // dataset changed while scanning; recompute for the current one
      return;
    }
    aggregateData_ = aggregateWatcher_.result();
    updateAggregateDisplay(false);
  }

  void ImagingPanelWidget::updateAggregateDisplay(bool keepView)
  {
    if (!aggregate_) return;
    const bool useMax = aggregateMode_ && aggregateMode_->currentIndex() == 1;
    const std::vector<double>& values = useMax ? aggregateData_.maxIntensity : aggregateData_.mean;
    aggregate_->setSpectrum(aggregateData_.mz, values,
      useMax ? tr("Aggregate spectrum (max / skyline) — click a peak to image it")
             : tr("Aggregate spectrum (mean) — click a peak to image it"),
      keepView);
    // The marker reflects the ion image actually displayed, not the raw spinbox value.
    aggregate_->setMarkerMz(currentIonImage_
      ? std::optional<double>(currentIonImage_->mz) : std::nullopt);
  }

  void ImagingPanelWidget::browseToPeak(double mz)
  {
    if (!store_) return;
    mz_->setValue(mz);
    aggregate_->setMarkerMz(mz);
    extractIonImage();
  }

  void ImagingPanelWidget::addOverlay()
  {
    if (!currentIonImage_ || currentIonImage_->intensities.size() != ticImage_.size()) return;
    overlays_.push_back({currentIonImage_->intensities, currentIonImage_->mask,
                         currentIonImage_->mz, currentIonImage_->tolerancePpm});
    displayMode_->setCurrentIndex(2);
    showOverlay();
    updateControls();
  }

  void ImagingPanelWidget::clearOverlay()
  {
    overlays_.clear();
    displayMode_->setCurrentIndex(currentIonImage_ ? 1 : 0);
    if (currentIonImage_)
      image_->setImage(currentIonImage_->intensities, currentIonImage_->mask,
        tr("Ion image · m/z %1 ± %2 ppm").arg(currentIonImage_->mz, 0, 'f', 5)
                                              .arg(currentIonImage_->tolerancePpm, 0, 'f', 1));
    else showTicImage();
    updateControls();
  }

  void ImagingPanelWidget::showTicImage()
  {
    if (!store_) return;
    image_->setImage(ticImage_, mask_, tr("MSI pixel TIC image"));
  }

  void ImagingPanelWidget::showOverlay()
  {
    if (overlays_.empty())
    {
      showTicImage();
      return;
    }
    static constexpr std::array<std::array<double, 3>, 8> hues{{
      {{1.0, 0.18, 0.18}}, {{0.18, 1.0, 0.18}}, {{0.25, 0.48, 1.0}}, {{1.0, 0.78, 0.16}},
      {{1.0, 0.25, 0.86}}, {{0.2, 0.95, 0.95}}, {{1.0, 0.5, 0.2}}, {{0.7, 0.35, 1.0}}}};
    std::vector<double> composite(ticImage_.size(), 0.0);
    QImage colorImage(static_cast<int>(summary_.width), static_cast<int>(summary_.height),
                      QImage::Format_RGB32);
    colorImage.fill(viridis(0.0));  // colormap floor for masked/empty pixels
    std::vector<double> maxima(overlays_.size(), 1.0);
    std::vector<std::pair<QColor, QString>> legend;
    for (std::size_t channel = 0; channel < overlays_.size(); ++channel)
    {
      // Robust per-channel scale (using that channel's own mask) so one channel's
      // hot pixel can't blank the others; fall back to 1.0 only if it has no signal.
      const double channelMax = robustMaximum(overlays_[channel].intensities,
                                              overlays_[channel].mask, 0.99);
      maxima[channel] = channelMax > 0.0 ? channelMax : 1.0;
      const auto& hue = hues[channel % hues.size()];
      legend.emplace_back(QColor::fromRgbF(static_cast<float>(hue[0]), static_cast<float>(hue[1]),
                                           static_cast<float>(hue[2])),
                          tr("m/z %1").arg(overlays_[channel].mz, 0, 'f', 4));
    }
    for (std::size_t index = 0; index < composite.size(); ++index)
    {
      double red = 0.0;
      double green = 0.0;
      double blue = 0.0;
      for (std::size_t channel = 0; channel < overlays_.size(); ++channel)
      {
        const double value = std::sqrt(std::max(0.0, overlays_[channel].intensities[index]
                                                     / maxima[channel]));
        const auto& hue = hues[channel % hues.size()];
        red += value * hue[0];
        green += value * hue[1];
        blue += value * hue[2];
      }
      red = std::clamp(red, 0.0, 1.0);
      green = std::clamp(green, 0.0, 1.0);
      blue = std::clamp(blue, 0.0, 1.0);
      composite[index] = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
      if (mask_.empty() || mask_[index])
      {
        const int x = static_cast<int>(index % summary_.width);
        const int y = static_cast<int>(index / summary_.width);
        colorImage.setPixel(x, y, qRgb(static_cast<int>(red * 255.0),
                                       static_cast<int>(green * 255.0),
                                       static_cast<int>(blue * 255.0)));
      }
    }
    image_->setCompositeImage(std::move(colorImage), composite, mask_,
                              tr("Multi-ion overlay · %1 channels").arg(overlays_.size()),
                              std::move(legend));
  }

  void ImagingPanelWidget::updateControls()
  {
    const bool available = store_ != nullptr;
    displayMode_->setEnabled(available);
    mz_->setEnabled(available);
    tolerance_->setEnabled(available);
    extract_->setEnabled(available && !extractionWatcher_.isRunning());
    addOverlay_->setEnabled(available && currentIonImage_.has_value());
    clearOverlay_->setEnabled(!overlays_.empty());
  }
}
