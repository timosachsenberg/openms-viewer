#include "widgets/ChromatogramPanelWidget.h"
#include "widgets/CompactControls.h"

#include "model/RtUnit.h"

#include "model/TraceSmoothing.h"
#include "plot/PlotAxis.h"
#include "plot/PlotTheme.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace OpenMSViewer
{
  class ChromatogramTableModel final : public QAbstractTableModel
  {
  public:
    enum Column
    {
      Index, NativeId, Type, Tic, PrecursorMz, Charge, ProductMz, RtRange,
      PointCount, MaximumIntensity, TotalIntensity, ColumnCount
    };

    explicit ChromatogramTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    void setChromatograms(const std::vector<ChromatogramRecord>& chromatograms)
    {
      beginResetModel();
      chromatograms_ = chromatograms;
      endResetModel();
    }

    [[nodiscard]] const ChromatogramRecord* recordForRow(int row) const noexcept
    {
      if (row < 0 || static_cast<std::size_t>(row) >= chromatograms_.size()) return nullptr;
      return &chromatograms_[static_cast<std::size_t>(row)];
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
      return parent.isValid() ? 0 : static_cast<int>(chromatograms_.size());
    }

    int columnCount(const QModelIndex& parent = {}) const override
    {
      return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
      if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
      switch (section)
      {
        case Index: return QStringLiteral("#");
        case NativeId: return QStringLiteral("Native ID");
        case Type: return QStringLiteral("Type");
        case Tic: return QStringLiteral("TIC");
        case PrecursorMz: return QStringLiteral("Q1 m/z");
        case Charge: return QStringLiteral("Z");
        case ProductMz: return QStringLiteral("Q3 m/z");
        case RtRange: return QStringLiteral("RT range (%1)").arg(RtUnit::unit(rtInMinutes_));
        case PointCount: return QStringLiteral("Points");
        case MaximumIntensity: return QStringLiteral("Maximum");
        case TotalIntensity: return QStringLiteral("Total");
        default: return {};
      }
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
      const ChromatogramRecord* record = recordForRow(index.row());
      if (!record || index.column() < 0 || index.column() >= ColumnCount) return {};
      if (role == Qt::TextAlignmentRole)
      {
        return index.column() == NativeId || index.column() == Type
          ? QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter)
          : QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
      }
      if (role == Qt::UserRole)
      {
        switch (index.column())
        {
          case Index: return static_cast<qulonglong>(record->index);
          case NativeId: return record->nativeId;
          case Type: return record->type;
          case Tic: return record->isTic;
          case PrecursorMz: return record->precursorMz.value_or(0.0);
          case Charge: return record->precursorCharge;
          case ProductMz: return record->productMz.value_or(0.0);
          case RtRange: return record->rtMin;
          case PointCount: return static_cast<qulonglong>(record->points.size());
          case MaximumIntensity: return record->maximumIntensity;
          case TotalIntensity: return record->totalIntensity;
          default: return {};
        }
      }
      if (role != Qt::DisplayRole) return {};
      switch (index.column())
      {
        case Index: return static_cast<qulonglong>(record->index);
        case NativeId: return record->nativeId.isEmpty() ? QStringLiteral("-") : record->nativeId;
        case Type: return record->type;
        case Tic: return record->isTic ? QStringLiteral("yes") : QStringLiteral("no");
        case PrecursorMz: return record->precursorMz ? QString::number(*record->precursorMz, 'f', 4)
                                                     : QStringLiteral("-");
        case Charge: return record->precursorCharge > 0 ? QString::number(record->precursorCharge)
                                                        : QStringLiteral("-");
        case ProductMz: return record->productMz ? QString::number(*record->productMz, 'f', 4)
                                                 : QStringLiteral("-");
        case RtRange:
          return record->points.empty() ? QStringLiteral("-")
            : QStringLiteral("%1–%2").arg(RtUnit::format(record->rtMin, rtInMinutes_),
                                          RtUnit::format(record->rtMax, rtInMinutes_));
        case PointCount: return static_cast<qulonglong>(record->points.size());
        case MaximumIntensity: return QString::number(record->maximumIntensity, 'e', 2);
        case TotalIntensity: return QString::number(record->totalIntensity, 'e', 2);
        default: return {};
      }
    }

    void setRtInMinutes(bool minutes)
    {
      if (rtInMinutes_ == minutes) return;
      rtInMinutes_ = minutes;
      emit headerDataChanged(Qt::Horizontal, RtRange, RtRange);
      if (rowCount() > 0)
        emit dataChanged(index(0, RtRange), index(rowCount() - 1, RtRange), {Qt::DisplayRole});
    }

  private:
    bool rtInMinutes_{false};
    std::vector<ChromatogramRecord> chromatograms_;
  };

  ChromatogramPlotWidget::ChromatogramPlotWidget(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("chromatogramPlot"));
    setMinimumHeight(210);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(tr("Chromatogram traces"));
    setAccessibleDescription(tr("Selected chromatogram traces linked to the peak-map RT range."));
  }

  void ChromatogramPlotWidget::setChromatograms(
    const std::vector<ChromatogramRecord>& chromatograms)
  {
    chromatograms_ = chromatograms;
    selectedIndices_.clear();
    rebuildSmoothing();
    update();
  }

  void ChromatogramPlotWidget::setSmoothing(bool smooth)
  {
    if (smooth_ == smooth) return;
    smooth_ = smooth;
    rebuildSmoothing();
    update();
  }

  void ChromatogramPlotWidget::rebuildSmoothing()
  {
    smoothed_.clear();
    if (!smooth_) return;
    smoothed_.reserve(chromatograms_.size());
    for (const ChromatogramRecord& record : chromatograms_)
    {
      std::vector<double> intensity;
      intensity.reserve(record.points.size());
      for (const ChromatogramPoint& point : record.points) intensity.push_back(point.intensity);
      smoothed_.push_back(TraceSmoothing::savitzkyGolay(intensity));
    }
  }

  void ChromatogramPlotWidget::setSelectedIndices(const std::vector<std::size_t>& indices)
  {
    selectedIndices_ = indices;
    update();
  }

  void ChromatogramPlotWidget::setPeakMapRange(const PlotRange& range)
  {
    peakMapRtRange_ = std::pair{range.rtMin, range.rtMax};
    update();
  }

  void ChromatogramPlotWidget::setRtInMinutes(bool minutes)
  {
    rtInMinutes_ = minutes;
    update();
  }

  const std::vector<std::size_t>& ChromatogramPlotWidget::selectedIndices() const noexcept
  {
    return selectedIndices_;
  }

  std::optional<std::pair<double, double>> ChromatogramPlotWidget::peakMapRtRange() const noexcept
  {
    return peakMapRtRange_;
  }

  QRectF ChromatogramPlotWidget::plotRect() const
  {
    return QRectF(rect()).adjusted(72.0, 34.0, -18.0, -44.0);
  }

  std::optional<std::pair<double, double>> ChromatogramPlotWidget::selectedRtBounds() const
  {
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const std::size_t index : selectedIndices_)
    {
      if (index >= chromatograms_.size() || chromatograms_[index].points.empty()) continue;
      minimum = std::min(minimum, chromatograms_[index].rtMin);
      maximum = std::max(maximum, chromatograms_[index].rtMax);
    }
    if (!std::isfinite(minimum)) return std::nullopt;
    if (maximum <= minimum) maximum = minimum + 1.0;
    return std::pair{minimum, maximum};
  }

  void ChromatogramPlotWidget::paintEvent(QPaintEvent*)
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    const QRectF area = plotRect();
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(area);

    const auto rtBounds = selectedRtBounds();
    if (!rtBounds || selectedIndices_.empty())
    {
      // A non-empty selection with no bounds means every selected record is an
      // empty/failed trace; say so rather than pretending nothing is selected.
      painter.setPen(palette().color(QPalette::Text));
      painter.drawText(area, Qt::AlignCenter,
                       selectedIndices_.empty()
                         ? tr("Select one or more chromatograms in the table")
                         : tr("Selected chromatogram(s) have no data points"));
      return;
    }
    const auto [rtMinimum, rtMaximum] = *rtBounds;
    double intensityMaximum = 0.0;
    for (const std::size_t index : selectedIndices_)
      if (index < chromatograms_.size())
        intensityMaximum = std::max(intensityMaximum, chromatograms_[index].maximumIntensity);
    if (intensityMaximum <= 0.0) intensityMaximum = 1.0;

    const auto mapX = [&](double rt)
    {
      return area.left() + (rt - rtMinimum) / (rtMaximum - rtMinimum) * area.width();
    };
    const auto mapY = [&](double intensity)
    {
      return area.bottom() - intensity / intensityMaximum * area.height();
    };

    // "Nice" numeric ticks (shared with the TIC) so RT and intensity read as
    // round values instead of arbitrary fractions of the range.
    const double factor = rtInMinutes_ ? 60.0 : 1.0;
    const auto rtTicks = PlotAxis::niceTicks(rtMinimum / factor, rtMaximum / factor, 6);
    const auto intensityTicks = PlotAxis::niceTicks(0.0, intensityMaximum, 5);

    if (peakMapRtRange_)
    {
      const double leftRt = std::max(rtMinimum, peakMapRtRange_->first);
      const double rightRt = std::min(rtMaximum, peakMapRtRange_->second);
      if (rightRt >= leftRt)
      {
        painter.fillRect(QRectF(QPointF(mapX(leftRt), area.top()),
                                QPointF(mapX(rightRt), area.bottom())), PlotTheme::rangeHighlight(palette()));
      }
    }

    painter.setPen(QPen(palette().color(QPalette::Mid), 1.0, Qt::DotLine));
    for (const double tick : rtTicks)
    {
      const double x = mapX(tick * factor);
      if (x >= area.left() - 0.5 && x <= area.right() + 0.5)
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    for (const double tick : intensityTicks)
    {
      const double y = mapY(tick);
      if (y >= area.top() - 0.5 && y <= area.bottom() + 0.5)
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    static constexpr std::array<QColor, 8> colors{
      QColor(52, 152, 219), QColor(231, 76, 60), QColor(46, 204, 113), QColor(155, 89, 182),
      QColor(241, 196, 15), QColor(26, 188, 156), QColor(230, 126, 34), QColor(120, 144, 156)};
    // Polyline over the record's points; `values` supplies the intensity (raw, or
    // the cached Savitzky-Golay smoothing).
    const auto buildPath = [&](const ChromatogramRecord& record, const std::vector<double>* values)
    {
      QPainterPath path;
      bool first = true;
      for (std::size_t point = 0; point < record.points.size(); ++point)
      {
        const double intensity = values ? (*values)[point] : record.points[point].intensity;
        const QPointF position(mapX(record.points[point].rt), mapY(intensity));
        first ? path.moveTo(position) : path.lineTo(position);
        first = false;
      }
      return path;
    };

    // Clip traces to the plot rect: Savitzky-Golay can overshoot above the raw max
    // on sharp peaks, which would otherwise draw over the legend/title band.
    painter.save();
    painter.setClipRect(area);
    int colorIndex = 0;
    for (const std::size_t index : selectedIndices_)
    {
      if (index >= chromatograms_.size()) continue;
      const auto& record = chromatograms_[index];
      const QColor color = colors[static_cast<std::size_t>(colorIndex++) % colors.size()];
      // When smoothing, draw the raw trace faintly under the prominent smoothed one
      // (a true overlay). The cached vector matches record.points 1:1.
      const bool haveSmoothed = smooth_ && index < smoothed_.size()
        && smoothed_[index].size() == record.points.size();
      if (haveSmoothed)
      {
        QColor faint = color;
        faint.setAlpha(70);
        painter.setPen(QPen(faint, 1.0));
        painter.drawPath(buildPath(record, nullptr));
      }
      painter.setPen(QPen(color, 1.7));
      painter.drawPath(buildPath(record, haveSmoothed ? &smoothed_[index] : nullptr));
    }
    painter.restore();

    // Numeric tick marks + labels. RT label precision follows the tick spacing so
    // closely-spaced ticks (or large RT offsets) never render as the same text.
    const double rtStep = rtTicks.size() > 1 ? std::abs(rtTicks[1] - rtTicks[0]) : 1.0;
    const int rtDecimals = rtStep >= 1.0
      ? 0 : std::min(6, static_cast<int>(std::ceil(-std::log10(rtStep))));
    painter.setPen(palette().color(QPalette::Text));
    for (const double tick : rtTicks)
    {
      const double x = mapX(tick * factor);
      if (x < area.left() - 0.5 || x > area.right() + 0.5) continue;
      painter.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + 4.0));
      painter.drawText(QRectF(x - 42.0, area.bottom() + 5.0, 84.0, 15.0),
                       Qt::AlignHCenter | Qt::AlignTop, QString::number(tick, 'f', rtDecimals));
    }
    for (const double tick : intensityTicks)
    {
      const double y = mapY(tick);
      if (y < area.top() - 0.5 || y > area.bottom() + 0.5) continue;
      painter.drawLine(QPointF(area.left() - 4.0, y), QPointF(area.left(), y));
      painter.drawText(QRectF(0.0, y - 8.0, area.left() - 6.0, 16.0),
                       Qt::AlignRight | Qt::AlignVCenter, QString::number(tick, 'g', 3));
    }
    painter.drawText(QRectF(area.left(), height() - 23.0, area.width(), 20.0), Qt::AlignCenter,
                     rtInMinutes_ ? tr("Retention time (min)") : tr("Retention time (s)"));
    painter.save();
    painter.translate(17.0, area.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-area.height() / 2.0, -10.0, area.height(), 20.0),
                     Qt::AlignCenter, tr("Intensity"));
    painter.restore();

    colorIndex = 0;
    double legendX = area.left();
    for (const std::size_t index : selectedIndices_)
    {
      if (index >= chromatograms_.size()) continue;
      const auto& record = chromatograms_[index];
      QString label = record.nativeId.isEmpty() ? tr("Chromatogram %1").arg(index) : record.nativeId;
      label = painter.fontMetrics().elidedText(label, Qt::ElideMiddle, 150);
      // Measure before drawing so the final entry never spills past the plot edge;
      // stop early rather than clip a half-drawn label.
      const double entryWidth = 23.0 + painter.fontMetrics().horizontalAdvance(label);
      if (legendX + entryWidth > area.right()) break;
      painter.setPen(QPen(colors[static_cast<std::size_t>(colorIndex) % colors.size()], 3.0));
      painter.drawLine(QPointF(legendX, 18.0), QPointF(legendX + 18.0, 18.0));
      painter.setPen(palette().color(QPalette::Text));
      painter.drawText(QPointF(legendX + 23.0, 23.0), label);
      legendX += 8.0 + entryWidth;
      ++colorIndex;
    }

    // Hover readout: mirror the TIC — snap to the sample under the cursor on the
    // vertically nearest trace and show a colour-matched RT/intensity chip.
    if (hoverPos_ && area.contains(QPointF(*hoverPos_)))
    {
      const double hoverX = hoverPos_->x();
      const double hoverY = hoverPos_->y();
      const ChromatogramPoint* bestPoint = nullptr;
      QColor bestColor;
      QString bestLabel;
      QPointF bestPos;
      double bestVertical = std::numeric_limits<double>::max();
      // Advance the colour counter exactly as the draw/legend loops do (skip
      // out-of-range indices WITHOUT advancing) so the chip colour matches its trace.
      int traceColor = 0;
      for (const std::size_t index : selectedIndices_)
      {
        if (index >= chromatograms_.size()) continue;
        const auto& record = chromatograms_[index];
        // Empty records still consume a colour slot in the draw/legend loops, so
        // advance here too (skipping without advancing would offset the palette).
        if (record.points.empty()) { ++traceColor; continue; }
        // Points are RT-sorted, so map the cursor x back to a target RT and binary
        // search the nearest sample (O(log n)) rather than rescanning every point.
        const double targetRt = rtMinimum
          + (hoverX - area.left()) / area.width() * (rtMaximum - rtMinimum);
        auto upper = std::lower_bound(record.points.begin(), record.points.end(), targetRt,
          [](const ChromatogramPoint& point, double value) { return point.rt < value; });
        const ChromatogramPoint* nearest = upper != record.points.end() ? &*upper : nullptr;
        if (upper != record.points.begin())
        {
          const auto previous = upper - 1;
          if (!nearest || std::abs(previous->rt - targetRt) < std::abs(nearest->rt - targetRt))
            nearest = &*previous;
        }
        if (nearest)
        {
          const QPointF position(mapX(nearest->rt), mapY(nearest->intensity));
          const double vertical = std::abs(position.y() - hoverY);
          if (vertical < bestVertical)
          {
            bestVertical = vertical;
            bestPoint = nearest;
            bestPos = position;
            bestColor = colors[static_cast<std::size_t>(traceColor) % colors.size()];
            bestLabel = record.nativeId;
          }
        }
        ++traceColor;
      }
      if (bestPoint)
      {
        painter.setPen(QPen(bestColor, 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(bestPos, 3.5, 3.5);
        QString text = tr("RT %1 %2 · I %3")
          .arg(bestPoint->rt / factor, 0, 'f', rtInMinutes_ ? 3 : 2)
          .arg(rtInMinutes_ ? tr("min") : tr("s"))
          .arg(bestPoint->intensity, 0, 'g', 4);
        if (selectedIndices_.size() > 1 && !bestLabel.isEmpty())
          text = painter.fontMetrics().elidedText(bestLabel, Qt::ElideMiddle, 120)
                 + QStringLiteral(" · ") + text;
        const double textWidth = std::min<double>(
          painter.fontMetrics().horizontalAdvance(text) + 12.0, area.width());
        const QRectF box(std::clamp<double>(bestPos.x() + 8, area.left(),
                                            std::max<double>(area.left(), area.right() - textWidth)),
                         area.top() + 2, textWidth, 18);
        const QColor baseColor = palette().color(QPalette::Base);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(baseColor.red(), baseColor.green(), baseColor.blue(), 220));
        painter.drawRoundedRect(box, 3, 3);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(box.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
      }
    }
  }

  void ChromatogramPlotWidget::mousePressEvent(QMouseEvent* event)
  {
    const QRectF area = plotRect();
    const auto bounds = selectedRtBounds();
    if (event->button() == Qt::LeftButton && bounds && area.contains(event->position()))
    {
      const double fraction = std::clamp((event->position().x() - area.left()) / area.width(), 0.0, 1.0);
      emit rtActivated(bounds->first + fraction * (bounds->second - bounds->first));
    }
    QWidget::mousePressEvent(event);
  }

  void ChromatogramPlotWidget::mouseMoveEvent(QMouseEvent* event)
  {
    hoverPos_ = event->pos();
    update();
    QWidget::mouseMoveEvent(event);
  }

  void ChromatogramPlotWidget::leaveEvent(QEvent* event)
  {
    hoverPos_.reset();
    update();
    QWidget::leaveEvent(event);
  }

  ChromatogramPanelWidget::ChromatogramPanelWidget(QWidget* parent) : QWidget(parent)
  {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* controls = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("chromatogramSearch"));
    search_->setPlaceholderText(tr("Search chromatograms…"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(tr("Search chromatograms"));
    controls->addWidget(search_, 1);
    auto* display = new QToolButton(this);
    display->setObjectName(QStringLiteral("chromatogramDisplayOptions"));
    display->setText(tr("Display"));
    display->setPopupMode(QToolButton::InstantPopup);
    display->setAccessibleName(tr("Chromatogram display options"));
    auto* displayMenu = new QMenu(display);
    displayMenu->setObjectName(QStringLiteral("chromatogramDisplayMenu"));
    auto* smooth = new QCheckBox(tr("Smooth (Savitzky-Golay)"), this);
    smooth->setObjectName(QStringLiteral("chromatogramSmooth"));
    smooth->setToolTip(tr("Overlay a Savitzky-Golay lowpass of each chromatogram trace"));
    CompactControls::addMenuControl(displayMenu, smooth);
    display->setMenu(displayMenu);
    controls->addWidget(display);
    countLabel_ = new QLabel(this);
    countLabel_->setObjectName(QStringLiteral("chromatogramCountLabel"));
    controls->addWidget(countLabel_);
    auto* clearButton = CompactControls::makeIconButton(
      this, QIcon(QStringLiteral(":/icons/material-clear-all.svg")),
      tr("Clear chromatogram selection"), QStringLiteral("chromatogramClearSelection"));
    controls->addWidget(clearButton);
    auto* exportButton = CompactControls::makeIconButton(
      this, QIcon(QStringLiteral(":/icons/material-file-download.svg")),
      tr("Export selected chromatograms as TSV"), QStringLiteral("chromatogramExportTsv"));
    controls->addWidget(exportButton);
    layout->addLayout(controls);

    model_ = new ChromatogramTableModel(this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSortRole(Qt::UserRole);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setFilterKeyColumn(-1);
    proxy_->setSourceModel(model_);
    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("chromatogramTable"));
    table_->setAccessibleName(tr("Filtered chromatogram table"));
    table_->setToolTip(tr("Ctrl-click rows to compare chromatograms"));
    table_->setModel(proxy_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setSortingEnabled(true);
    table_->sortByColumn(ChromatogramTableModel::Index, Qt::AscendingOrder);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setMaximumHeight(230);
    layout->addWidget(table_);

    plot_ = new ChromatogramPlotWidget(this);
    layout->addWidget(plot_, 1);

    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ChromatogramPanelWidget::updateSelection);
    connect(search_, &QLineEdit::textChanged, proxy_, &QSortFilterProxyModel::setFilterFixedString);
    connect(search_, &QLineEdit::textChanged, this, &ChromatogramPanelWidget::updateCountLabel);
    connect(clearButton, &QToolButton::clicked, this, &ChromatogramPanelWidget::clearSelection);
    connect(exportButton, &QToolButton::clicked, this, &ChromatogramPanelWidget::exportTsv);
    connect(plot_, &ChromatogramPlotWidget::rtActivated, this, &ChromatogramPanelWidget::rtActivated);
    connect(smooth, &QCheckBox::toggled, plot_, &ChromatogramPlotWidget::setSmoothing);
    connect(proxy_, &QAbstractItemModel::rowsInserted, this, &ChromatogramPanelWidget::updateCountLabel);
    connect(proxy_, &QAbstractItemModel::rowsRemoved, this, &ChromatogramPanelWidget::updateCountLabel);
    updateCountLabel();
  }

  void ChromatogramPanelWidget::setChromatograms(
    const std::vector<ChromatogramRecord>& chromatograms)
  {
    model_->setChromatograms(chromatograms);
    plot_->setChromatograms(chromatograms);
    updateCountLabel();
  }

  void ChromatogramPanelWidget::clear()
  {
    setChromatograms({});
  }

  void ChromatogramPanelWidget::setPeakMapRange(const PlotRange& range)
  {
    plot_->setPeakMapRange(range);
  }

  std::size_t ChromatogramPanelWidget::selectedChromatogramCount() const noexcept
  {
    return plot_->selectedIndices().size();
  }

  ChromatogramPlotWidget* ChromatogramPanelWidget::plot() const noexcept { return plot_; }

  void ChromatogramPanelWidget::setRtInMinutes(bool minutes)
  {
    plot_->setRtInMinutes(minutes);
    model_->setRtInMinutes(minutes);
  }

  void ChromatogramPanelWidget::updateSelection()
  {
    std::vector<std::size_t> selected;
    for (const QModelIndex& proxyIndex : table_->selectionModel()->selectedRows())
    {
      const QModelIndex sourceIndex = proxy_->mapToSource(proxyIndex);
      if (const ChromatogramRecord* record = model_->recordForRow(sourceIndex.row()))
        selected.push_back(record->index);
    }
    std::sort(selected.begin(), selected.end());
    plot_->setSelectedIndices(selected);
    updateCountLabel();
  }

  void ChromatogramPanelWidget::clearSelection()
  {
    table_->clearSelection();
  }

  void ChromatogramPanelWidget::exportTsv()
  {
    const QString path = QFileDialog::getSaveFileName(
      this, tr("Export chromatogram summary"), QStringLiteral("chromatograms.tsv"),
      tr("Tab-separated values (*.tsv);;All files (*)"));
    if (path.isEmpty()) return;
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream stream(&output);
    for (int column = 0; column < model_->columnCount(); ++column)
    {
      if (column) stream << '\t';
      stream << model_->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
    }
    stream << '\n';
    for (int row = 0; row < proxy_->rowCount(); ++row)
    {
      for (int column = 0; column < model_->columnCount(); ++column)
      {
        if (column) stream << '\t';
        QString value = proxy_->data(proxy_->index(row, column), Qt::DisplayRole).toString();
        value.replace('\t', ' ');
        value.replace('\n', ' ');
        stream << value;
      }
      stream << '\n';
    }
    output.commit();
  }

  void ChromatogramPanelWidget::updateCountLabel()
  {
    countLabel_->setText(tr("%1 of %2 · %3 selected")
      .arg(proxy_->rowCount()).arg(model_->rowCount()).arg(selectedChromatogramCount()));
  }
}
