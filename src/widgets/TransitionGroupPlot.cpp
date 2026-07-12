#include "widgets/TransitionGroupPlot.h"

#include "plot/PlotAxis.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace OpenMSViewer
{
  namespace
  {
    // Distinct, stable colours for fragment traces.
    constexpr std::array<QColor, 8> kTraceColors{
      QColor(52, 152, 219), QColor(231, 76, 60), QColor(46, 204, 113), QColor(155, 89, 182),
      QColor(241, 196, 15), QColor(26, 188, 156), QColor(230, 126, 34), QColor(120, 144, 156)};

    double peakIntensity(const TransitionChromatogram& transition)
    {
      double maximum = 0.0;
      for (const double value : transition.intensity)
        if (std::isfinite(value)) maximum = std::max(maximum, value);
      return maximum;
    }
  }

  TransitionGroupPlot::TransitionGroupPlot(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("transitionGroupPlot"));
    setMinimumHeight(240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(tr("Transition group chromatogram"));
  }

  void TransitionGroupPlot::setData(std::vector<TransitionChromatogram> transitions,
                                    std::vector<OswPeakGroup> peakGroups,
                                    int selectedPeakGroup, double libraryRt)
  {
    transitions_ = std::move(transitions);
    peakGroups_ = std::move(peakGroups);
    selectedPeakGroup_ = selectedPeakGroup;
    libraryRt_ = libraryRt;
    update();
  }

  void TransitionGroupPlot::setShowAllTransitions(bool showAll)
  {
    if (showAll_ == showAll) return;
    showAll_ = showAll;
    update();
  }

  void TransitionGroupPlot::clear()
  {
    transitions_.clear();
    peakGroups_.clear();
    selectedPeakGroup_ = -1;
    libraryRt_ = 0.0;
    update();
  }

  QRectF TransitionGroupPlot::plotRect() const
  {
    return QRectF(rect()).adjusted(70.0, 12.0, -14.0, -44.0);
  }

  std::vector<const TransitionChromatogram*> TransitionGroupPlot::visibleTransitions() const
  {
    std::vector<const TransitionChromatogram*> ms1;
    std::vector<const TransitionChromatogram*> fragments;
    for (const TransitionChromatogram& transition : transitions_)
    {
      if (transition.rt.empty()) continue;
      (transition.isMs1() ? ms1 : fragments).push_back(&transition);
    }
    // Fragments ranked by peak intensity; keep the top-N unless "show all".
    std::sort(fragments.begin(), fragments.end(),
              [](const TransitionChromatogram* a, const TransitionChromatogram* b)
              { return peakIntensity(*a) > peakIntensity(*b); });
    if (!showAll_ && static_cast<int>(fragments.size()) > topFragments_)
      fragments.resize(static_cast<std::size_t>(topFragments_));
    // MS1 traces are always shown (drawn first, behind the fragments).
    std::vector<const TransitionChromatogram*> visible = std::move(ms1);
    visible.insert(visible.end(), fragments.begin(), fragments.end());
    return visible;
  }

  void TransitionGroupPlot::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    const QRectF area = plotRect();
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(area);

    const auto visible = visibleTransitions();
    if (visible.empty())
    {
      painter.setPen(palette().color(QPalette::Text));
      painter.drawText(area, Qt::AlignCenter, tr("Select a precursor to view its transitions"));
      return;
    }

    double rtMin = std::numeric_limits<double>::infinity();
    double rtMax = -std::numeric_limits<double>::infinity();
    double intensityMax = 0.0;
    for (const TransitionChromatogram* transition : visible)
    {
      const std::size_t count = std::min(transition->rt.size(), transition->intensity.size());
      for (std::size_t point = 0; point < count; ++point)
      {
        const double rt = transition->rt[point];
        const double intensity = transition->intensity[point];
        if (!std::isfinite(rt) || !std::isfinite(intensity)) continue;  // ignore bad samples
        rtMin = std::min(rtMin, rt);
        rtMax = std::max(rtMax, rt);
        intensityMax = std::max(intensityMax, intensity);
      }
    }
    // Include the selected peak-group boundaries in the RT window so they are visible.
    if (selectedPeakGroup_ >= 0 && selectedPeakGroup_ < static_cast<int>(peakGroups_.size()))
    {
      const OswPeakGroup& group = peakGroups_[static_cast<std::size_t>(selectedPeakGroup_)];
      if (std::isfinite(group.leftWidth)) rtMin = std::min(rtMin, group.leftWidth);
      if (std::isfinite(group.rightWidth)) rtMax = std::max(rtMax, group.rightWidth);
    }
    // Keep a degenerate-but-finite RT range (single-point or all-identical-RT
    // traces) centred on its value instead of collapsing to [0,1], which would
    // push a real RT (e.g. 305 s) far off-screen.
    if (!std::isfinite(rtMin) || !std::isfinite(rtMax)) { rtMin = 0.0; rtMax = 1.0; }
    else if (rtMax <= rtMin) { rtMin -= 0.5; rtMax += 0.5; }
    if (!(intensityMax > 0.0)) intensityMax = 1.0;

    const auto mapX = [&](double rt)
    { return area.left() + (rt - rtMin) / (rtMax - rtMin) * area.width(); };
    const auto mapY = [&](double intensity)
    { return area.bottom() - intensity / intensityMax * area.height() * 0.96; };

    // Nice-tick RT + intensity gridlines (shared helper, as the TIC/chromatogram).
    const auto rtTicks = PlotAxis::niceTicks(rtMin, rtMax, 6);
    const auto intensityTicks = PlotAxis::niceTicks(0.0, intensityMax, 5);
    painter.setPen(QPen(palette().color(QPalette::Mid), 1.0, Qt::DotLine));
    for (const double tick : rtTicks)
    {
      const double x = mapX(tick);
      if (x >= area.left() - 0.5 && x <= area.right() + 0.5)
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    for (const double tick : intensityTicks)
    {
      const double y = mapY(tick);
      if (y >= area.top() - 0.5 && y <= area.bottom() + 0.5)
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    // Other candidate peak groups: faint boundary lines.
    for (int index = 0; index < static_cast<int>(peakGroups_.size()); ++index)
    {
      if (index == selectedPeakGroup_) continue;
      const OswPeakGroup& group = peakGroups_[static_cast<std::size_t>(index)];
      painter.setPen(QPen(QColor(150, 150, 150, 120), 1.0, Qt::DashLine));
      for (const double boundary : {group.leftWidth, group.rightWidth})
      {
        if (!std::isfinite(boundary)) continue;
        const double x = mapX(boundary);
        if (x >= area.left() && x <= area.right())
          painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
      }
    }

    // Selected peak group: strong shaded RT window + apex line.
    if (selectedPeakGroup_ >= 0 && selectedPeakGroup_ < static_cast<int>(peakGroups_.size()))
    {
      const OswPeakGroup& group = peakGroups_[static_cast<std::size_t>(selectedPeakGroup_)];
      if (std::isfinite(group.leftWidth) && std::isfinite(group.rightWidth))
      {
        const double left = std::clamp(mapX(group.leftWidth), area.left(), area.right());
        const double right = std::clamp(mapX(group.rightWidth), area.left(), area.right());
        painter.fillRect(QRectF(QPointF(left, area.top()), QPointF(right, area.bottom())),
                         QColor(255, 210, 40, 55));
      }
      if (std::isfinite(group.apexRt))
      {
        painter.setPen(QPen(QColor(255, 170, 0), 1.4, Qt::DashLine));
        const double apex = mapX(group.apexRt);
        if (apex >= area.left() && apex <= area.right())
          painter.drawLine(QPointF(apex, area.top()), QPointF(apex, area.bottom()));
      }
    }

    // Library RT marker.
    if (libraryRt_ > 0.0 && std::isfinite(libraryRt_))
    {
      const double x = mapX(libraryRt_);
      if (x >= area.left() && x <= area.right())
      {
        painter.setPen(QPen(QColor(120, 120, 200), 1.2, Qt::DotLine));
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
      }
    }

    // Transition traces: MS1 dashed dark (behind), fragments solid colour-cycled.
    int colorIndex = 0;
    for (const TransitionChromatogram* transition : visible)
    {
      QPainterPath path;
      bool haveLast = false;
      const std::size_t count = std::min(transition->rt.size(), transition->intensity.size());
      for (std::size_t point = 0; point < count; ++point)
      {
        const double rt = transition->rt[point];
        const double intensity = transition->intensity[point];
        if (!std::isfinite(rt) || !std::isfinite(intensity)) { haveLast = false; continue; }
        const QPointF position(mapX(rt), mapY(intensity));
        haveLast ? path.lineTo(position) : path.moveTo(position);
        haveLast = true;
      }
      if (transition->isMs1())
        painter.setPen(QPen(palette().color(QPalette::Text), 1.3, Qt::DashLine));
      else
        painter.setPen(QPen(kTraceColors[static_cast<std::size_t>(colorIndex++) % kTraceColors.size()], 1.6));
      painter.drawPath(path);
    }

    // Axis labels + legend.
    painter.setPen(palette().color(QPalette::Text));
    const double rtStep = rtTicks.size() > 1 ? std::abs(rtTicks[1] - rtTicks[0]) : 1.0;
    const int rtDecimals = rtStep >= 1.0 ? 0
      : std::min(4, static_cast<int>(std::ceil(-std::log10(rtStep))));
    for (const double tick : rtTicks)
    {
      const double x = mapX(tick);
      if (x < area.left() - 0.5 || x > area.right() + 0.5) continue;
      painter.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + 4.0));
      painter.drawText(QRectF(x - 42.0, area.bottom() + 5.0, 84.0, 15.0),
                       Qt::AlignHCenter | Qt::AlignTop, QString::number(tick, 'f', rtDecimals));
    }
    for (const double tick : intensityTicks)
    {
      const double y = mapY(tick);
      if (y < area.top() - 0.5 || y > area.bottom() + 0.5) continue;
      painter.drawText(QRectF(0.0, y - 8.0, area.left() - 6.0, 16.0),
                       Qt::AlignRight | Qt::AlignVCenter, QString::number(tick, 'g', 3));
    }
    painter.drawText(QRectF(area.left(), height() - 22.0, area.width(), 18.0),
                     Qt::AlignCenter, tr("Retention time (s)"));

    // Legend: colour-matched fragment ion labels. Entries are measured before
    // drawing (so the last never protrudes); when the row is full the remaining
    // fragments collapse into a "+N more" tag rather than being dropped silently.
    int fragmentTotal = 0;
    for (const TransitionChromatogram* transition : visible)
      if (!transition->isMs1()) ++fragmentTotal;
    double legendX = area.left() + 4.0;
    colorIndex = 0;
    int shown = 0;
    for (const TransitionChromatogram* transition : visible)
    {
      if (transition->isMs1()) continue;
      const QColor color = kTraceColors[static_cast<std::size_t>(colorIndex++) % kTraceColors.size()];
      QString label = transition->annotation;
      if (label.isEmpty())
      {
        if (!transition->ionType.isEmpty())
          label = QStringLiteral("%1%2").arg(transition->ionType).arg(transition->ordinal);
        else if (transition->transitionId >= 0)
          label = QStringLiteral("tr %1").arg(transition->transitionId);
        else
          label = tr("trace");
      }
      label = painter.fontMetrics().elidedText(label, Qt::ElideRight, 90);
      const double entryWidth = 26.0 + painter.fontMetrics().horizontalAdvance(label);
      if (legendX + entryWidth > area.right() - 4.0)
      {
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QPointF(legendX, area.top() + 12.0),
                         tr("+%1 more").arg(fragmentTotal - shown));
        break;
      }
      painter.setPen(QPen(color, 3.0));
      painter.drawLine(QPointF(legendX, area.top() + 8.0), QPointF(legendX + 14.0, area.top() + 8.0));
      painter.setPen(palette().color(QPalette::Text));
      painter.drawText(QPointF(legendX + 18.0, area.top() + 12.0), label);
      legendX += entryWidth;
      ++shown;
    }
  }
}
