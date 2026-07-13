#include "widgets/FeatureTableWidget.h"
#include "widgets/CompactControls.h"

#include "model/RtUnit.h"

#include <QAbstractTableModel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace OpenMSViewer
{
  class FeatureTableModel final : public QAbstractTableModel
  {
  public:
    enum Column { Index, Rt, Mz, Intensity, Charge, Quality, ColumnCount };

    explicit FeatureTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    void setFeatures(const std::vector<FeatureRecord>& features)
    {
      beginResetModel();
      features_ = features;
      endResetModel();
    }

    void setRtInMinutes(bool minutes)
    {
      if (rtInMinutes_ == minutes) return;
      rtInMinutes_ = minutes;
      emit headerDataChanged(Qt::Horizontal, Rt, Rt);
      if (!features_.empty())
        emit dataChanged(index(0, Rt), index(rowCount() - 1, Rt), {Qt::DisplayRole});
    }

    [[nodiscard]] const FeatureRecord* featureForRow(int row) const noexcept
    {
      if (row < 0 || static_cast<std::size_t>(row) >= features_.size()) return nullptr;
      return &features_[static_cast<std::size_t>(row)];
    }

    int rowForFeature(std::size_t index) const noexcept
    {
      const auto found = std::find_if(features_.begin(), features_.end(), [index](const FeatureRecord& feature)
      {
        return feature.index == index;
      });
      return found == features_.end() ? -1 : static_cast<int>(std::distance(features_.begin(), found));
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
      return parent.isValid() ? 0 : static_cast<int>(features_.size());
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
        case Rt: return RtUnit::columnHeader(rtInMinutes_);
        case Mz: return QStringLiteral("m/z");
        case Intensity: return QStringLiteral("Intensity");
        case Charge: return QStringLiteral("Z");
        case Quality: return QStringLiteral("Quality");
        default: return {};
      }
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
      const FeatureRecord* feature = featureForRow(index.row());
      if (!feature || index.column() < 0 || index.column() >= ColumnCount) return {};

      if (role == Qt::TextAlignmentRole)
      {
        return index.column() == Index || index.column() == Charge
                 ? QVariant::fromValue(Qt::AlignCenter)
                 : QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
      }

      if (role == Qt::UserRole)
      {
        switch (index.column())
        {
          case Index: return static_cast<qulonglong>(feature->index);
          case Rt: return feature->rt;
          case Mz: return feature->mz;
          case Intensity: return feature->intensity;
          case Charge: return feature->charge;
          case Quality: return feature->quality;
          default: return {};
        }
      }

      if (role != Qt::DisplayRole) return {};
      switch (index.column())
      {
        case Index: return static_cast<qulonglong>(feature->index);
        case Rt: return RtUnit::format(feature->rt, rtInMinutes_);
        case Mz: return QString::number(feature->mz, 'f', 4);
        case Intensity: return QString::number(feature->intensity, 'e', 2);
        case Charge: return feature->charge == 0 ? QStringLiteral("-") : QString::number(feature->charge);
        case Quality: return feature->quality > 0.0 ? QString::number(feature->quality, 'f', 3)
                                                   : QStringLiteral("-");
        default: return {};
      }
    }

  private:
    std::vector<FeatureRecord> features_;
    bool rtInMinutes_{false};
  };

  class FeatureFilterProxyModel final : public QSortFilterProxyModel
  {
  public:
    explicit FeatureFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent)
    {
      setSortRole(Qt::UserRole);
      setDynamicSortFilter(true);
    }

    void setMinimumIntensity(double value) { minimumIntensity_ = value; invalidateRowsFilter(); }
    void setMinimumQuality(double value) { minimumQuality_ = value; invalidateRowsFilter(); }
    void setChargeMode(int value) { chargeMode_ = value; invalidateRowsFilter(); }
    void setSearch(QString value) { search_ = std::move(value); invalidateRowsFilter(); }

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
      const auto* model = static_cast<const FeatureTableModel*>(sourceModel());
      const FeatureRecord* feature = model ? model->featureForRow(sourceRow) : nullptr;
      if (!feature) return false;
      if (feature->intensity < minimumIntensity_ || feature->quality < minimumQuality_) return false;
      if (!search_.isEmpty())
      {
        const QString haystack = QStringLiteral("%1 %2 %3 %4 %5 %6")
          .arg(feature->index).arg(feature->rt, 0, 'f', 3).arg(feature->mz, 0, 'f', 5)
          .arg(feature->intensity, 0, 'g', 8).arg(feature->charge).arg(feature->quality, 0, 'f', 3);
        if (!haystack.contains(search_, Qt::CaseInsensitive)) return false;
      }
      if (chargeMode_ == 0) return true;
      if (chargeMode_ == 5) return feature->charge >= 5;
      return feature->charge == chargeMode_;
    }

  private:
    double minimumIntensity_{0.0};
    double minimumQuality_{0.0};
    int chargeMode_{0};
    QString search_;
  };

  FeatureTableWidget::FeatureTableWidget(QWidget* parent) : QWidget(parent)
  {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* filters = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("featureSearch"));
    search_->setPlaceholderText(tr("Search features…"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(tr("Search features"));
    filters->addWidget(search_, 1);

    auto* filterButton = new QToolButton(this);
    filterButton->setObjectName(QStringLiteral("featureFilterOptions"));
    filterButton->setText(tr("Filters"));
    filterButton->setPopupMode(QToolButton::InstantPopup);
    filterButton->setAccessibleName(tr("Feature table filters"));
    auto* filterMenu = new QMenu(filterButton);
    filterMenu->setObjectName(QStringLiteral("featureFilterMenu"));
    minimumIntensity_ = new QDoubleSpinBox(this);
    minimumIntensity_->setRange(0.0, std::numeric_limits<double>::max());
    minimumIntensity_->setDecimals(0);
    minimumIntensity_->setSpecialValueText(tr("Any"));
    minimumIntensity_->setMaximumWidth(140);
    CompactControls::addLabeledMenuControl(
      filterMenu, tr("Minimum intensity"), minimumIntensity_);

    minimumQuality_ = new QDoubleSpinBox(this);
    minimumQuality_->setRange(0.0, 1.0);
    minimumQuality_->setDecimals(3);
    minimumQuality_->setSingleStep(0.05);
    minimumQuality_->setSpecialValueText(tr("Any"));
    minimumQuality_->setMaximumWidth(100);
    CompactControls::addLabeledMenuControl(
      filterMenu, tr("Minimum quality"), minimumQuality_);

    charge_ = new QComboBox(this);
    charge_->addItems({tr("All"), QStringLiteral("1"), QStringLiteral("2"),
                       QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5+")});
    CompactControls::addLabeledMenuControl(filterMenu, tr("Charge"), charge_);
    filterMenu->addSeparator();
    auto* reset = CompactControls::makeIconButton(
      filterMenu, QIcon(QStringLiteral(":/icons/material-clear-all.svg")),
      tr("Reset feature filters"), QStringLiteral("featureResetFilters"));
    CompactControls::addMenuControl(filterMenu, reset);
    filterButton->setMenu(filterMenu);
    filters->addWidget(filterButton);
    countLabel_ = new QLabel(this);
    filters->addWidget(countLabel_);
    auto* exportButton = CompactControls::makeIconButton(
      this, QIcon(QStringLiteral(":/icons/material-file-download.svg")),
      tr("Export filtered features as TSV"), QStringLiteral("featureExportTsv"));
    filters->addWidget(exportButton);
    layout->addLayout(filters);

    model_ = new FeatureTableModel(this);
    auto* featureProxy = new FeatureFilterProxyModel(this);
    proxy_ = featureProxy;
    proxy_->setSourceModel(model_);

    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("featureTable"));
    table_->setAccessibleName(tr("Filtered feature table"));
    table_->setToolTip(tr("Click a row to select and zoom to that feature"));
    table_->setModel(proxy_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSortingEnabled(true);
    table_->sortByColumn(FeatureTableModel::Intensity, Qt::DescendingOrder);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(table_);

    connect(minimumIntensity_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &FeatureTableWidget::updateFilters);
    connect(search_, &QLineEdit::textChanged, this, &FeatureTableWidget::updateFilters);
    connect(minimumQuality_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &FeatureTableWidget::updateFilters);
    connect(charge_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &FeatureTableWidget::updateFilters);
    connect(reset, &QToolButton::clicked, this, &FeatureTableWidget::resetFilters);
    connect(exportButton, &QToolButton::clicked, this, &FeatureTableWidget::exportTsv);
    const auto activateRow = [this](const QModelIndex& proxyIndex)
    {
      if (!proxyIndex.isValid() || synchronizingSelection_) return;
      const QModelIndex sourceIndex = proxy_->mapToSource(proxyIndex);
      if (const FeatureRecord* feature = model_->featureForRow(sourceIndex.row()))
        emit featureActivated(feature->index);
    };
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, activateRow);
    // Clicking the already-current row does not change the current index, so
    // re-emit activation on click to re-zoom to the same feature.
    connect(table_, &QTableView::clicked, this, activateRow);
    connect(proxy_, &QAbstractItemModel::rowsInserted, this, &FeatureTableWidget::updateCountLabel);
    connect(proxy_, &QAbstractItemModel::rowsRemoved, this, &FeatureTableWidget::updateCountLabel);
    connect(proxy_, &QAbstractItemModel::modelReset, this, &FeatureTableWidget::updateCountLabel);
    updateCountLabel();
  }

  void FeatureTableWidget::setFeatures(const std::vector<FeatureRecord>& features)
  {
    model_->setFeatures(features);
    updateCountLabel();
  }

  void FeatureTableWidget::clear()
  {
    model_->setFeatures({});
    updateCountLabel();
  }

  void FeatureTableWidget::selectFeature(std::size_t featureIndex)
  {
    const int row = model_->rowForFeature(featureIndex);
    if (row < 0) return;
    const QModelIndex source = model_->index(row, 0);
    const QModelIndex proxy = proxy_->mapFromSource(source);
    if (!proxy.isValid()) return;
    QScopedValueRollback guard(synchronizingSelection_, true);
    table_->selectRow(proxy.row());
    table_->scrollTo(proxy, QAbstractItemView::PositionAtCenter);
  }

  void FeatureTableWidget::setRtInMinutes(bool minutes)
  {
    model_->setRtInMinutes(minutes);
  }

  void FeatureTableWidget::updateFilters()
  {
    auto* proxy = static_cast<FeatureFilterProxyModel*>(proxy_);
    proxy->setMinimumIntensity(minimumIntensity_->value());
    proxy->setMinimumQuality(minimumQuality_->value());
    proxy->setChargeMode(charge_->currentIndex());
    proxy->setSearch(search_->text().trimmed());
    updateCountLabel();
  }

  void FeatureTableWidget::resetFilters()
  {
    minimumIntensity_->setValue(0.0);
    search_->clear();
    minimumQuality_->setValue(0.0);
    charge_->setCurrentIndex(0);
    updateFilters();
  }

  void FeatureTableWidget::exportTsv()
  {
    const QString path = QFileDialog::getSaveFileName(
      this, tr("Export filtered features"), QStringLiteral("features.tsv"),
      tr("Tab-separated values (*.tsv);;All files (*)"));
    if (path.isEmpty()) return;

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream stream(&output);
    stream << "#\tRT (s)\tm/z\tIntensity\tZ\tQuality\n";
    for (int proxyRow = 0; proxyRow < proxy_->rowCount(); ++proxyRow)
    {
      const QModelIndex source = proxy_->mapToSource(proxy_->index(proxyRow, 0));
      const FeatureRecord* feature = model_->featureForRow(source.row());
      if (!feature) continue;
      stream << feature->index << '\t' << QString::number(feature->rt, 'f', 6) << '\t'
             << QString::number(feature->mz, 'f', 8) << '\t'
             << QString::number(feature->intensity, 'g', 12) << '\t'
             << feature->charge << '\t' << QString::number(feature->quality, 'g', 8) << '\n';
    }
    output.commit();
  }

  void FeatureTableWidget::updateCountLabel()
  {
    countLabel_->setText(tr("%1 of %2 features").arg(proxy_->rowCount()).arg(model_->rowCount()));
  }
}
