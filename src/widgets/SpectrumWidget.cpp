#include "widgets/SpectrumWidget.h"

#include "model/RtUnit.h"

#include <QPainter>
#include <QFontMetrics>
#include <QInputDialog>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <vector>

namespace OpenMSViewer
{
  namespace
  {
    QColor ionColor(IonType type)
    {
      switch (type)
      {
        case IonType::B: return QColor(31, 119, 180);
        case IonType::Y: return QColor(214, 39, 40);
        case IonType::A: return QColor(44, 160, 44);
        case IonType::C: return QColor(148, 103, 189);
        case IonType::X: return QColor(140, 86, 75);
        case IonType::Z: return QColor(227, 119, 194);
        case IonType::Precursor: return QColor(255, 127, 14);
        case IonType::Unknown: return QColor(145, 145, 155);
      }
      return QColor(145, 145, 155);
    }

    QString ionTypeName(IonType type)
    {
      switch (type)
      {
        case IonType::A: return QStringLiteral("a");
        case IonType::B: return QStringLiteral("b");
        case IonType::C: return QStringLiteral("c");
        case IonType::X: return QStringLiteral("x");
        case IonType::Y: return QStringLiteral("y");
        case IonType::Z: return QStringLiteral("z");
        case IonType::Precursor: return QStringLiteral("[M]");
        case IonType::Unknown: return QStringLiteral("?");
      }
      return QStringLiteral("?");
    }
  }

  SpectrumWidget::SpectrumWidget(QWidget* parent) : QWidget(parent)
  {
    setMinimumSize(400, 220);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Mass spectrum"));
    setAccessibleDescription(
      tr("Interactive mass spectrum. Use the wheel or drag to zoom and the context menu to reset or export."));
  }

  void SpectrumWidget::setExperiment(std::shared_ptr<const OpenMS::MSExperiment> experiment)
  {
    experiment_ = std::move(experiment);
    standaloneSpectrum_.reset();
    spectrumIndex_ = 0;
    totalSpectra_ = experiment_ ? experiment_->size() : 0;
    annotation_.reset();
    mzView_.reset();
    hoveredPeak_.reset();
    measurementStart_.reset();
    measurements_.clear();
    peakLabels_.clear();
    selectedMeasurement_.reset();
    update();
  }

  void SpectrumWidget::setSpectrumIndex(std::size_t index)
  {
    if (!experiment_ || index >= experiment_->size()) return;
    standaloneSpectrum_.reset();
    spectrumIndex_ = index;
    annotation_.reset();
    hoveredPeak_.reset();
    measurementStart_.reset();
    selectedMeasurement_.reset();
    if (mzView_)
    {
      const auto full = fullMzRange();
      applyMzView(std::max(mzView_->first, full.first),
                  std::min(mzView_->second, full.second));
    }
    update();
  }

  void SpectrumWidget::setStandaloneSpectrum(OpenMS::MSSpectrum spectrum, std::size_t index,
                                             std::size_t totalSpectra)
  {
    experiment_.reset();
    standaloneSpectrum_ = std::move(spectrum);
    spectrumIndex_ = index;
    totalSpectra_ = totalSpectra;
    annotation_.reset();
    mzView_.reset();
    hoveredPeak_.reset();
    measurementStart_.reset();
    measurements_.clear();
    peakLabels_.clear();
    selectedMeasurement_.reset();
    update();
  }

  void SpectrumWidget::clear()
  {
    experiment_.reset();
    standaloneSpectrum_.reset();
    annotation_.reset();
    spectrumIndex_ = 0;
    totalSpectra_ = 0;
    mzView_.reset();
    hoveredPeak_.reset();
    measurementStart_.reset();
    measurements_.clear();
    peakLabels_.clear();
    selectedMeasurement_.reset();
    update();
  }

  void SpectrumWidget::setRelativeIntensity(bool relative)
  {
    relativeIntensity_ = relative;
    update();
  }

  void SpectrumWidget::setAnnotation(std::optional<SpectrumAnnotation> annotation)
  {
    annotation_ = std::move(annotation);
    hoveredPeak_.reset();   // force the annotation-aware tooltip to rebuild on next hover
    setToolTip({});
    update();
  }

  void SpectrumWidget::setAnnotationEnabled(bool enabled)
  {
    annotationEnabled_ = enabled;
    hoveredPeak_.reset();
    setToolTip({});
    update();
  }

  void SpectrumWidget::setMirrorMode(bool enabled)
  {
    mirrorMode_ = enabled;
    update();
  }

  void SpectrumWidget::setShowUnmatchedTheoretical(bool show)
  {
    showUnmatchedTheoretical_ = show;
    update();
  }

