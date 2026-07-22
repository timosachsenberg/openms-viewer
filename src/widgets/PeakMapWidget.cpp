#include "widgets/PeakMapWidget.h"

#include "plot/PeakMapRasterizer.h"
#include "plot/PlotAxis.h"
#include "model/RtUnit.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QEnterEvent>
#include <QFormLayout>
#include <QIcon>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

namespace OpenMSViewer
{
  namespace
  {
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
    updateCanvasSizeLimits();
    renderTimer_.setSingleShot(true);
    renderTimer_.setInterval(45);
    connect(&renderTimer_, &QTimer::timeout, this, &PeakMapWidget::startRender);
    editCreateTimer_.setSingleShot(true);
    connect(&editCreateTimer_, &QTimer::timeout, this, [this]
    {
      emit featureCreateRequested(pendingCreateData_.x(), pendingCreateData_.y());
    });
    connect(&renderWatcher_, &QFutureWatcher<QImage>::finished, this, [this]
    {
      if (activeGeneration_ == desiredGeneration_)
      {
        try
        {
          raster_ = renderWatcher_.result();
          rasterRange_ = pendingRasterRange_;         // remember what this image depicts
          rasterAxesSwapped_ = pendingAxesSwapped_;   // ...and in which axis orientation
        }
        catch (const std::exception& error)
        {
          raster_ = {};
          qWarning() << "Peak-map rendering failed:" << error.what();
        }
        catch (...)
        {
          raster_ = {};
          qWarning() << "Peak-map rendering failed with an unknown error";
        }
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

  void PeakMapWidget::setSpectrumMarker(double rt, int msLevel, std::optional<double> precursorMz)
  {
    selectedRt_ = rt;
    selectedMsLevel_ = msLevel;
    selectedMarkerMz_ = precursorMz;
    hasSelectedRt_ = true;
    update();
  }

  void PeakMapWidget::setSelectedMz(std::optional<double> mz)
  {
    if (selectedMz_ == mz) return;
    selectedMz_ = mz;
    update();
  }

  void PeakMapWidget::setSnapToPeak(bool enabled)
  {
    snapToPeak_ = enabled;
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
  PeakMapColorMap PeakMapWidget::colorMap() const noexcept { return colorMap_; }

  PeakMapIntensityScale PeakMapWidget::intensityScale() const noexcept { return intensityScale_; }
  int PeakMapWidget::rasterWidth() const noexcept { return rasterWidth_; }
  const PlotRange& PeakMapWidget::viewRange() const noexcept { return view_; }
  bool PeakMapWidget::canZoomBack() const noexcept { return !history_.empty(); }
  bool PeakMapWidget::hasExperiment() const noexcept { return experiment_ != nullptr; }
  std::shared_ptr<const OpenMS::MSExperiment> PeakMapWidget::experiment() const noexcept
  {
    return experiment_;
  }
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

  bool PeakMapWidget::isZoomedIn() const
  {
    if (!experiment_ || dataBounds_.rtSpan() <= 0.0 || dataBounds_.mzSpan() <= 0.0) return false;
    const double rtEps = dataBounds_.rtSpan() * 1e-6;
    const double mzEps = dataBounds_.mzSpan() * 1e-6;
    return view_.rtMin > dataBounds_.rtMin + rtEps || view_.rtMax < dataBounds_.rtMax - rtEps
        || view_.mzMin > dataBounds_.mzMin + mzEps || view_.mzMax < dataBounds_.mzMax - mzEps;
  }

  void PeakMapWidget::recenterFromMinimap(const QPointF& position)
  {
    const QRect mini = minimapRect();
    if (mini.width() <= 0 || mini.height() <= 0) return;
    const double x = std::clamp((position.x() - mini.left()) / mini.width(), 0.0, 1.0);
    const double y = std::clamp((mini.bottom() - position.y()) / mini.height(), 0.0, 1.0);
    const double centerRt = axesSwapped_ ? dataBounds_.rtMin + y * dataBounds_.rtSpan()
                                          : dataBounds_.rtMin + x * dataBounds_.rtSpan();
    const double centerMz = axesSwapped_ ? dataBounds_.mzMin + x * dataBounds_.mzSpan()
                                          : dataBounds_.mzMin + y * dataBounds_.mzSpan();
    applyRange({centerRt - view_.rtSpan() / 2.0, centerRt + view_.rtSpan() / 2.0,
                centerMz - view_.mzSpan() / 2.0, centerMz + view_.mzSpan() / 2.0}, false);
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
    raster_ = {};   // don't reproject the old-colormap image; show the floor until re-render
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
    raster_ = {};   // the old scale's image would misrepresent the new one; show floor
    minimap_ = {};
    ++desiredMinimapGeneration_;
    scheduleRender();
    startMinimapRender();
  }

  void PeakMapWidget::setRasterWidth(int width)
  {
    const int bounded = std::clamp(width, MinimumRasterWidth, MaximumRasterWidth);
    if (rasterWidth_ == bounded) return;
    rasterWidth_ = bounded;
    updateCanvasSizeLimits();
    raster_ = {};
    scheduleRender();
  }

  void PeakMapWidget::setInteractionMode(int modeIndex)
  {
    const auto mode = static_cast<PeakMapInteractionMode>(std::clamp(modeIndex, 0, 3));
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

  void PeakMapWidget::setConsensusFeatures(const std::vector<ConsensusFeatureRecord>& features)
  {
    consensusFeatures_ = features;
    selectedConsensus_.reset();
    update();
  }

  void PeakMapWidget::setSelectedConsensus(std::optional<std::size_t> consensusIndex)
  {
    selectedConsensus_ = consensusIndex;
    update();
  }

  std::optional<std::size_t> PeakMapWidget::selectedConsensus() const noexcept
  {
    return selectedConsensus_;
  }

  bool PeakMapWidget::hasConsensusFeatures() const noexcept
  {
    return !consensusFeatures_.empty();
  }

  void PeakMapWidget::setShowConsensus(bool show)
  {
    showConsensus_ = show;
    update();
  }

  void PeakMapWidget::setPrecursorMarkers(std::vector<PrecursorMarker> markers)
  {
    precursorMarkers_ = std::move(markers);
    hoveredPrecursor_.reset();
    update();
  }

  void PeakMapWidget::setShowPrecursors(bool show)
  {
    showPrecursors_ = show;
    if (!show) hoveredPrecursor_.reset();
    update();
  }

  void PeakMapWidget::setRtInMinutes(bool minutes)
  {
    if (rtInMinutes_ == minutes) return;
    rtInMinutes_ = minutes;
    setToolTip({});   // force the hover tooltip to rebuild in the new unit
    update();         // raster pixels are unit-agnostic; only axes/readouts re-render
  }

  void PeakMapWidget::zoomToConsensus(std::size_t consensusIndex)
  {
    if (!experiment_ || consensusIndex >= consensusFeatures_.size()) return;
    const ConsensusFeatureRecord& feature = consensusFeatures_[consensusIndex];
    if (!std::isfinite(feature.rt) || !std::isfinite(feature.mz)) return;
    selectedConsensus_ = feature.index;
    const bool envValid = std::isfinite(feature.bounds.rtMin) && std::isfinite(feature.bounds.rtMax)
      && std::isfinite(feature.bounds.mzMin) && std::isfinite(feature.bounds.mzMax)
      && feature.bounds.rtMax >= feature.bounds.rtMin
      && feature.bounds.mzMax >= feature.bounds.mzMin;
    const double rtSpan = std::max(envValid ? feature.bounds.rtSpan() : 0.0, 20.0);
    const double mzSpan = std::max(envValid ? feature.bounds.mzSpan() : 0.0, 4.0);
    applyRange({feature.rt - rtSpan * 0.7, feature.rt + rtSpan * 0.7,
                feature.mz - mzSpan * 0.7, feature.mz + mzSpan * 0.7}, true);
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

  QSize PeakMapWidget::maximumRasterSize() const
  {
    return QSize(rasterWidth_, std::max(1, rasterWidth_ / 2));
  }

  QSize PeakMapWidget::boundedRenderSize() const
  {
    return plotRect().size().boundedTo(maximumRasterSize());
  }

  void PeakMapWidget::updateCanvasSizeLimits()
  {
    // plotRect() removes 68+22 horizontal and 20+52 vertical pixels for axes.
    // The configured raster is a maximum, not a forced size. A smaller viewport
    // gets a smaller widget and raster, still at a literal 1:1 pixel mapping.
    const QSize maximum = maximumRasterSize() + QSize(90, 72);
    setMinimumSize(std::min(480, maximum.width()), std::min(300, maximum.height()));
    setMaximumSize(maximum);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    updateGeometry();
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
    // Throttle, not debounce: leave an already-running timer alone so continuous
    // pan/zoom re-renders at the timer cadence instead of freezing until the
    // interaction pauses. The reprojected preview (paintEvent) tracks in between.
    if (!renderTimer_.isActive()) renderTimer_.start();
    update();
  }

  void PeakMapWidget::startRender()
  {
    if (!experiment_ || renderWatcher_.isRunning()) return;
    const QSize renderSize = boundedRenderSize();
    if (renderSize.width() <= 0 || renderSize.height() <= 0) return;

    activeGeneration_ = desiredGeneration_;
    const auto experiment = experiment_;
    const PlotRange range = view_;
    pendingRasterRange_ = range;
    const bool swapped = axesSwapped_;
    pendingAxesSwapped_ = swapped;
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

  void PeakMapWidget::showGoToRangeDialog()
  {
    if (!experiment_) return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Go to range"));
    auto* form = new QFormLayout(&dialog);
    const auto makeSpin = [&](double value, double low, double high, int decimals)
    {
      auto* spin = new QDoubleSpinBox(&dialog);
      spin->setDecimals(decimals);   // m/z needs finer precision than RT seconds
      spin->setRange(low, high);     // clamped to the data bounds
      spin->setValue(std::clamp(value, low, high));
      spin->setSingleStep((high - low) / 100.0);
      return spin;
    };
    // RT spins display in the current unit; values are converted back to seconds
    // (canonical) before navigating.
    const double rtFactor = RtUnit::scale(rtInMinutes_);
    const int rtDecimals = rtInMinutes_ ? 4 : 3;
    auto* rtMin = makeSpin(view_.rtMin / rtFactor, dataBounds_.rtMin / rtFactor, dataBounds_.rtMax / rtFactor, rtDecimals);
    auto* rtMax = makeSpin(view_.rtMax / rtFactor, dataBounds_.rtMin / rtFactor, dataBounds_.rtMax / rtFactor, rtDecimals);
    auto* mzMin = makeSpin(view_.mzMin, dataBounds_.mzMin, dataBounds_.mzMax, 5);
    auto* mzMax = makeSpin(view_.mzMax, dataBounds_.mzMin, dataBounds_.mzMax, 5);
    form->addRow(tr("RT min (%1)").arg(RtUnit::unit(rtInMinutes_)), rtMin);
    form->addRow(tr("RT max (%1)").arg(RtUnit::unit(rtInMinutes_)), rtMax);
    form->addRow(tr("m/z min"), mzMin);
    form->addRow(tr("m/z max"), mzMax);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // Only navigate on a well-ordered, non-empty range (reject min>=max before
    // normalize() would silently expand an equal pair into a 1-unit interval).
    if (dialog.exec() == QDialog::Accepted
        && rtMax->value() > rtMin->value() && mzMax->value() > mzMin->value())
      applyRange({rtMin->value() * rtFactor, rtMax->value() * rtFactor,
                  mzMin->value(), mzMax->value()}, true);
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
    // The plot canvas and raster are deliberately the same size. Nearest pixel
    // painting avoids the blur and sampling seams caused by resizing a raster.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.fillRect(rect(), palette().window());
    const QRect area = plotRect();
    // Colormap floor so the plot ground matches the raster's "no data" colour and
    // there is no seam between the widget background and the rendered image.
    painter.fillRect(area, QColor::fromRgb(PeakMapRasterizer::color(0.0, colorMap_)));

    // Only draw the raster while it matches the current axis orientation; after an
    // axis swap it is skipped (floor shown) until the re-render lands, so we never
    // paint an image whose axes disagree with the view.
    if (!raster_.isNull() && rasterAxesSwapped_ == axesSwapped_)
    {
      const bool exactRasterView = raster_.size() == area.size()
        && rasterRange_.rtMin == view_.rtMin && rasterRange_.rtMax == view_.rtMax
        && rasterRange_.mzMin == view_.mzMin && rasterRange_.mzMax == view_.mzMax;
      if (exactRasterView)
      {
        // QPoint overload is a literal 1:1 copy: no source transform, filtering,
        // or fractional rectangle can introduce softened or skipped rows.
        painter.drawImage(area.topLeft(), raster_);
      }
      else
      {
        // Reproject the last raster into the CURRENT view so a pan/zoom shows a
        // tracking preview (shifted/scaled) until the throttled re-render lands.
        // Fractions map against the plot rect's continuous edges, so at rest
        // (rasterRange_==view_) the target is exactly `area`.
        QRectF target(area);
        QRectF source(raster_.rect());
        bool hasVisibleRaster = true;
        if (rasterRange_.isValid() && view_.rtSpan() > 0.0 && view_.mzSpan() > 0.0)
        {
          // Crop in data coordinates before projecting. During rapid zoom the old
          // raster may cover millions of current view widths; drawing that whole
          // image into an enormous QRectF can overflow a paint backend. Both the
          // source and destination rectangles below are always image/plot bounded.
          const PlotRange overlap{
            std::max(rasterRange_.rtMin, view_.rtMin),
            std::min(rasterRange_.rtMax, view_.rtMax),
            std::max(rasterRange_.mzMin, view_.mzMin),
            std::min(rasterRange_.mzMax, view_.mzMax)};
          hasVisibleRaster = overlap.isValid();
          if (hasVisibleRaster)
          {
            const auto rectFor = [](const PlotRange& selection, const PlotRange& bounds,
                                    const QSizeF& size, bool swapped)
            {
              if (swapped) // x = m/z, y = RT (inverted)
              {
                return QRectF(
                  (selection.mzMin - bounds.mzMin) / bounds.mzSpan() * size.width(),
                  (bounds.rtMax - selection.rtMax) / bounds.rtSpan() * size.height(),
                  selection.mzSpan() / bounds.mzSpan() * size.width(),
                  selection.rtSpan() / bounds.rtSpan() * size.height());
              }
              return QRectF(
                (selection.rtMin - bounds.rtMin) / bounds.rtSpan() * size.width(),
                (bounds.mzMax - selection.mzMax) / bounds.mzSpan() * size.height(),
                selection.rtSpan() / bounds.rtSpan() * size.width(),
                selection.mzSpan() / bounds.mzSpan() * size.height());
            };
            source = rectFor(overlap, rasterRange_, raster_.size(), axesSwapped_);
            target = rectFor(overlap, view_, area.size(), axesSwapped_);
            target.translate(area.topLeft());
          }
        }
        if (hasVisibleRaster)
        {
          painter.save();
          painter.setClipRect(area);
          painter.drawImage(target, raster_, source);
          painter.restore();
        }
      }
    }
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
      // MS level colours the marker (blue MS1, orange MS2+); an MS2 precursor adds
      // a perpendicular m/z line and a crosshair so the fragmented precursor is
      // located in both RT and m/z, not just its RT column.
      const QColor markerColor = selectedMsLevel_ >= 2 ? QColor(255, 140, 40) : QColor(90, 200, 255);
      const QPointF rtPoint = pixelFor(selectedRt_, (view_.mzMin + view_.mzMax) / 2.0);
      painter.setPen(QPen(markerColor, 1.5, Qt::DashLine));
      if (axesSwapped_) painter.drawLine(QPointF(area.left(), rtPoint.y()), QPointF(area.right(), rtPoint.y()));
      else painter.drawLine(QPointF(rtPoint.x(), area.top()), QPointF(rtPoint.x(), area.bottom()));

      const bool precursorVisible = selectedMarkerMz_
        && *selectedMarkerMz_ >= view_.mzMin && *selectedMarkerMz_ <= view_.mzMax;
      if (precursorVisible)
      {
        const QPointF cross = pixelFor(selectedRt_, *selectedMarkerMz_);
        painter.setPen(QPen(markerColor, 1.5, Qt::DashLine));
        if (axesSwapped_) painter.drawLine(QPointF(cross.x(), area.top()), QPointF(cross.x(), area.bottom()));
        else painter.drawLine(QPointF(area.left(), cross.y()), QPointF(area.right(), cross.y()));
        painter.setPen(QPen(markerColor, 1.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(cross, 5.0, 5.0);
      }

      const QString markerLabel = selectedMarkerMz_
        ? tr("MS%1 · precursor m/z %2").arg(selectedMsLevel_).arg(*selectedMarkerMz_, 0, 'f', 4)
        : tr("MS%1 · RT %2 %3").arg(selectedMsLevel_)
            .arg(RtUnit::format(selectedRt_, rtInMinutes_, 1)).arg(RtUnit::unit(rtInMinutes_));
      painter.setPen(markerColor);
      const int markerLabelWidth = painter.fontMetrics().horizontalAdvance(markerLabel);
      if (axesSwapped_)
        painter.drawText(QPointF(area.left() + 6, std::max<double>(area.top() + 12, rtPoint.y() - 5)), markerLabel);
      else
      {
        const double labelX = std::clamp<double>(rtPoint.x() + 6, area.left() + 4,
                                                 area.right() - markerLabelWidth - 4);
        painter.drawText(QPointF(labelX, area.top() + 14), markerLabel);
      }
    }

    // Pinned m/z reference (from SelectionController): a full-span line drawn
    // perpendicular to the m/z axis, plus its own label. A pure coordinate — no
    // dot, no intensity — so it never claims a peak exists where it crosses.
    if (selectedMz_ && *selectedMz_ >= view_.mzMin && *selectedMz_ <= view_.mzMax)
    {
      const QColor mzColor(0, 200, 180);  // teal — distinct from RT/precursor markers
      const QPointF mzPoint = pixelFor((view_.rtMin + view_.rtMax) / 2.0, *selectedMz_);
      painter.setPen(QPen(mzColor, 1.5, Qt::DashLine));
      if (axesSwapped_) painter.drawLine(QPointF(mzPoint.x(), area.top()), QPointF(mzPoint.x(), area.bottom()));
      else painter.drawLine(QPointF(area.left(), mzPoint.y()), QPointF(area.right(), mzPoint.y()));

      const QString mzLabel = tr("m/z %1").arg(*selectedMz_, 0, 'f', 4);
      painter.setPen(mzColor);
      const int mzLabelWidth = painter.fontMetrics().horizontalAdvance(mzLabel);
      if (axesSwapped_)
      {
        const double labelX = std::clamp<double>(mzPoint.x() + 6, area.left() + 4,
                                                 area.right() - mzLabelWidth - 4);
        painter.drawText(QPointF(labelX, area.bottom() - 6), mzLabel);
      }
      else
        painter.drawText(QPointF(area.left() + 6, std::max<double>(area.top() + 12, mzPoint.y() - 5)), mzLabel);
    }

    // Transient hover highlight: a ring on the peak under the cursor.
    if (hoveredPeak_)
    {
      const QPointF dot = pixelFor(hoveredPeak_->x(), hoveredPeak_->y());
      painter.setPen(QPen(QColor(255, 255, 255), 1.6));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(dot, 5.0, 5.0);
    }

    drawConsensus(painter);  // approximate multi-run context, under the precise overlays
    drawPrecursors(painter);
    drawFeatures(painter);
    drawIdentifications(painter);

    if (showMinimap_ && isZoomedIn() && !minimap_.isNull())
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

      // Mirror the selected-spectrum marker onto the minimap.
      if (hasSelectedRt_ && dataBounds_.rtSpan() > 0.0 && dataBounds_.mzSpan() > 0.0)
      {
        const double markerMz = selectedMarkerMz_ ? *selectedMarkerMz_
                                                   : (dataBounds_.mzMin + dataBounds_.mzMax) / 2.0;
        // Clamp to [0,1] so an out-of-bounds precursor never draws the dot outside
        // the minimap (onto the main plot); skip if the values are not finite.
        const double rtFrac = std::clamp((selectedRt_ - dataBounds_.rtMin) / dataBounds_.rtSpan(), 0.0, 1.0);
        const double mzFrac = std::clamp((markerMz - dataBounds_.mzMin) / dataBounds_.mzSpan(), 0.0, 1.0);
        if (std::isfinite(rtFrac) && std::isfinite(mzFrac))
        {
          const double markerX = std::clamp(
            mini.left() + (axesSwapped_ ? mzFrac : rtFrac) * mini.width(),
            static_cast<double>(mini.left()), static_cast<double>(mini.right()));
          const double markerY = std::clamp(
            mini.bottom() - (axesSwapped_ ? rtFrac : mzFrac) * mini.height(),
            static_cast<double>(mini.top()), static_cast<double>(mini.bottom()));
          const QColor markerColor = selectedMsLevel_ >= 2 ? QColor(255, 140, 40) : QColor(90, 200, 255);
          painter.setPen(QPen(markerColor, 1.2));
          painter.setBrush(markerColor);
          painter.drawEllipse(QPointF(markerX, markerY), 2.5, 2.5);
        }
      }

      // Mirror the pinned m/z line onto the minimap so both agree.
      if (selectedMz_ && dataBounds_.mzSpan() > 0.0)
      {
        const double mzFrac = (*selectedMz_ - dataBounds_.mzMin) / dataBounds_.mzSpan();
        if (std::isfinite(mzFrac) && mzFrac >= 0.0 && mzFrac <= 1.0)
        {
          painter.setPen(QPen(QColor(0, 200, 180), 1.2, Qt::DashLine));
          if (axesSwapped_)
          {
            const double x = mini.left() + mzFrac * mini.width();
            painter.drawLine(QPointF(x, mini.top()), QPointF(x, mini.bottom()));
          }
          else
          {
            const double y = mini.bottom() - mzFrac * mini.height();
            painter.drawLine(QPointF(mini.left(), y), QPointF(mini.right(), y));
          }
        }
      }
    }

    if (dragMode_ == DragMode::Zoom)
    {
      painter.setPen(QPen(QColor(80, 210, 255), 1.5, Qt::DashLine));
      painter.setBrush(QColor(80, 210, 255, 35));
      painter.drawRect(QRect(dragStart_, dragCurrent_).normalized().intersected(area));
    }
    else if (dragMode_ == DragMode::Measure)
    {
      // Endpoints snap to the nearest peak when zoomed in (issue #14); pixelFor()
      // maps the (possibly snapped) data point back to a pixel so the drawn line and
      // the ΔRT/Δm/z readout share exactly the same endpoints.
      const QPointF startData = measureEndpoint(dragStart_);
      const QPointF endData = measureEndpoint(dragCurrent_);
      const QPointF start = pixelFor(startData.x(), startData.y());
      const QPointF end = pixelFor(endData.x(), endData.y());
      painter.setPen(QPen(QColor(255, 215, 70), 2));
      painter.drawLine(start, end);
      painter.setBrush(QColor(255, 215, 70));
      painter.drawEllipse(start, 3, 3);
      painter.drawEllipse(end, 3, 3);
    }

    // Keep interaction guidance/readouts inside the peak-map canvas but away
    // from the measured data. Drawing it last also prevents the measurement
    // line from crossing through the fixed top-left badge.
    drawLegend(painter, area);
  }

  void PeakMapWidget::drawAxes(QPainter& painter, const QRect& area) const
  {
    painter.save();
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.drawRect(area);

    // RT ticks: compute "nice" numbers on the DISPLAY (minutes) range so labels are
    // round, then map each back to seconds for pixel positioning (factor == 1 in the
    // seconds case, so this is a no-op there).
    const double rtFactor = RtUnit::scale(rtInMinutes_);
    const auto rtTicksSeconds = [&]
    {
      auto ticks = PlotAxis::niceTicks(view_.rtMin / rtFactor, view_.rtMax / rtFactor, 7);
      for (double& tick : ticks) tick *= rtFactor;
      return ticks;
    }();
    const auto mzTicks = PlotAxis::niceTicks(view_.mzMin, view_.mzMax, 7);

    const auto& xTicks = axesSwapped_ ? mzTicks : rtTicksSeconds;
    const auto& yTicks = axesSwapped_ ? rtTicksSeconds : mzTicks;
    const double xMin = axesSwapped_ ? view_.mzMin : view_.rtMin;
    const double xSpan = axesSwapped_ ? view_.mzSpan() : view_.rtSpan();
    const double yMin = axesSwapped_ ? view_.rtMin : view_.mzMin;
    const double ySpan = axesSwapped_ ? view_.rtSpan() : view_.mzSpan();
    const bool xIsRt = !axesSwapped_;
    const bool yIsRt = axesSwapped_;

    // RT tick text is the value in display units (seconds ÷ factor), with precision
    // matched to the displayed span; m/z is labelled as before.
    const auto labelFor = [&](double value, double span, bool isRt)
    {
      return isRt ? tickLabel(value / rtFactor, span / rtFactor) : tickLabel(value, span);
    };

    painter.setPen(palette().color(QPalette::Text));
    for (double value : xTicks)
    {
      const int x = area.left() + static_cast<int>((value - xMin) / xSpan * area.width());
      painter.drawLine(x, area.bottom(), x, area.bottom() + 5);
      painter.drawText(QRect(x - 42, area.bottom() + 7, 84, 20), Qt::AlignHCenter | Qt::AlignTop,
                       labelFor(value, xSpan, xIsRt));
    }
    for (double value : yTicks)
    {
      const int y = area.bottom() - static_cast<int>((value - yMin) / ySpan * area.height());
      painter.drawLine(area.left() - 5, y, area.left(), y);
      painter.drawText(QRect(2, y - 10, area.left() - 10, 20), Qt::AlignRight | Qt::AlignVCenter,
                       labelFor(value, ySpan, yIsRt));
    }

    painter.drawText(QRect(area.left(), area.bottom() + 30, area.width(), 20), Qt::AlignCenter,
                     axesSwapped_ ? tr("m/z") : RtUnit::axisTitle(rtInMinutes_));
    painter.translate(16, area.center().y());
    painter.rotate(-90);
    painter.drawText(QRect(-area.height() / 2, -10, area.height(), 20), Qt::AlignCenter,
                     axesSwapped_ ? RtUnit::axisTitle(rtInMinutes_) : tr("m/z"));
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

  void PeakMapWidget::drawConsensus(QPainter& painter) const
  {
    if (!showConsensus_ || consensusFeatures_.empty()) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    const auto drawOne = [&](const ConsensusFeatureRecord& feature)
    {
      if (!std::isfinite(feature.rt) || !std::isfinite(feature.mz)) return false;
      // The envelope is usable only when all four bounds are finite AND ordered;
      // otherwise pixelFor() would receive a non-finite coordinate.
      const bool envValid = std::isfinite(feature.bounds.rtMin) && std::isfinite(feature.bounds.rtMax)
        && std::isfinite(feature.bounds.mzMin) && std::isfinite(feature.bounds.mzMax)
        && feature.bounds.rtMax >= feature.bounds.rtMin
        && feature.bounds.mzMax >= feature.bounds.mzMin;
      // Cull by the envelope when it has extent, else by the centroid alone.
      if (envValid)
      {
        if (feature.bounds.rtMax < view_.rtMin || feature.bounds.rtMin > view_.rtMax
            || feature.bounds.mzMax < view_.mzMin || feature.bounds.mzMin > view_.mzMax) return false;
      }
      else if (feature.rt < view_.rtMin || feature.rt > view_.rtMax
               || feature.mz < view_.mzMin || feature.mz > view_.mzMax) return false;

      const bool selected = selectedConsensus_ && *selectedConsensus_ == feature.index;
      // Amber, distinct from feature green and identification hues; selected turns
      // a bold coral so it reads against the amber field.
      const QColor color = selected ? QColor(255, 90, 90) : QColor(255, 170, 0);
      const int lineWidth = selected ? 2 : 1;

      // Dashed envelope: the bounding box of the per-map handle centroids. Dashing
      // signals it is an aggregate span, not a single feature's convex hull.
      if (envValid && (feature.bounds.rtMax > feature.bounds.rtMin
                       || feature.bounds.mzMax > feature.bounds.mzMin))
      {
        const QPointF corner = pixelFor(feature.bounds.rtMin, feature.bounds.mzMin);
        const QPointF opposite = pixelFor(feature.bounds.rtMax, feature.bounds.mzMax);
        QColor envelope = color;
        envelope.setAlpha(selected ? 220 : 130);
        painter.setPen(QPen(envelope, lineWidth, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(corner, opposite).normalized());
      }

      // Upward triangle centroid: features use circles and identifications use
      // diamonds, so a triangle keeps all three overlays distinguishable by shape
      // (not just colour) when shown together.
      const QPointF centre = pixelFor(feature.rt, feature.mz);
      const double radius = selected ? 6.5 : 4.5;
      QPolygonF triangle;
      triangle << QPointF(centre.x(), centre.y() - radius)
               << QPointF(centre.x() + radius * 0.9, centre.y() + radius * 0.7)
               << QPointF(centre.x() - radius * 0.9, centre.y() + radius * 0.7);
      painter.setPen(QPen(Qt::white, selected ? 2 : 1));
      painter.setBrush(color);
      painter.drawPolygon(triangle);
      return true;
    };

    // Draw the selected feature first so its highlight is guaranteed even when the
    // draw cap is reached before its position in the list (large consensus maps).
    std::optional<std::size_t> selectedPos;
    if (selectedConsensus_)
      for (std::size_t i = 0; i < consensusFeatures_.size(); ++i)
        if (consensusFeatures_[i].index == *selectedConsensus_) { selectedPos = i; break; }
    if (selectedPos) drawOne(consensusFeatures_[*selectedPos]);

    std::size_t drawn = 0;
    for (std::size_t i = 0; i < consensusFeatures_.size(); ++i)
    {
      if (drawn >= 10000) break;
      if (selectedPos && i == *selectedPos) continue;  // already drawn above
      if (drawOne(consensusFeatures_[i])) ++drawn;
    }
    painter.restore();
  }

  void PeakMapWidget::drawPrecursors(QPainter& painter) const
  {
    if (!showPrecursors_ || precursorMarkers_.empty()) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipRect(plotRect());
    std::size_t drawn = 0;
    for (std::size_t index = 0; index < precursorMarkers_.size(); ++index)
    {
      if (drawn >= 20000) break;
      const PrecursorMarker& marker = precursorMarkers_[index];
      if (!std::isfinite(marker.rt) || !std::isfinite(marker.mz)) continue;
      if (marker.rt < view_.rtMin || marker.rt > view_.rtMax
          || marker.upperMz < view_.mzMin || marker.lowerMz > view_.mzMax) continue;
      ++drawn;

      const bool hovered = hoveredPrecursor_ && *hoveredPrecursor_ == index;
      // Magenta so it reads against the viridis raster and stays distinct from the
      // green/orange/amber feature/id/consensus overlays.
      const QColor color = hovered ? QColor(255, 120, 220) : QColor(214, 90, 190);

      // Dashed isolation window: an m/z segment (low..high) at the scan RT. Its
      // screen orientation depends on the axis swap, so caps are drawn perpendicular
      // to the segment rather than assuming it is vertical.
      const bool hasWindow = marker.upperMz > marker.lowerMz;
      if (hasWindow)
      {
        const QPointF low = pixelFor(marker.rt, marker.lowerMz);
        const QPointF high = pixelFor(marker.rt, marker.upperMz);
        QColor windowColor = color;
        windowColor.setAlpha(hovered ? 230 : 150);
        painter.setPen(QPen(windowColor, hovered ? 1.6 : 1.0, Qt::DashLine));
        painter.drawLine(low, high);
        const QPointF along = high - low;
        const double length = std::hypot(along.x(), along.y());
        if (length > 1e-6)
        {
          const QPointF cap(-along.y() / length * 3.0, along.x() / length * 3.0);
          painter.drawLine(low - cap, low + cap);
          painter.drawLine(high - cap, high + cap);
        }
      }

      // Precursor position: a small filled circle at (RT, precursor m/z).
      const QPointF centre = pixelFor(marker.rt, marker.mz);
      const double radius = hovered ? 4.0 : 2.6;
      painter.setPen(QPen(Qt::white, hovered ? 2 : 1));
      painter.setBrush(color);
      painter.drawEllipse(centre, radius, radius);
    }
    painter.restore();
  }

  std::optional<std::size_t> PeakMapWidget::nearestPrecursor(const QPointF& position) const
  {
    if (!showPrecursors_ || precursorMarkers_.empty() || !plotRect().contains(position.toPoint()))
      return std::nullopt;
    double bestSquaredDistance = 14.0 * 14.0;
    std::optional<std::size_t> best;
    // Mirror drawPrecursors' iteration + cull + cap exactly so the hittable set is
    // always identical to the drawn set (drawn == clickable).
    std::size_t visible = 0;
    for (std::size_t index = 0; index < precursorMarkers_.size(); ++index)
    {
      if (visible >= 20000) break;
      const PrecursorMarker& marker = precursorMarkers_[index];
      if (!std::isfinite(marker.rt) || !std::isfinite(marker.mz)) continue;
      if (marker.rt < view_.rtMin || marker.rt > view_.rtMax
          || marker.upperMz < view_.mzMin || marker.lowerMz > view_.mzMax) continue;
      ++visible;
      const QPointF pixel = pixelFor(marker.rt, marker.mz);
      const double dx = position.x() - pixel.x();
      const double dy = position.y() - pixel.y();
      const double squaredDistance = dx * dx + dy * dy;
      if (squaredDistance < bestSquaredDistance)
      {
        bestSquaredDistance = squaredDistance;
        best = index;
      }
    }
    return best;
  }

  std::optional<std::size_t> PeakMapWidget::nearestFeature(const QPointF& position) const
  {
    // Hit-test whenever any feature overlay is visible (centroids OR bounds OR hulls),
    // so Edit-mode grab/move and hover work even when centroids alone are hidden.
    const bool anyFeatureOverlay = showFeatureCentroids_ || showFeatureBounds_ || showFeatureHulls_;
    if (!anyFeatureOverlay || features_.empty() || !plotRect().contains(position.toPoint()))
      return std::nullopt;
    const QPointF data = dataAt(position);
    double bestSquaredDistance = 15.0 * 15.0;
    std::optional<std::size_t> best;                          // nearest centroid within 15 px
    double bestEnclosingArea = std::numeric_limits<double>::max();
    std::optional<std::size_t> enclosing;                     // tightest bounds under the cursor
    const std::size_t limit = std::min<std::size_t>(features_.size(), 10000);
    for (std::size_t index = 0; index < limit; ++index)
    {
      const FeatureRecord& feature = features_[index];
      // Cull by bounding-box overlap so the clickable set matches the DRAWN set
      // (drawFeatures() culls the same way). A feature whose centroid is scrolled
      // off-screen but whose box/hull still covers the view stays hittable.
      if (feature.bounds.rtMax < view_.rtMin || feature.bounds.rtMin > view_.rtMax
          || feature.bounds.mzMax < view_.mzMin || feature.bounds.mzMin > view_.mzMax) continue;
      // Precise: distance to the centroid marker when it is itself on-screen.
      if (feature.rt >= view_.rtMin && feature.rt <= view_.rtMax
          && feature.mz >= view_.mzMin && feature.mz <= view_.mzMax)
      {
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
      // Fallback: cursor inside the feature's data-space bounds (matches the drawn
      // box/hull). Prefer the tightest enclosing feature when several nest.
      if (feature.bounds.rtMax > feature.bounds.rtMin && feature.bounds.mzMax > feature.bounds.mzMin
          && data.x() >= feature.bounds.rtMin && data.x() <= feature.bounds.rtMax
          && data.y() >= feature.bounds.mzMin && data.y() <= feature.bounds.mzMax)
      {
        const double area = feature.bounds.rtSpan() * feature.bounds.mzSpan();
        if (area < bestEnclosingArea) { bestEnclosingArea = area; enclosing = feature.index; }
      }
    }
    // A close centroid wins (precise); otherwise fall back to the enclosing box/hull.
    return best ? best : enclosing;
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
      : interactionMode_ == PeakMapInteractionMode::Measure ? tr("Measure")
      : interactionMode_ == PeakMapInteractionMode::Edit ? tr("Edit") : tr("Zoom");
    QString hint;
    if (dragMode_ == DragMode::Measure)
    {
      const QPointF start = measureEndpoint(dragStart_);
      const QPointF end = measureEndpoint(dragCurrent_);
      hint = tr("ΔRT %1 %2 · Δm/z %3")
               .arg(RtUnit::format(std::abs(end.x() - start.x()), rtInMinutes_))
               .arg(RtUnit::unit(rtInMinutes_))
               .arg(std::abs(end.y() - start.y()), 0, 'f', 4);
    }
    else if (interactionMode_ == PeakMapInteractionMode::Edit)
      hint = tr("Edit features · click empty = add · drag = move · dbl-click = edit · Del = delete");
    else
      hint = tr("%1 mode · wheel zoom · double-click reset").arg(mode);
    const int hintWidth = painter.fontMetrics().horizontalAdvance(hint) + 14;
    // The plot reserves a 20 px top margin. Keep this readout inside the
    // PeakMapWidget but outside the raster so it never hides a peak.
    const QRect hintRect(area.left() + 8, 1, hintWidth, 18);
    painter.setPen(QColor(230, 230, 235));
    painter.setBrush(QColor(0, 0, 0, 155));
    painter.drawRoundedRect(hintRect, 5, 5);
    painter.drawText(hintRect, Qt::AlignCenter, hint);

    int legendY = area.top() + 8;
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
      legendY += 18;
    }
    if (showConsensus_ && !consensusFeatures_.empty())
    {
      painter.setPen(Qt::white);
      painter.setBrush(QColor(255, 170, 0));
      const QPointF center(area.left() + 17, legendY + 6);
      QPolygonF triangle;
      triangle << QPointF(center.x(), center.y() - 5)
               << QPointF(center.x() + 4.5, center.y() + 4)
               << QPointF(center.x() - 4.5, center.y() + 4);
      painter.drawPolygon(triangle);
      painter.drawText(area.left() + 28, legendY + 12, tr("Consensus (dashed = envelope)"));
      legendY += 18;
    }
    if (showPrecursors_ && !precursorMarkers_.empty())
    {
      painter.setPen(QPen(Qt::white, 1));
      painter.setBrush(QColor(214, 90, 190));
      painter.drawEllipse(QPointF(area.left() + 17, legendY + 6), 3, 3);
      painter.drawText(area.left() + 28, legendY + 12, tr("MS/MS precursor (dashed = isolation)"));
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
    // Native cursor shapes render crisply at every DPI with correct hotspots and
    // match OS conventions; the toolbar SVGs stay on the toolbar buttons only.
    switch (interactionMode_)
    {
      case PeakMapInteractionMode::Zoom:
        setCursor(Qt::CrossCursor);
        break;
      case PeakMapInteractionMode::Pan:
        setCursor(Qt::OpenHandCursor);
        break;
      case PeakMapInteractionMode::Measure:
        setCursor(Qt::CrossCursor);
        break;
      case PeakMapInteractionMode::Edit:
        setCursor(Qt::PointingHandCursor);
        break;
    }
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

  std::optional<PeakMapWidget::NearestPeak> PeakMapWidget::nearestPeak(double rt, double mz) const
  {
    if (!experiment_ || experiment_->empty()) return std::nullopt;
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
    if (!nearest || nearest->empty()) return std::nullopt;
    auto peak = std::lower_bound(nearest->cbegin(), nearest->cend(), mz,
      [](const OpenMS::Peak1D& candidate, double value) { return candidate.getMZ() < value; });
    if (peak == nearest->cend()) --peak;
    else if (peak != nearest->cbegin())
    {
      const auto previous = peak - 1;
      if (std::abs(previous->getMZ() - mz) < std::abs(peak->getMZ() - mz)) peak = previous;
    }
    return NearestPeak{nearest->getRT(), peak->getMZ(), peak->getIntensity()};
  }

  double PeakMapWidget::nearestIntensity(double rt, double mz) const
  {
    const auto peak = nearestPeak(rt, mz);
    if (!peak) return -1.0;
    const double tolerance = view_.mzSpan() / std::max(1, plotRect().width()) * 5.0;
    return std::abs(peak->mz - mz) <= tolerance ? peak->intensity : -1.0;
  }

  std::optional<PeakMapWidget::NearestPeak> PeakMapWidget::resolvablePeak(double rt, double mz) const
  {
    if (view_.mzSpan() >= kSnapMaxMzSpan) return std::nullopt;
    const auto peak = nearestPeak(rt, mz);
    if (!peak) return std::nullopt;
    // Accept only when the peak sits within a small pixel radius of the cursor in
    // both dimensions — this rejects peaks in a distant scan (RT) or an empty m/z
    // neighbourhood, so a cursor over blank canvas resolves nothing.
    const QPointF cursorPx = pixelFor(rt, mz);
    const QPointF peakPx = pixelFor(peak->rt, peak->mz);
    const double dx = peakPx.x() - cursorPx.x();
    const double dy = peakPx.y() - cursorPx.y();
    if (dx * dx + dy * dy > kSnapPixelRadius * kSnapPixelRadius) return std::nullopt;
    return peak;
  }

  std::optional<QPointF> PeakMapWidget::snapToPeak(double rt, double mz) const
  {
    // Measure endpoints and click-commit snap only when the user opted in; the
    // hover highlight uses resolvablePeak() directly and ignores this gate.
    if (!snapToPeak_) return std::nullopt;
    const auto peak = resolvablePeak(rt, mz);
    if (!peak) return std::nullopt;
    return QPointF(peak->rt, peak->mz);
  }

  QPointF PeakMapWidget::measureEndpoint(const QPoint& pixel) const
  {
    const QPointF data = dataAt(pixel);
    if (const auto snapped = snapToPeak(data.x(), data.y())) return *snapped;
    return data;
  }

  void PeakMapWidget::resizeEvent(QResizeEvent* event)
  {
    QWidget::resizeEvent(event);
    if (experiment_ && raster_.size() != boundedRenderSize()) scheduleRender();
    else update();
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
    if (experiment_ && event->button() == Qt::LeftButton && showMinimap_ && isZoomedIn()
        && !minimap_.isNull() && minimapRect().contains(event->position().toPoint()))
    {
      // Click (and hold-drag) inside the minimap recenters the view. Push one
      // history entry at drag start so a single Zoom-back undoes the whole gesture.
      draggingMinimap_ = true;
      rememberCurrentRange();
      recenterFromMinimap(event->position());
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
    else if (interactionMode_ == PeakMapInteractionMode::Edit)
    {
      // Grab the feature under the cursor to move it; empty space starts a create.
      dragMode_ = DragMode::Edit;
      editFeatureIndex_ = nearestFeature(event->position());
      if (editFeatureIndex_) selectedFeature_ = editFeatureIndex_;
    }
    else dragMode_ = DragMode::Zoom;
    event->accept();
  }

  void PeakMapWidget::mouseMoveEvent(QMouseEvent* event)
  {
    if (draggingMinimap_)
    {
      recenterFromMinimap(event->position());  // live pan while the button is held
      event->accept();
      return;
    }
    if (experiment_ && plotRect().contains(event->position().toPoint()))
    {
      const QPointF position = dataAt(event->position());
      // Highlight the peak under the cursor while idly viewing (Zoom/Pan). This
      // resolves regardless of the snap-to-peak toggle — a read-only peek costs
      // nothing. When a peak is resolved the readout is its exact triple; off any
      // peak (or zoomed out) it is the raw cursor with no intensity.
      const bool hoverPeakMode = dragMode_ == DragMode::None
        && (interactionMode_ == PeakMapInteractionMode::Zoom
            || interactionMode_ == PeakMapInteractionMode::Pan);
      std::optional<QPointF> resolved;
      if (hoverPeakMode)
      {
        if (const auto peak = resolvablePeak(position.x(), position.y()))
        {
          resolved = QPointF(peak->rt, peak->mz);
          emit cursorPositionChanged(peak->rt, peak->mz, peak->intensity);
        }
        else emit cursorPositionChanged(position.x(), position.y(), -1.0);
      }
      else
      {
        emit cursorPositionChanged(position.x(), position.y(),
                                   nearestIntensity(position.x(), position.y()));
      }
      if (resolved != hoveredPeak_) { hoveredPeak_ = resolved; update(); }
    }
    if (dragMode_ == DragMode::None)
    {
      const auto nearest = nearestFeature(event->position());
      const auto nearestId = nearest ? std::optional<std::size_t>{} : nearestIdentification(event->position());
      const auto nearestPre = (nearest || nearestId) ? std::optional<std::size_t>{}
                                                     : nearestPrecursor(event->position());
      if (nearest != hoveredFeature_ || nearestId != hoveredIdentification_
          || nearestPre != hoveredPrecursor_)
      {
        hoveredFeature_ = nearest;
        hoveredIdentification_ = nearestId;
        hoveredPrecursor_ = nearestPre;
        if (nearest && *nearest < features_.size())
        {
          const FeatureRecord& feature = features_[*nearest];
          setToolTip(tr("Feature #%1\nRT %2 %3 · m/z %4\nIntensity %5 · charge %6")
                       .arg(feature.index)
                       .arg(RtUnit::format(feature.rt, rtInMinutes_))
                       .arg(RtUnit::unit(rtInMinutes_))
                       .arg(feature.mz, 0, 'f', 4)
                       .arg(feature.intensity, 0, 'e', 2)
                       .arg(feature.charge));
          setCursor(Qt::PointingHandCursor);
        }
        else if (nearestId && *nearestId < identifications_.size())
        {
          const IdentificationRecord& identification = identifications_[*nearestId];
          const QString sequence = identification.bestHit() ? identification.bestHit()->sequence : tr("No hit");
          setToolTip(tr("Identification #%1\nRT %2 %3 · m/z %4\n%5")
                       .arg(identification.index)
                       .arg(RtUnit::format(identification.rt, rtInMinutes_))
                       .arg(RtUnit::unit(rtInMinutes_))
                       .arg(identification.mz, 0, 'f', 4)
                       .arg(sequence));
          setCursor(Qt::PointingHandCursor);
        }
        else if (nearestPre && *nearestPre < precursorMarkers_.size())
        {
          const PrecursorMarker& marker = precursorMarkers_[*nearestPre];
          QString window = tr("no isolation window");
          if (marker.upperMz > marker.lowerMz)
            window = tr("window %1–%2").arg(marker.lowerMz, 0, 'f', 2).arg(marker.upperMz, 0, 'f', 2);
          setToolTip(tr("MS%1 precursor · scan #%2\nRT %3 %4 · m/z %5 · charge %6\n%7")
                       .arg(marker.msLevel)
                       .arg(marker.spectrumIndex + 1)
                       .arg(RtUnit::format(marker.rt, rtInMinutes_))
                       .arg(RtUnit::unit(rtInMinutes_))
                       .arg(marker.mz, 0, 'f', 4)
                       .arg(marker.charge)
                       .arg(window));
          setCursor(Qt::PointingHandCursor);
        }
        else
        {
          setToolTip({});
          updateInteractionCursor();
        }
        update();
      }
      // leaveEvent unset the cursor; when the pointer re-enters over empty plot
      // area the hover state is unchanged (nothing under it), so the block above
      // is skipped and the crosshair/open-hand cursor is never reapplied. Restore
      // it here. Runs once: updateInteractionCursor() sets a non-arrow shape.
      else if (!hoveredFeature_ && !hoveredIdentification_ && !hoveredPrecursor_
               && cursor().shape() == Qt::ArrowCursor)
      {
        updateInteractionCursor();
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
    if (draggingMinimap_ && event->button() == Qt::LeftButton)
    {
      draggingMinimap_ = false;
      event->accept();
      return;
    }
    if (event->button() != Qt::LeftButton || dragMode_ == DragMode::None)
    {
      QWidget::mouseReleaseEvent(event);
      return;
    }
    dragCurrent_ = event->position().toPoint();
    const DragMode completedMode = dragMode_;
    dragMode_ = DragMode::None;
    updateInteractionCursor();

    if (completedMode == DragMode::Pan)
    {
      // A plain click (no real drag) in Pan mode changed nothing, but the press
      // already pushed a history entry — drop it so the first Zoom-back is not a
      // visible no-op.
      if ((dragCurrent_ - dragStart_).manhattanLength() < 6 && !history_.empty())
      {
        history_.pop_back();
        emit zoomHistoryChanged(!history_.empty());
      }
    }
    else if (completedMode == DragMode::Zoom)
    {
      const QRect selection = QRect(dragStart_, dragCurrent_).normalized().intersected(plotRect());
      // Accept a thin band (narrow-but-tall or wide-but-short) as an axis-band zoom:
      // a long drag in one axis is a deliberate "zoom this RT/m-z range" gesture.
      if (selection.width() >= 8 || selection.height() >= 8)
      {
        const QPointF first = dataAt(selection.topLeft());
        const QPointF second = dataAt(selection.bottomRight());
        applyRange({std::min(first.x(), second.x()), std::max(first.x(), second.x()),
                    std::min(first.y(), second.y()), std::max(first.y(), second.y())}, true);
      }
      else if ((dragCurrent_ - dragStart_).manhattanLength() < 6)
      {
        // Two independent decisions: `specificNavigation` suppresses the generic
        // nearest-RT jump; `overlayHit` suppresses the m/z commit. They differ —
        // a feature click still navigates by RT but must NOT pin a raster m/z.
        bool specificNavigation = false;
        bool overlayHit = false;
        if (const auto feature = nearestFeature(dragCurrent_))
        {
          selectedFeature_ = feature;
          emit featureActivated(*feature);
          overlayHit = true;
        }
        else if (const auto identification = nearestIdentification(dragCurrent_))
        {
          selectedIdentification_ = identification;
          emit identificationActivated(*identification);
          specificNavigation = true;
          overlayHit = true;
        }
        else if (const auto precursor = nearestPrecursor(dragCurrent_))
        {
          emit precursorActivated(precursorMarkers_[*precursor].spectrumIndex);
          specificNavigation = true;
          overlayHit = true;
        }
        // Identification / precursor activation already navigates to a specific MS/MS
        // scan. A generic nearest-RT activation would often replace it with an
        // adjacent MS1 scan.
        if (!specificNavigation) emit rtActivated(dataAt(dragCurrent_).x());
        // Commit the clicked m/z (only for a plain raster click, never an overlay
        // activation): the snapped peak when snapping is on and resolvable, the raw
        // cursor m/z when snapping is off, or a clear when snapping on but off any peak.
        if (!overlayHit)
        {
          const QPointF data = dataAt(dragCurrent_);
          if (!snapToPeak_) emit mzActivated(data.y());
          else if (const auto snapped = snapToPeak(data.x(), data.y())) emit mzActivated(snapped->y());
          else emit mzCleared();
        }
      }
    }
    else if (completedMode == DragMode::Edit)
    {
      const bool moved = (dragCurrent_ - dragStart_).manhattanLength() >= 6;
      const QPointF data = dataAt(dragCurrent_);
      if (editFeatureIndex_)  // grabbed an existing feature
      {
        if (moved) emit featureMoveRequested(*editFeatureIndex_, data.x(), data.y());
        else emit featureActivated(*editFeatureIndex_);  // a plain click just selects it
      }
      else if (!moved)  // clicked empty space → create, unless a double-click follows
      {
        pendingCreateData_ = data;
        editCreateTimer_.start(QApplication::doubleClickInterval());
      }
      editFeatureIndex_.reset();
    }
    update();
    event->accept();
  }

  void PeakMapWidget::mouseDoubleClickEvent(QMouseEvent* event)
  {
    if (event->button() == Qt::LeftButton && plotRect().contains(event->position().toPoint()))
    {
      // In Edit mode a double-click on a feature opens its edit dialog; the pending
      // press started an Edit drag and the first release armed a create — cancel both.
      if (interactionMode_ == PeakMapInteractionMode::Edit)
      {
        dragMode_ = DragMode::None;
        editFeatureIndex_.reset();
        editCreateTimer_.stop();  // this was a double-click, not a create
        if (const auto feature = nearestFeature(event->position()))
        {
          selectedFeature_ = feature;
          emit featureEditRequested(*feature);
          event->accept();
          return;
        }
      }
      resetView();
      event->accept();
      return;
    }
    QWidget::mouseDoubleClickEvent(event);
  }

  void PeakMapWidget::enterEvent(QEnterEvent* event)
  {
    updateInteractionCursor();
    QWidget::enterEvent(event);
  }

  void PeakMapWidget::leaveEvent(QEvent* event)
  {
    hoveredFeature_.reset();
    hoveredIdentification_.reset();
    hoveredPrecursor_.reset();
    hoveredPeak_.reset();
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
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && interactionMode_ == PeakMapInteractionMode::Edit && selectedFeature_)
    {
      emit featureDeleteRequested(*selectedFeature_);
    }
    else if (event->key() == Qt::Key_G) showGoToRangeDialog();
    else if (event->key() == Qt::Key_Z) setInteractionMode(0);
    else if (event->key() == Qt::Key_P) setInteractionMode(1);
    else if (event->key() == Qt::Key_M) setInteractionMode(2);
    else if (event->key() == Qt::Key_E) setInteractionMode(3);
    else
    {
      QWidget::keyPressEvent(event);
      return;
    }
    event->accept();
  }
}
