#include "widgets/ChromatogramPanelWidget.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSaveFile>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTextStream>
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
        case RtRange: return QStringLiteral("RT range (s)");
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
            : QStringLiteral("%1–%2").arg(record->rtMin, 0, 'f', 2).arg(record->rtMax, 0, 'f', 2);
        case PointCount: return static_cast<qulonglong>(record->points.size());
        case MaximumIntensity: return QString::number(record->maximumIntensity, 'e', 2);
        case TotalIntensity: return QString::number(record->totalIntensity, 'e', 2);
        default: return {};
      }
    }

  private:
    std::vector<ChromatogramRecord> chromatograms_;
  };

  ChromatogramPlotWidget::ChromatogramPlotWidget(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("chromatogramPlot"));
    setMinimumHeight(210);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Chromatogram traces"));
    setAccessibleDescription(tr("Selected chromatogram traces linked to the peak-map RT range."));
  }

  void ChromatogramPlotWidget::setChromatograms(
    const std::vector<ChromatogramRecord>& chromatograms)
  {
    chromatograms_ = chromatograms;
    selectedIndices_.clear();
    update();
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
      painter.setPen(palette().color(QPalette::Text));
      painter.drawText(area, Qt::AlignCenter,
                       tr("Select one or more chromatograms in the table"));
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

    if (peakMapRtRange_)
    {
      const double leftRt = std::max(rtMinimum, peakMapRtRange_->first);
      const double rightRt = std::min(rtMaximum, peakMapRtRange_->second);
      if (rightRt >= leftRt)
      {
        painter.fillRect(QRectF(QPointF(mapX(leftRt), area.top()),
                                QPointF(mapX(rightRt), area.bottom())), QColor(255, 210, 40, 52));
      }
    }

    painter.setPen(QPen(palette().color(QPalette::Mid), 1.0, Qt::DashLine));
    for (int tick = 1; tick < 5; ++tick)
    {
      const double x = area.left() + area.width() * tick / 5.0;
      const double y = area.top() + area.height() * tick / 5.0;
      painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
      painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    static constexpr std::array<QColor, 8> colors{
      QColor(52, 152, 219), QColor(231, 76, 60), QColor(46, 204, 113), QColor(155, 89, 182),
      QColor(241, 196, 15), QColor(26, 188, 156), QColor(230, 126, 34), QColor(236, 240, 241)};
    int colorIndex = 0;
    for (const std::size_t index : selectedIndices_)
    {
      if (index >= chromatograms_.size()) continue;
      const auto& record = chromatograms_[index];
      QPainterPath path;
      bool first = true;
      for (const ChromatogramPoint& point : record.points)
      {
        const QPointF position(mapX(point.rt), mapY(point.intensity));
        first ? path.moveTo(position) : path.lineTo(position);
        first = false;
      }
      painter.setPen(QPen(colors[static_cast<std::size_t>(colorIndex) % colors.size()], 1.7));
      painter.drawPath(path);
      ++colorIndex;
    }

    painter.setPen(palette().color(QPalette::Text));
    const double factor = rtInMinutes_ ? 60.0 : 1.0;
    for (int tick = 0; tick <= 5; ++tick)
    {
      const double fraction = tick / 5.0;
      const double x = area.left() + fraction * area.width();
      const double rt = rtMinimum + fraction * (rtMaximum - rtMinimum);
      painter.drawText(QRectF(x - 40.0, area.bottom() + 5.0, 80.0, 20.0),
                       Qt::AlignHCenter | Qt::AlignTop, QString::number(rt / factor, 'f', 2));
      const double intensity = intensityMaximum * (1.0 - fraction);
      const double y = area.top() + fraction * area.height();
      painter.drawText(QRectF(0.0, y - 10.0, area.left() - 8.0, 20.0),
                       Qt::AlignRight | Qt::AlignVCenter, QString::number(intensity, 'g', 3));
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
      painter.setPen(QPen(colors[static_cast<std::size_t>(colorIndex) % colors.size()], 3.0));
      painter.drawLine(QPointF(legendX, 18.0), QPointF(legendX + 18.0, 18.0));
      painter.setPen(palette().color(QPalette::Text));
      painter.drawText(QPointF(legendX + 23.0, 23.0), label);
      legendX += 31.0 + painter.fontMetrics().horizontalAdvance(label);
      if (legendX > area.right() - 100.0) break;
      ++colorIndex;
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
    controls->addWidget(search_);
    controls->addWidget(new QLabel(tr("Ctrl-click rows to compare"), this));
    controls->addStretch();
    countLabel_ = new QLabel(this);
    countLabel_->setObjectName(QStringLiteral("chromatogramCountLabel"));
    controls->addWidget(countLabel_);
    auto* clearButton = new QPushButton(tr("Clear selection"), this);
    controls->addWidget(clearButton);
    auto* exportButton = new QPushButton(tr("Export TSV…"), this);
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
    connect(clearButton, &QPushButton::clicked, this, &ChromatogramPanelWidget::clearSelection);
    connect(exportButton, &QPushButton::clicked, this, &ChromatogramPanelWidget::exportTsv);
    connect(plot_, &ChromatogramPlotWidget::rtActivated, this, &ChromatogramPanelWidget::rtActivated);
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