  void SpectrumWidget::setMeasurementMode(bool enabled)
  {
    measurementMode_ = enabled;
    if (enabled) labelMode_ = false;  // the two click tools are mutually exclusive
    measurementStart_.reset();
    setCursor(measurementMode_ || labelMode_ ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
  }

  void SpectrumWidget::setLabelMode(bool enabled)
  {
    labelMode_ = enabled;
    if (enabled) measurementMode_ = false;  // the two click tools are mutually exclusive
    measurementStart_.reset();
    setCursor(measurementMode_ || labelMode_ ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
  }

  void SpectrumWidget::setShowMzLabels(bool show)
  {
    showMzLabels_ = show;
    update();
  }

  void SpectrumWidget::setShowGrid(bool show)
  {
    showGrid_ = show;
    update();
  }

  void SpectrumWidget::setAutoYScale(bool enabled)
  {
    autoYScale_ = enabled;
    update();
  }

  void SpectrumWidget::setRtInMinutes(bool minutes)
  {
    if (rtInMinutes_ == minutes) return;
    rtInMinutes_ = minutes;
    update();
  }

  void SpectrumWidget::clearMeasurements()
  {
    measurements_.erase(spectrumIndex_);
    measurementStart_.reset();
    selectedMeasurement_.reset();
    update();
  }

  void SpectrumWidget::clearLabels()
  {
    peakLabels_.erase(spectrumIndex_);
    update();
  }

  void SpectrumWidget::resetMzView()
  {
    const auto full = fullMzRange();
    mzView_.reset();
    emit mzViewChanged(full.first, full.second, true);
    update();
  }

  std::size_t SpectrumWidget::spectrumIndex() const noexcept { return spectrumIndex_; }
  const std::optional<SpectrumAnnotation>& SpectrumWidget::annotation() const noexcept { return annotation_; }
  bool SpectrumWidget::measurementMode() const noexcept { return measurementMode_; }
  bool SpectrumWidget::labelMode() const noexcept { return labelMode_; }
  std::optional<std::pair<double, double>> SpectrumWidget::mzView() const noexcept { return mzView_; }

  const std::vector<PeakLabel>& SpectrumWidget::labels() const noexcept
  {
    static const std::vector<PeakLabel> empty;
    const auto found = peakLabels_.find(spectrumIndex_);
    return found == peakLabels_.end() ? empty : found->second;
  }

  const std::vector<SpectrumMeasurement>& SpectrumWidget::measurements() const noexcept
  {
    static const std::vector<SpectrumMeasurement> empty;
    const auto found = measurements_.find(spectrumIndex_);
    return found == measurements_.end() ? empty : found->second;
  }

  const OpenMS::MSSpectrum* SpectrumWidget::currentSpectrum() const noexcept
  {
    if (standaloneSpectrum_) return &*standaloneSpectrum_;
    if (!experiment_ || spectrumIndex_ >= experiment_->size()) return nullptr;
    return &(*experiment_)[spectrumIndex_];
  }

  QRect SpectrumWidget::plotRect() const { return rect().adjusted(62, 42, -18, -42); }

  std::pair<double, double> SpectrumWidget::fullMzRange() const
  {
    const auto* spectrum = currentSpectrum();
    if (!spectrum || spectrum->empty()) return {0.0, 1.0};
    double minimum = spectrum->front().getMZ();
    double maximum = spectrum->back().getMZ();
    if (annotation_)
    {
      for (const TheoreticalIon& ion : annotation_->unmatched)
      {
        minimum = std::min(minimum, ion.mz);
        maximum = std::max(maximum, ion.mz);
      }
    }
    if (maximum <= minimum) maximum = minimum + 1.0;
    return {minimum, maximum};
  }

  void SpectrumWidget::applyMzView(double minimumMz, double maximumMz, bool reset)
  {
    const auto full = fullMzRange();
    minimumMz = std::clamp(minimumMz, full.first, full.second);
    maximumMz = std::clamp(maximumMz, full.first, full.second);
    if (maximumMz <= minimumMz || maximumMz - minimumMz < (full.second - full.first) * 1e-8)
      return;
    if (reset || (minimumMz <= full.first && maximumMz >= full.second)) mzView_.reset();
    else mzView_ = std::pair{minimumMz, maximumMz};
    emit mzViewChanged(minimumMz, maximumMz, reset || !mzView_);
    update();
  }

  std::optional<std::pair<double, double>> SpectrumWidget::peakAt(const QPointF& position) const
  {
    const auto* spectrum = currentSpectrum();
    const QRect area = plotRect();
    if (!spectrum || spectrum->empty() || !area.contains(position.toPoint())) return std::nullopt;
    const auto full = fullMzRange();
    const double minimum = mzView_ ? mzView_->first : full.first;
    const double maximum = mzView_ ? mzView_->second : full.second;
    // Reuse the vertical scaling captured by the most recent paint so the cursor's
    // y position can disambiguate overlapping sticks (2D snap in pixel space)
    // against exactly what is on screen — no per-move rescan of the spectrum.
    const double baseline = plotBaseline_;
    const double positiveHeight = plotPositiveHeight_;
    const double intensityMax = plotIntensityMax_;

    const auto xForMz = [&](double mz)
    {
      return area.left() + (mz - minimum) / (maximum - minimum) * area.width();
    };
    const auto yForIntensity = [&](double intensity)
    {
      return baseline - intensity / intensityMax * positiveHeight;
    };

    constexpr double pixelTolerance = 10.0;
    const double target = minimum + (position.x() - area.left()) / area.width() * (maximum - minimum);
    const auto insertion = std::lower_bound(spectrum->begin(), spectrum->end(), target,
      [](const OpenMS::Peak1D& peak, double value) { return peak.getMZ() < value; });

    // Among the sticks inside the horizontal snap window, choose the one nearest
    // the cursor in 2D pixel space so pointing high selects the tall neighbour.
    const OpenMS::Peak1D* best = nullptr;
    double bestDistance = 0.0;
    const auto consider = [&](const OpenMS::Peak1D& peak)
    {
      // Ignore peaks outside the visible m/z range so hit-testing never selects a
      // stick the paint pass culled; peaks are sorted, so this also stops the scan.
      if (peak.getMZ() < minimum || peak.getMZ() > maximum) return false;
      const double dx = xForMz(peak.getMZ()) - position.x();
      if (std::abs(dx) > pixelTolerance) return false;
      const double dy = yForIntensity(peak.getIntensity()) - position.y();
      const double distance = dx * dx + dy * dy;
      if (!best || distance < bestDistance) { best = &peak; bestDistance = distance; }
      return true;
    };
    for (auto it = insertion; it != spectrum->end(); ++it) if (!consider(*it)) break;
    for (auto it = insertion; it != spectrum->begin();) { --it; if (!consider(*it)) break; }
    if (!best) return std::nullopt;
    return std::pair{static_cast<double>(best->getMZ()),
                     static_cast<double>(best->getIntensity())};
  }

  std::optional<std::size_t> SpectrumWidget::measurementAt(const QPointF& position) const
  {
    const auto& list = measurements();
    const auto* spectrum = currentSpectrum();
    if (list.empty() || !spectrum || spectrum->empty()) return std::nullopt;
    const QRect area = plotRect();
    const auto full = fullMzRange();
    const double mzMin = mzView_ ? mzView_->first : full.first;
    const double mzMax = mzView_ ? mzView_->second : full.second;

    // Recompute the exact scaling paintEvent uses (a click is not perf-critical),
    // so hit-testing matches the current view rather than a possibly stale cache.
    const bool mirror = annotationEnabled_ && annotation_
                        && !annotation_->sequence.isEmpty() && mirrorMode_;
    const double baseline = mirror ? area.center().y() : area.bottom();
    const double positiveHeight = mirror ? area.height() * 0.45 : area.height() * 0.95;
    double viewMax = 0.0;
    double baseMax = 0.0;
    for (const auto& peak : *spectrum)
    {
      const double intensity = peak.getIntensity();
      baseMax = std::max(baseMax, intensity);
      if (peak.getMZ() >= mzMin && peak.getMZ() <= mzMax) viewMax = std::max(viewMax, intensity);
    }
    if (baseMax <= 0.0) baseMax = 1.0;
    if (viewMax <= 0.0) viewMax = baseMax;
    const double intensityMax = (mirror || autoYScale_) ? viewMax : baseMax;
    const auto xForMz = [&](double mz)
    {
      return area.left() + (mz - mzMin) / (mzMax - mzMin) * area.width();
    };
    const auto yForIntensity = [&](double intensity)
    {
      return baseline - intensity / intensityMax * positiveHeight;
    };

    // Hit-test the horizontal bracket bar; only brackets paintEvent actually draws
    // (both endpoints inside the visible range) are selectable.
    std::optional<std::size_t> best;
    double bestDy = 7.0;
    for (std::size_t index = 0; index < list.size(); ++index)
    {
      const SpectrumMeasurement& measurement = list[index];
      if (measurement.firstMz < mzMin || measurement.firstMz > mzMax
          || measurement.secondMz < mzMin || measurement.secondMz > mzMax) continue;
      const double firstX = xForMz(measurement.firstMz);
      const double secondX = xForMz(measurement.secondMz);
      const double firstY = yForIntensity(measurement.firstIntensity);
      const double secondY = yForIntensity(measurement.secondIntensity);
      const double bracketY = std::max<double>(area.top() + 22,
        std::min(firstY, secondY) - std::max(18, area.height() / 14));
      if (position.x() < std::min(firstX, secondX) - 3 || position.x() > std::max(firstX, secondX) + 3)
        continue;
      const double dy = std::abs(position.y() - bracketY);
      if (dy <= bestDy) { bestDy = dy; best = index; }
    }
    return best;
  }

  void SpectrumWidget::editLabelAt(const std::pair<double, double>& peak)
  {
    // Capture context and hold NO references/iterators across the modal dialog:
    // its nested event loop can deliver a background load or spectrum change that
    // mutates or clears peakLabels_ and would otherwise dangle these.
    const std::size_t targetSpectrum = spectrumIndex_;
    QString current;
    if (const auto found = peakLabels_.find(targetSpectrum); found != peakLabels_.end())
    {
      const auto existing = std::find_if(found->second.begin(), found->second.end(),
        [&](const PeakLabel& label) { return std::abs(label.mz - peak.first) <= 1e-3; });
      if (existing != found->second.end()) current = existing->text;
    }

    bool accepted = false;
    const QString text = QInputDialog::getText(this, tr("Peak label"),
      tr("Label for m/z %1 (clear to remove):").arg(peak.first, 0, 'f', 4),
      QLineEdit::Normal, current, &accepted).trimmed();
    if (!accepted) return;                          // cancelled — leave labels untouched
    if (spectrumIndex_ != targetSpectrum) return;   // spectrum changed under the dialog

    // Re-find after the dialog; the store may have changed while it was open.
    auto& list = peakLabels_[targetSpectrum];
    const auto existing = std::find_if(list.begin(), list.end(),
      [&](const PeakLabel& label) { return std::abs(label.mz - peak.first) <= 1e-3; });
    if (text.isEmpty())
    {
      if (existing != list.end()) list.erase(existing);   // clearing the text removes it
    }
    else if (existing != list.end())
    {
      existing->text = text;
    }
    else
    {
      list.push_back({peak.first, peak.second, text});
    }
    if (list.empty()) peakLabels_.erase(targetSpectrum);   // never leave an empty entry
    update();
  }

  void SpectrumWidget::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), palette().window());
    const QRect area = plotRect();

