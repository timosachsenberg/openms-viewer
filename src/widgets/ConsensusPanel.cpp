#include "widgets/ConsensusPanel.h"

#include "widgets/ConsensusQuantChart.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QScopedValueRollback>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>

#include <cmath>
#include <map>

namespace OpenMSViewer
{
  class ConsensusFeatureModel final : public QAbstractTableModel
  {
  public:
    enum Column { Rt, Mz, Charge, Quality, Maps, Intensity, Sum, Peptide, ColumnCount };
    using QAbstractTableModel::QAbstractTableModel;

    void setRows(std::vector<ConsensusFeatureRecord> rows)
    {
      beginResetModel();
      rows_ = std::move(rows);
      endResetModel();
    }
    [[nodiscard]] const ConsensusFeatureRecord& row(int index) const
    { return rows_[static_cast<std::size_t>(index)]; }

    int rowCount(const QModelIndex& parent = {}) const override
    { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    int columnCount(const QModelIndex& = {}) const override { return ColumnCount; }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
      if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
      switch (section)
      {
        case Rt:        return QStringLiteral("RT");
        case Mz:        return QStringLiteral("m/z");
        case Charge:    return QStringLiteral("z");
        case Quality:   return QStringLiteral("Quality");
        case Maps:      return QStringLiteral("Maps");
        case Intensity: return QStringLiteral("Intensity");
        case Sum:       return QStringLiteral("Σ maps");
        case Peptide:   return QStringLiteral("Peptide");
      }
      return {};
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
      if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::UserRole)) return {};
      const ConsensusFeatureRecord& record = rows_[static_cast<std::size_t>(index.row())];
      const bool sortRole = role == Qt::UserRole;
      switch (index.column())
      {
        case Rt:        return sortRole ? QVariant(record.rt) : QVariant(QString::number(record.rt, 'f', 1));
        case Mz:        return sortRole ? QVariant(record.mz) : QVariant(QString::number(record.mz, 'f', 4));
        case Charge:    return record.charge;
        case Quality:   return sortRole ? QVariant(record.quality) : QVariant(QString::number(record.quality, 'g', 3));
        case Maps:      return sortRole ? QVariant(record.coveredMaps)
                                        : QVariant(QStringLiteral("%1/%2").arg(record.coveredMaps).arg(record.totalMaps));
        case Intensity: return sortRole ? QVariant(record.storedIntensity) : QVariant(QString::number(record.storedIntensity, 'g', 4));
        case Sum:       return sortRole ? QVariant(record.sumIntensity) : QVariant(QString::number(record.sumIntensity, 'g', 4));
        case Peptide:   return record.bestPeptide;
      }
      return {};
    }

  private:
    std::vector<ConsensusFeatureRecord> rows_;
  };

  class ConsensusFilterProxy final : public QSortFilterProxyModel
  {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setSearch(const QString& text) { search_ = text.trimmed(); invalidateFilter(); }
    void setMinCoverage(int minCoverage) { minCoverage_ = minCoverage; invalidateFilter(); }

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex&) const override
    {
      const auto* model = static_cast<const ConsensusFeatureModel*>(sourceModel());
      if (!model) return true;
      const ConsensusFeatureRecord& record = model->row(sourceRow);
      if (record.coveredMaps < minCoverage_) return false;
      if (!search_.isEmpty() && !record.bestPeptide.contains(search_, Qt::CaseInsensitive))
        return false;
      return true;
    }

  private:
    QString search_;
    int minCoverage_{0};
  };

  ConsensusPanel::ConsensusPanel(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("consensusPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* controls = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("consensusSearch"));
    search_->setPlaceholderText(tr("Search peptide…"));
    search_->setClearButtonEnabled(true);
    minMaps_ = new QSpinBox(this);
    minMaps_->setObjectName(QStringLiteral("consensusMinMaps"));
    minMaps_->setPrefix(tr("≥ maps "));
    minMaps_->setRange(0, 1);
    logScale_ = new QCheckBox(tr("Log scale"), this);
    logScale_->setObjectName(QStringLiteral("consensusLogScale"));
    countLabel_ = new QLabel(this);
    controls->addWidget(search_, 1);
    controls->addWidget(minMaps_);
    controls->addWidget(logScale_);
    controls->addWidget(countLabel_);
    layout->addLayout(controls);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    model_ = new ConsensusFeatureModel(this);
    proxy_ = new ConsensusFilterProxy(this);
    proxy_->setSourceModel(model_);
    proxy_->setSortRole(Qt::UserRole);
    view_ = new QTableView(this);
    view_->setObjectName(QStringLiteral("consensusTable"));
    view_->setModel(proxy_);
    view_->setSortingEnabled(true);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->horizontalHeader()->setStretchLastSection(true);
    view_->verticalHeader()->setVisible(false);
    splitter->addWidget(view_);

    auto* detail = new QWidget(this);
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLabel_ = new QLabel(detail);
    detailLabel_->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    detailLayout->addWidget(detailLabel_);
    chart_ = new ConsensusQuantChart(detail);
    detailLayout->addWidget(chart_, 1);
    splitter->addWidget(detail);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    connect(view_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current) { onFeatureActivated(current); });
    // A deliberate double-click (never mere selection) drills into the source scan,
    // so highlighting on arrow-key browsing stays non-intrusive. doubleClicked is
    // also reliable across styles/platforms, unlike the activation-trigger-dependent
    // QAbstractItemView::activated.
    connect(view_, &QTableView::doubleClicked, this, [this](const QModelIndex& index)
    {
      if (!index.isValid()) return;
      const int sourceRow = proxy_->mapToSource(index).row();
      if (sourceRow >= 0 && sourceRow < static_cast<int>(features_.size()))
        emit featureDrillDown(static_cast<qint64>(model_->row(sourceRow).index));
    });
    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString& text) { proxy_->setSearch(text); reselectAfterFilter(); });
    connect(minMaps_, &QSpinBox::valueChanged, this,
            [this](int value) { proxy_->setMinCoverage(value); reselectAfterFilter(); });
    connect(logScale_, &QCheckBox::toggled, this,
            [this](bool logScale) { if (chart_) chart_->setLogScale(logScale); });
  }

  void ConsensusPanel::setData(std::shared_ptr<OpenMS::ConsensusMap> map,
                               std::vector<ConsensusFeatureRecord> features,
                               std::vector<ConsensusColumn> columns,
                               const QString& experimentType)
  {
    map_ = std::move(map);
    features_ = std::move(features);
    columns_ = std::move(columns);
    experimentType_ = experimentType;
    minMaps_->setRange(0, columns_.empty() ? 1 : static_cast<int>(columns_.size()));
    model_->setRows(features_);
    view_->resizeColumnsToContents();
    countLabel_->setText(tr("%1 features · %2 maps%3").arg(features_.size()).arg(columns_.size())
      .arg(experimentType_.isEmpty() ? QString() : QStringLiteral(" · %1").arg(experimentType_)));
    chart_->clear();
    detailLabel_->clear();
    if (!features_.empty()) view_->selectRow(0);
  }

  void ConsensusPanel::clear()
  {
    map_.reset();
    features_.clear();
    columns_.clear();
    model_->setRows({});
    chart_->clear();
    detailLabel_->clear();
    countLabel_->clear();
  }

  void ConsensusPanel::selectFeature(std::size_t index)
  {
    for (int row = 0; row < proxy_->rowCount(); ++row)
    {
      const int sourceRow = proxy_->mapToSource(proxy_->index(row, 0)).row();
      if (sourceRow >= 0 && model_->row(sourceRow).index == index)
      {
        QScopedValueRollback<bool> guard(synchronizing_, true);
        view_->selectRow(row);
        onFeatureActivated(proxy_->index(row, 0));
        return;
      }
    }
    clearDetails();  // the requested feature is filtered out of the current view
  }

  void ConsensusPanel::clearDetails()
  {
    chart_->clear();
    detailLabel_->clear();
  }

  void ConsensusPanel::reselectAfterFilter()
  {
    if (proxy_->rowCount() == 0) { clearDetails(); return; }
    if (!view_->currentIndex().isValid()) view_->selectRow(0);
  }

  void ConsensusPanel::onFeatureActivated(const QModelIndex& current)
  {
    if (!map_) return;
    if (!current.isValid()) { clearDetails(); return; }
    const int sourceRow = proxy_->mapToSource(current).row();
    if (sourceRow < 0 || sourceRow >= static_cast<int>(features_.size())) return;
    const ConsensusFeatureRecord& record = model_->row(sourceRow);

    const auto handles = ConsensusDocument::handlesFor(*map_, record.index);
    chart_->setData(handles, columns_);

    // Coefficient of variation over PER-MAP intensities (what the chart shows —
    // multiple handles for one map are summed first), not individual handles.
    std::map<std::int64_t, double> perMap;
    for (const ConsensusHandle& handle : handles)
      if (std::isfinite(handle.intensity)) perMap[handle.mapIndex] += handle.intensity;
    double cv = 0.0;
    if (perMap.size() > 1)
    {
      double sum = 0.0;
      for (const auto& [mapIndex, value] : perMap) sum += value;
      const double mean = sum / perMap.size();
      if (mean > 0.0)
      {
        double variance = 0.0;
        for (const auto& [mapIndex, value] : perMap) variance += (value - mean) * (value - mean);
        cv = std::sqrt(variance / perMap.size()) / mean * 100.0;
      }
    }
    detailLabel_->setText(tr("%1coverage %2/%3 · CV %4% · stored %5 · Σ %6")
      .arg(record.bestPeptide.isEmpty() ? QString() : QStringLiteral("%1 · ").arg(record.bestPeptide))
      .arg(record.coveredMaps).arg(record.totalMaps)
      .arg(QString::number(cv, 'f', 1))
      .arg(QString::number(record.storedIntensity, 'g', 4))
      .arg(QString::number(record.sumIntensity, 'g', 4)));

    if (!synchronizing_) emit featureActivated(static_cast<qint64>(record.index));
  }
}