    const OpenMS::MSSpectrum* selectedSpectrum = currentSpectrum();
    if (!selectedSpectrum)
    {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(area, Qt::AlignCenter, tr("No spectrum selected"));
      return;
    }

    const auto& spectrum = *selectedSpectrum;
    if (spectrum.empty())
    {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(area, Qt::AlignCenter, tr("Selected spectrum has no peaks"));
      return;
    }

    const bool annotated = annotationEnabled_ && annotation_ && !annotation_->sequence.isEmpty();
    const bool mirror = annotated && mirrorMode_;
    const int baseline = mirror ? area.center().y() : area.bottom();
    const double positiveHeight = mirror ? area.height() * 0.45 : area.height() * 0.95;

    painter.setPen(palette().color(QPalette::Mid));
    painter.drawLine(area.left(), baseline, area.right(), baseline);
    painter.drawLine(area.left(), area.top(), area.left(), area.bottom());

    // Faint dotted gridlines at the axis ticks — the same grid the TIC/chromatogram
    // plots use and the classic TOPPView spectrum look — drawn behind the peaks so
    // the sticks stay crisp on top. Interior lines only; the frame edges and the
    // baseline are already the solid axis lines above.
    if (showGrid_)
    {
      painter.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DotLine));
      for (int tick = 1; tick < 5; ++tick)
      {
        const int x = area.left() + tick * area.width() / 5;
        painter.drawLine(x, area.top(), x, area.bottom());
      }
      if (!mirror)
        for (int tick = 1; tick <= 4; ++tick)
        {
          const int y = baseline - tick * static_cast<int>(positiveHeight) / 4;
          painter.drawLine(area.left(), y, area.right(), y);
        }
    }

    const auto fullRange = fullMzRange();
    const double mzMin = mzView_ ? mzView_->first : fullRange.first;
    const double mzMax = mzView_ ? mzView_->second : fullRange.second;
    double viewIntensityMax = 0.0;
    double baseIntensityMax = 0.0;
    for (const auto& peak : spectrum)
    {
      const double intensity = static_cast<double>(peak.getIntensity());
      baseIntensityMax = std::max(baseIntensityMax, intensity);
      if (peak.getMZ() >= mzMin && peak.getMZ() <= mzMax)
        viewIntensityMax = std::max(viewIntensityMax, intensity);
    }
    if (baseIntensityMax <= 0.0) baseIntensityMax = 1.0;
    if (viewIntensityMax <= 0.0) viewIntensityMax = baseIntensityMax;
    // Intensity mapped to the top of the plot: the visible-range maximum when
    // auto-scaling (default) or in a butterfly view, otherwise the whole-spectrum
    // base peak so absolute heights stay comparable when zoomed or across spectra.
    const double intensityMax = (mirror || autoYScale_) ? viewIntensityMax : baseIntensityMax;
    plotBaseline_ = baseline;
    plotPositiveHeight_ = positiveHeight;
    plotIntensityMax_ = intensityMax;

    const bool darkTheme = palette().color(QPalette::Window).lightnessF() < 0.5;
    const QColor stickColor = darkTheme ? QColor(35, 190, 225) : QColor(15, 110, 150);

    const auto xForMz = [&](double mz)
    {
      return area.left() + static_cast<int>((mz - mzMin) / (mzMax - mzMin) * area.width());
    };
    const auto yForIntensity = [&](double intensity)
    {
      return baseline - static_cast<int>(intensity / intensityMax * positiveHeight);
    };

    // Shared matched-ion label placement: prioritise tall peaks, cap the count,
    // and nudge away from collisions — upward for the experimental (top) trace,
    // downward for the mirrored theoretical (bottom) trace.
    struct StickLabel { int x; int yTip; QString text; QColor color; double priority; };
    auto drawStickLabels = [&](std::vector<StickLabel> items, bool above)
    {
      std::sort(items.begin(), items.end(),
        [](const StickLabel& left, const StickLabel& right) { return left.priority > right.priority; });
      if (items.size() > 50) items.resize(50);
      painter.setFont(QFont(painter.font().family(), std::max(7, font().pointSize() - 2)));
      std::vector<QRect> used;
      for (const StickLabel& item : items)
      {
        const QSize textSize = painter.fontMetrics().size(Qt::TextSingleLine, item.text);
        const int top = above ? item.yTip - textSize.height() - 3 : item.yTip + 3;
        QRect labelRect(QPoint(item.x + 3, top), textSize + QSize(4, 2));
        if (labelRect.right() > area.right()) labelRect.moveRight(area.right());  // keep inside plot
        const int step = above ? -(textSize.height() + 2) : (textSize.height() + 2);
        for (int attempt = 0; attempt < 3
             && std::any_of(used.cbegin(), used.cend(),
               [&labelRect](const QRect& u) { return u.intersects(labelRect); }); ++attempt)
          labelRect.translate(0, step);
        if (labelRect.top() < area.top() || labelRect.bottom() > area.bottom()
            || std::any_of(used.cbegin(), used.cend(),
              [&labelRect](const QRect& u) { return u.intersects(labelRect); })) continue;
        used.push_back(labelRect);
        painter.setPen(item.color);
        painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, item.text);
      }
    };

    painter.setPen(QPen(annotated ? QColor(145, 145, 155, 170) : stickColor, 1));
    if (spectrum.size() <= 5000)
    {
      for (const auto& peak : spectrum)
      {
        if (peak.getMZ() < mzMin || peak.getMZ() > mzMax) continue;
        const int x = xForMz(peak.getMZ());
        painter.drawLine(x, baseline, x, yForIntensity(peak.getIntensity()));
      }
    }
    else
    {
      std::vector<double> envelope(static_cast<std::size_t>(area.width() + 1), 0.0);
      for (const auto& peak : spectrum)
      {
        if (peak.getMZ() < mzMin || peak.getMZ() > mzMax) continue;
        const int bin = std::clamp(xForMz(peak.getMZ()) - area.left(), 0, area.width());
        envelope[static_cast<std::size_t>(bin)] = std::max(
          envelope[static_cast<std::size_t>(bin)], static_cast<double>(peak.getIntensity()));
      }
      for (int bin = 0; bin <= area.width(); ++bin)
      {
        if (envelope[static_cast<std::size_t>(bin)] <= 0.0) continue;
        const int x = area.left() + bin;
        painter.drawLine(x, baseline, x, yForIntensity(envelope[static_cast<std::size_t>(bin)]));
      }
    }

    if (annotated)
    {
      if (!mirror)
      {
        std::vector<StickLabel> labels;
        for (const MatchedIon& ion : annotation_->matched)
        {
          if (ion.experimentalMz < mzMin || ion.experimentalMz > mzMax) continue;
          const int x = xForMz(ion.experimentalMz);
          const int y = yForIntensity(ion.experimentalIntensity);
          painter.setPen(QPen(ionColor(ion.type), 2));
          painter.drawLine(x, baseline, x, y);
          labels.push_back({x, y, formatIonLabel(ion.name), ionColor(ion.type), ion.experimentalIntensity});
        }
        drawStickLabels(std::move(labels), true);
      }
      else
      {
        double theoreticalMax = 1.0;
        for (const MatchedIon& ion : annotation_->matched)
          theoreticalMax = std::max(theoreticalMax, ion.theoreticalIntensity);
        for (const TheoreticalIon& ion : annotation_->unmatched)
          theoreticalMax = std::max(theoreticalMax, ion.intensity);
        const auto negativeY = [&](double intensity)
        {
          return baseline + static_cast<int>(intensity / theoreticalMax * area.height() * 0.42);
        };

        // Experimental (top) matched peaks: colour by ion type and label upward
        // so observed fragments can be read and lined up against the theoretical.
        std::vector<StickLabel> topLabels;
        for (const MatchedIon& ion : annotation_->matched)
        {
          if (ion.experimentalMz < mzMin || ion.experimentalMz > mzMax) continue;
          const int x = xForMz(ion.experimentalMz);
          const int y = yForIntensity(ion.experimentalIntensity);
          painter.setPen(QPen(ionColor(ion.type), 2));
          painter.drawLine(x, baseline, x, y);
          topLabels.push_back({x, y, formatIonLabel(ion.name), ionColor(ion.type), ion.experimentalIntensity});
        }
        drawStickLabels(std::move(topLabels), true);

        // Theoretical (bottom) peaks: matched in ion colour, unmatched dashed grey,
        // each labelled downward.
        std::vector<StickLabel> bottomLabels;
        for (const MatchedIon& ion : annotation_->matched)
        {
          if (ion.theoreticalMz < mzMin || ion.theoreticalMz > mzMax) continue;
          painter.setPen(QPen(ionColor(ion.type), 2));
          const int x = xForMz(ion.theoreticalMz);
          const int y = negativeY(ion.theoreticalIntensity);
          painter.drawLine(x, baseline, x, y);
          bottomLabels.push_back({x, y, formatIonLabel(ion.name), ionColor(ion.type), ion.theoreticalIntensity});
        }
        if (showUnmatchedTheoretical_)
        {
          const QColor unmatchedLabel(140, 140, 150);
          for (const TheoreticalIon& ion : annotation_->unmatched)
          {
            if (ion.mz < mzMin || ion.mz > mzMax) continue;
            painter.setPen(QPen(QColor(120, 120, 130, 150), 1, Qt::DashLine));
            const int x = xForMz(ion.mz);
            const int y = negativeY(ion.intensity);
            painter.drawLine(x, baseline, x, y);
            bottomLabels.push_back({x, y, formatIonLabel(ion.name), unmatchedLabel, ion.intensity});
          }
        }
        drawStickLabels(std::move(bottomLabels), false);

        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(QRect(area.left() + 4, area.bottom() - 18, area.width() - 8, 16),
                         Qt::AlignRight | Qt::AlignBottom, tr("Theoretical spectrum"));
      }

      // On-canvas key mapping ion colours to the ion types actually present.
      static const IonType legendOrder[] = {IonType::B, IonType::Y, IonType::A,
                                            IonType::C, IonType::X, IonType::Z,
                                            IonType::Precursor, IonType::Unknown};
      std::vector<IonType> present;
      for (IonType type : legendOrder)
        if (std::any_of(annotation_->matched.cbegin(), annotation_->matched.cend(),
              [type](const MatchedIon& ion) { return ion.type == type; }))
          present.push_back(type);
      if (!present.empty())
      {
        painter.setFont(QFont(painter.font().family(), std::max(7, font().pointSize() - 1)));
        const QFontMetrics metrics = painter.fontMetrics();
        int total = 0;
        for (IonType type : present)
          total += 11 + 3 + metrics.horizontalAdvance(ionTypeName(type)) + 10;
        int x = std::max(area.left() + 2, area.right() - total);
        const int y = area.top() + 3;
        // Translucent backing keeps the key legible over tall right-side peaks.
        const QColor base = palette().color(QPalette::Base);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(base.red(), base.green(), base.blue(), 205));
        painter.drawRoundedRect(QRect(x - 4, y - 2, area.right() - x + 4, 15), 3, 3);
        for (IonType type : present)
        {
          painter.fillRect(QRect(x, y + 1, 9, 9), ionColor(type));
          x += 11 + 3;
          painter.setPen(palette().color(QPalette::Text));
          const int textWidth = metrics.horizontalAdvance(ionTypeName(type));
          painter.drawText(QRect(x, y - 2, textWidth + 2, 15), Qt::AlignLeft | Qt::AlignVCenter,
                           ionTypeName(type));
          x += textWidth + 10;
        }
      }

      // Restore the default font/brush so the smaller label/legend font does not
      // leak into the precursor marker, measurement, and hover-callout drawing.
      painter.setFont(font());
      painter.setBrush(Qt::NoBrush);
    }

    if (spectrum.getMSLevel() >= 2 && !spectrum.getPrecursors().empty())
    {
      const double precursorMz = spectrum.getPrecursors().front().getMZ();
      if (precursorMz >= mzMin && precursorMz <= mzMax)
      {
        painter.setPen(QPen(QColor(255, 140, 40), 1.5, Qt::DashLine));
        const int x = xForMz(precursorMz);
        painter.drawLine(x, area.top(), x, area.bottom());
        painter.drawText(x + 4, area.top() + 14,
                         tr("precursor %1").arg(precursorMz, 0, 'f', 4));
      }
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    {
      const auto& measurementList = measurements();
      for (std::size_t index = 0; index < measurementList.size(); ++index)
      {
        const SpectrumMeasurement& measurement = measurementList[index];
        if (measurement.firstMz < mzMin || measurement.firstMz > mzMax
            || measurement.secondMz < mzMin || measurement.secondMz > mzMax) continue;
        const bool selected = selectedMeasurement_ && *selectedMeasurement_ == index;
        const int firstX = xForMz(measurement.firstMz);
        const int secondX = xForMz(measurement.secondMz);
        const int firstY = yForIntensity(measurement.firstIntensity);
        const int secondY = yForIntensity(measurement.secondIntensity);
        const int bracketY = std::max(area.top() + 22,
          std::min(firstY, secondY) - std::max(18, area.height() / 14));
        const QColor barColor = selected ? QColor(255, 80, 40) : QColor(255, 136, 0);
        painter.setPen(QPen(barColor, selected ? 2.6 : 1.8));
        painter.drawLine(firstX, bracketY, secondX, bracketY);
        painter.setPen(QPen(barColor, 1.0, Qt::DotLine));
        painter.drawLine(firstX, firstY, firstX, bracketY);
        painter.drawLine(secondX, secondY, secondX, bracketY);
        painter.setPen(selected ? QColor(255, 110, 70) : QColor(255, 155, 30));
        painter.drawText(QRect(std::min(firstX, secondX), bracketY - 20,
                               std::abs(secondX - firstX), 18), Qt::AlignCenter,
                         tr("Δ %1").arg(std::abs(measurement.secondMz - measurement.firstMz), 0, 'f', 4));
      }
    }

    if (measurementStart_)
    {
      const int x = xForMz(measurementStart_->first);
      const int y = yForIntensity(measurementStart_->second);
      painter.setPen(QPen(QColor(255, 136, 0), 2.0));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(QPointF(x, y), 5.0, 5.0);
      if (hoveredPeak_ && hoveredPeak_->first != measurementStart_->first)
      {
        const int hx = xForMz(hoveredPeak_->first);
        const int hy = yForIntensity(hoveredPeak_->second);
        painter.setPen(QPen(QColor(255, 136, 0), 1.3, Qt::DashLine));
        painter.drawLine(QPointF(x, y), QPointF(hx, hy));
        // Live Δ m/z readout tracking the candidate second peak during placement.
        painter.setPen(QColor(255, 155, 30));
        painter.drawText(QPointF((x + hx) / 2.0 + 4.0, std::min(y, hy) - 6),
                         tr("Δ %1").arg(std::abs(hoveredPeak_->first - measurementStart_->first), 0, 'f', 4));
      }
    }

    // User-authored peak labels: an arrowed leader to the peak tip plus a text
    // chip, stacked upward to avoid overlapping one another.
    {
      const QColor labelColor = darkTheme ? QColor(120, 220, 140) : QColor(28, 130, 60);
      painter.setFont(QFont(painter.font().family(), std::max(7, font().pointSize() - 1)));
      std::vector<QRect> usedRects;
      for (const PeakLabel& label : labels())
      {
        if (label.mz < mzMin || label.mz > mzMax || label.text.isEmpty()) continue;
        const int x = xForMz(label.mz);
        const int yTip = yForIntensity(label.intensity);
        const QSize textSize = painter.fontMetrics().size(Qt::TextSingleLine, label.text);
        QRect box(QPoint(x - textSize.width() / 2, yTip - textSize.height() - 12),
                  textSize + QSize(8, 4));
        box.moveLeft(std::clamp(box.left(), area.left(), std::max(area.left(), area.right() - box.width())));
        for (int attempt = 0; attempt < 4
             && std::any_of(usedRects.cbegin(), usedRects.cend(),
               [&box](const QRect& used) { return used.intersects(box); }); ++attempt)
          box.translate(0, -(textSize.height() + 4));
        if (box.top() < area.top()) box.moveTop(area.top());
        usedRects.push_back(box);
        painter.setPen(QPen(labelColor, 1.2));
        painter.drawLine(QPoint(x, box.bottom()), QPoint(x, yTip));
        painter.setPen(Qt::NoPen);
        painter.setBrush(labelColor);
        const QPointF arrow[3] = {QPointF(x, yTip), QPointF(x - 3, yTip - 6.0), QPointF(x + 3, yTip - 6.0)};
        painter.drawPolygon(arrow, 3);
        const QColor chip = palette().color(QPalette::Base);
        painter.setBrush(QColor(chip.red(), chip.green(), chip.blue(), 225));
        painter.drawRoundedRect(box, 3, 3);
        painter.setPen(labelColor);
        painter.drawText(box, Qt::AlignCenter, label.text);
      }
      painter.setFont(font());       // don't leak the label font into later drawing
      painter.setBrush(Qt::NoBrush);
    }

    if (hoveredPeak_ && hoveredPeak_->first >= mzMin && hoveredPeak_->first <= mzMax)
    {
      const int x = xForMz(hoveredPeak_->first);
      const int y = yForIntensity(hoveredPeak_->second);
      painter.setPen(QPen(measurementMode_ ? QColor(255, 136, 0) : QColor(40, 170, 255), 2.0));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(QPointF(x, y), 4.5, 4.5);
      painter.setPen(palette().color(QPalette::Text));
      painter.setBrush(QColor(palette().color(QPalette::Base).red(),
                              palette().color(QPalette::Base).green(),
                              palette().color(QPalette::Base).blue(), 220));
      const QString label = tr("m/z %1 · I %2")
        .arg(hoveredPeak_->first, 0, 'f', 5).arg(hoveredPeak_->second, 0, 'g', 5);
      const QRect labelRect(x + 7, std::max(area.top() + 2, y - 25),
                            painter.fontMetrics().horizontalAdvance(label) + 10, 21);
      painter.drawRoundedRect(labelRect, 3, 3);
      painter.drawText(labelRect.adjusted(5, 0, -5, 0), Qt::AlignVCenter, label);
    }

    if (showMzLabels_ && !annotated)
    {
      std::vector<const OpenMS::Peak1D*> peaks;
      for (const auto& peak : spectrum)
        if (peak.getMZ() >= mzMin && peak.getMZ() <= mzMax
            && peak.getIntensity() >= intensityMax * 0.05) peaks.push_back(&peak);
      std::sort(peaks.begin(), peaks.end(), [](const auto* left, const auto* right)
      {
        return left->getIntensity() > right->getIntensity();
      });
      if (peaks.size() > 15) peaks.resize(15);
      std::vector<int> usedX;
      painter.setPen(darkTheme ? QColor(115, 195, 235) : QColor(30, 120, 160));
      painter.setFont(QFont(painter.font().family(), std::max(7, painter.font().pointSize() - 2)));
      for (const auto* peak : peaks)
      {
        const int x = xForMz(peak->getMZ());
        if (std::any_of(usedX.begin(), usedX.end(), [x](int used) { return std::abs(used - x) < 34; }))
          continue;
        usedX.push_back(x);
        painter.drawText(QPointF(x + 2, std::max(area.top() + 11,
          yForIntensity(peak->getIntensity()) - 3)), QString::number(peak->getMZ(), 'f', 3));
      }
    }

    if (draggingZoom_)
    {
      QRect selection(QPoint(dragStart_.x(), area.top()), QPoint(dragCurrent_.x(), area.bottom()));
      painter.setPen(QPen(QColor(255, 220, 70), 1.2));
      painter.setBrush(QColor(255, 220, 70, 35));
      painter.drawRect(selection.normalized());
    }
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.setFont(font());
    painter.setPen(palette().color(QPalette::Text));
    QString title = standaloneSpectrum_
      ? tr("#%1/%2   MS%3   pixel spectrum   %4 peaks")
          .arg(spectrumIndex_ + 1).arg(totalSpectra_).arg(spectrum.getMSLevel()).arg(spectrum.size())
      : tr("#%1   MS%2   RT %3 %4   %5 peaks")
                      .arg(spectrumIndex_ + 1)
                      .arg(spectrum.getMSLevel())
                      .arg(RtUnit::format(spectrum.getRT(), rtInMinutes_))
                      .arg(RtUnit::unit(rtInMinutes_))
                      .arg(spectrum.size());
    if (annotated)
      title += tr("   %1 (z=%2)   %3/%4 ions · %5% coverage")
        .arg(annotation_->sequence).arg(annotation_->charge)
        .arg(annotation_->matched.size()).arg(annotation_->theoreticalCount)
        .arg(annotation_->coverage * 100.0, 0, 'f', 1);
    painter.drawText(QRect(area.left(), 6, area.width(), 30), Qt::AlignLeft | Qt::AlignVCenter, title);
    painter.drawText(QRect(area.left(), area.bottom() + 14, area.width(), 20), Qt::AlignCenter, tr("m/z"));
    painter.save();
    painter.translate(14, area.center().y());
    painter.rotate(-90);
    painter.drawText(QRect(-area.height() / 2, -10, area.height(), 20), Qt::AlignCenter,
                     mirror || relativeIntensity_ ? tr("Relative intensity (%)") : tr("Intensity"));
    painter.restore();

    if (!mirror)
    {
      // In relative mode the axis reads percent of the whole-spectrum base peak,
      // so a zoomed-in minor peak shows its true height rather than a false 100%.
      const double topPercent = intensityMax / baseIntensityMax * 100.0;
      for (int tick = 0; tick <= 4; ++tick)
      {
        const double fraction = tick / 4.0;
        const int y = baseline - static_cast<int>(fraction * positiveHeight);
        painter.drawLine(area.left() - 4, y, area.left(), y);
        const double value = relativeIntensity_ ? fraction * topPercent : fraction * intensityMax;
        const QString label = relativeIntensity_
          ? QString::number(value, 'f', topPercent < 20.0 ? 1 : 0)
          : QString::number(value, 'g', 3);
        painter.drawText(QRect(1, y - 9, area.left() - 9, 18), Qt::AlignRight | Qt::AlignVCenter, label);
      }
    }
    else
    {
      painter.drawText(QRect(1, area.top() - 2, area.left() - 9, 18), Qt::AlignRight, QStringLiteral("100"));
      painter.drawText(QRect(1, baseline - 9, area.left() - 9, 18), Qt::AlignRight, QStringLiteral("0"));
      painter.drawText(QRect(1, area.bottom() - 16, area.left() - 9, 18), Qt::AlignRight, QStringLiteral("100"));
    }

    for (int tick = 0; tick <= 5; ++tick)
    {
      const double fraction = tick / 5.0;
      const int x = area.left() + static_cast<int>(fraction * area.width());
      painter.drawLine(x, area.bottom(), x, area.bottom() + 4);
      painter.drawText(QRect(x - 40, area.bottom() + 5, 80, 18), Qt::AlignHCenter,
                       QString::number(mzMin + fraction * (mzMax - mzMin), 'f', 1));
    }
  }

  void SpectrumWidget::wheelEvent(QWheelEvent* event)
  {
    if (!currentSpectrum() || !plotRect().contains(event->position().toPoint())) return;
    const auto full = fullMzRange();
    const double minimum = mzView_ ? mzView_->first : full.first;
    const double maximum = mzView_ ? mzView_->second : full.second;
    const double fraction = std::clamp((event->position().x() - plotRect().left())
      / plotRect().width(), 0.0, 1.0);
    const double cursor = minimum + fraction * (maximum - minimum);
    const double span = (maximum - minimum) * (event->angleDelta().y() > 0 ? 0.8 : 1.25);
    applyMzView(cursor - fraction * span, cursor + (1.0 - fraction) * span);
    event->accept();
  }

  void SpectrumWidget::mousePressEvent(QMouseEvent* event)
  {
    if (event->button() != Qt::LeftButton || !plotRect().contains(event->pos())) return;
    if (measurementMode_)
    {
      const auto peak = peakAt(event->position());
      if (!peak) return;
      if (!measurementStart_)
      {
        measurementStart_ = peak;
      }
      else if (peak->first != measurementStart_->first)
      {
        measurements_[spectrumIndex_].push_back({measurementStart_->first,
                                                  measurementStart_->second,
                                                  peak->first, peak->second});
        measurementStart_.reset();
      }
      update();
      return;
    }
    if (labelMode_)
    {
      const auto peak = peakAt(event->position());
      if (peak) editLabelAt(*peak);  // snap, then add/edit/remove a free-text label
      return;
    }
    draggingZoom_ = true;
    dragStart_ = event->pos();
    dragCurrent_ = dragStart_;
    update();
  }

  void SpectrumWidget::mouseMoveEvent(QMouseEvent* event)
  {
    if (draggingZoom_) dragCurrent_ = event->pos();
    const auto peak = peakAt(event->position());
    if (peak != hoveredPeak_)
    {
      hoveredPeak_ = peak;
      if (peak)
      {
        QString tip = tr("m/z %1\nIntensity %2")
          .arg(peak->first, 0, 'f', 6).arg(peak->second, 0, 'g', 8);
        // When the spectrum is annotated, surface the matched fragment's identity
        // and mass error — the labels dropped by on-canvas de-collision are only
        // readable here.
        if (annotationEnabled_ && annotation_ && !annotation_->sequence.isEmpty())
        {
          // The snapped peak's m/z equals a matched ion's experimentalMz exactly,
          // so match on near-identity — never on a merely-nearby different peak.
          const MatchedIon* match = nullptr;
          double bestError = 1e-3;
          for (const MatchedIon& ion : annotation_->matched)
          {
            const double error = std::abs(ion.experimentalMz - peak->first);
            if (error <= bestError) { bestError = error; match = &ion; }
          }
          if (match)
          {
            const double ppm = match->theoreticalMz > 0.0
              ? match->mzError / match->theoreticalMz * 1e6 : 0.0;
            tip = tr("%1\nm/z %2\nΔ %3 Da · %4 ppm\nIntensity %5")
              .arg(formatIonLabel(match->name))
              .arg(peak->first, 0, 'f', 6)
              .arg(match->mzError, 0, 'f', 4)
              .arg(ppm, 0, 'f', 1)
              .arg(peak->second, 0, 'g', 8);
          }
        }
        setToolTip(tip);
      }
      else setToolTip({});
    }
    update();
  }

  void SpectrumWidget::mouseReleaseEvent(QMouseEvent* event)
  {
    if (!draggingZoom_ || event->button() != Qt::LeftButton) return;
    draggingZoom_ = false;
    dragCurrent_ = event->pos();
    if (std::abs(dragCurrent_.x() - dragStart_.x()) < 6)
    {
      // A click (rather than a drag-to-zoom) selects/deselects a measurement so it
      // can be removed with Delete; clicking empty space clears the selection.
      const auto hit = measurementAt(event->position());
      if (hit && hit == selectedMeasurement_) selectedMeasurement_.reset();
      else selectedMeasurement_ = hit;
      update();
      return;
    }
    const QRect area = plotRect();
    const auto full = fullMzRange();
    const double minimum = mzView_ ? mzView_->first : full.first;
    const double maximum = mzView_ ? mzView_->second : full.second;
    const double left = std::clamp((std::min(dragStart_.x(), dragCurrent_.x()) - area.left())
      / static_cast<double>(area.width()), 0.0, 1.0);
    const double right = std::clamp((std::max(dragStart_.x(), dragCurrent_.x()) - area.left())
      / static_cast<double>(area.width()), 0.0, 1.0);
    applyMzView(minimum + left * (maximum - minimum),
                minimum + right * (maximum - minimum));
  }

  void SpectrumWidget::mouseDoubleClickEvent(QMouseEvent*) { resetMzView(); }

  void SpectrumWidget::leaveEvent(QEvent* event)
  {
    hoveredPeak_.reset();
    QWidget::leaveEvent(event);
    update();
  }

  void SpectrumWidget::keyPressEvent(QKeyEvent* event)
  {
    if (event->key() == Qt::Key_Home)
    {
      resetMzView();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
      if (selectedMeasurement_)
      {
        const auto found = measurements_.find(spectrumIndex_);
        if (found != measurements_.end() && *selectedMeasurement_ < found->second.size())
        {
          found->second.erase(found->second.begin()
            + static_cast<std::ptrdiff_t>(*selectedMeasurement_));
          if (found->second.empty()) measurements_.erase(found);
        }
        selectedMeasurement_.reset();
        update();
        event->accept();
        return;
      }
    }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal
        || event->key() == Qt::Key_Minus)
    {
      const auto full = fullMzRange();
      const double minimum = mzView_ ? mzView_->first : full.first;
      const double maximum = mzView_ ? mzView_->second : full.second;
      const double center = (minimum + maximum) / 2.0;
      const double span = (maximum - minimum)
        * (event->key() == Qt::Key_Minus ? 1.25 : 0.8);
      applyMzView(center - span / 2.0, center + span / 2.0);
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Escape)
    {
      measurementStart_.reset();
      draggingZoom_ = false;
      update();
      event->accept();
      return;
    }
    QWidget::keyPressEvent(event);
  }
}
