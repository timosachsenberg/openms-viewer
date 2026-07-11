#include "widgets/IdentificationTableWidget.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <optional>

namespace OpenMSViewer
{
  class IdentificationTableModel final : public QAbstractTableModel
  {
  public:
    enum Column { Index, Rt, Mz, Sequence, Charge, Score, Rank, LinkedSpectrum, ColumnCount };
    struct Row { std::size_t identificationIndex; std::size_t hitIndex; };

    explicit IdentificationTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    void setIdentifications(const std::vector<IdentificationRecord>& identifications)
    {
      beginResetModel();
      identifications_ = identifications;
      rebuildRows_();
      endResetModel();
    }

    void setShowAllHits(bool show)
    {
      beginResetModel();
      showAllHits_ = show;
      rebuildRows_();
      endResetModel();
    }

    [[nodiscard]] const IdentificationRecord* identificationForRow(int row) const noexcept
    {
      if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return nullptr;
      const std::size_t index = rows_[static_cast<std::size_t>(row)].identificationIndex;
      return index < identifications_.size() ? &identifications_[index] : nullptr;
    }

    [[nodiscard]] const PeptideHitRecord* hitForRow(int row) const noexcept
    {
      const IdentificationRecord* identification = identificationForRow(row);
      if (!identification) return nullptr;
      const std::size_t hitIndex = rows_[static_cast<std::size_t>(row)].hitIndex;
      return hitIndex < identification->hits.size() ? &identification->hits[hitIndex] : nullptr;
    }

    [[nodiscard]] Row rowRecord(int row) const noexcept
    {
      if (row < 0 || static_cast<std::size_t>(row) >= rows_.size())
        return {std::numeric_limits<std::size_t>::max(), 0};
      return rows_[static_cast<std::size_t>(row)];
    }

    int rowFor(std::size_t identificationIndex, std::size_t hitIndex) const noexcept
    {
      for (std::size_t row = 0; row < rows_.size(); ++row)
      {
        if (rows_[row].identificationIndex == identificationIndex
            && (rows_[row].hitIndex == hitIndex || !showAllHits_)) return static_cast<int>(row);
      }
      return -1;
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
      return parent.isValid() ? 0 : static_cast<int>(rows_.size());
    }

    int columnCount(const QModelIndex& parent = {}) const override
    {
      return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
      if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
      switch (section)
      {
        case Index: return QStringLiteral("#");
        case Rt: return QStringLiteral("RT (s)");
        case Mz: return QStringLiteral("m/z");
        case Sequence: return QStringLiteral("Sequence");
        case Charge: return QStringLiteral("Z");
        case Score: return QStringLiteral("Score");
        case Rank: return QStringLiteral("Rank");
        case LinkedSpectrum: return QStringLiteral("Spectrum");
        default: return {};
      }
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
      const IdentificationRecord* identification = identificationForRow(index.row());
      const PeptideHitRecord* hit = hitForRow(index.row());
      if (!identification) return {};

      if (role == Qt::TextAlignmentRole)
      {
        if (index.column() == Sequence) return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
      }
      if (role == Qt::ToolTipRole)
      {
        return QStringLiteral("%1 · %2 score · %3 hit(s)")
          .arg(identification->identifier, identification->scoreType)
          .arg(identification->hits.size());
      }
      if (role == Qt::UserRole)
      {
        switch (index.column())
        {
          case Index: return static_cast<qulonglong>(identification->index);
          case Rt: return identification->rt;
          case Mz: return identification->mz;
          case Sequence: return hit ? hit->sequence : QString{};
          case Charge: return hit ? hit->charge : 0;
          case Score: return hit ? hit->score : 0.0;
          case Rank: return hit ? static_cast<qulonglong>(hit->index + 1) : qulonglong{0};
          case LinkedSpectrum: return identification->spectrumIndex
            ? QVariant::fromValue(static_cast<qulonglong>(*identification->spectrumIndex + 1)) : QVariant{};
          default: return {};
        }
      }
      if (role != Qt::DisplayRole) return {};
      switch (index.column())
      {
        case Index: return static_cast<qulonglong>(identification->index);
        case Rt: return std::isfinite(identification->rt) ? QString::number(identification->rt, 'f', 2)
                                                        : QStringLiteral("-");
        case Mz: return std::isfinite(identification->mz) ? QString::number(identification->mz, 'f', 4)
                                                         : QStringLiteral("-");
        case Sequence: return hit ? hit->sequence : QStringLiteral("-");
        case Charge: return hit && hit->charge != 0 ? QString::number(hit->charge) : QStringLiteral("-");
        case Score: return hit ? QString::number(hit->score, 'g', 6) : QStringLiteral("-");
        case Rank: return hit ? QString::number(hit->index + 1) : QStringLiteral("-");
        case LinkedSpectrum: return identification->spectrumIndex
          ? QStringLiteral("#%1").arg(*identification->spectrumIndex + 1) : QStringLiteral("-");
        default: return {};
      }
    }

  private:
    void rebuildRows_()
    {
      rows_.clear();
      for (const IdentificationRecord& identification : identifications_)
      {
        if (identification.hits.empty()) rows_.push_back({identification.index, 0});
        else if (showAllHits_)
          for (const PeptideHitRecord& hit : identification.hits)
            rows_.push_back({identification.index, hit.index});
        else rows_.push_back({identification.index, 0});
      }
    }

    std::vector<IdentificationRecord> identifications_;
    std::vector<Row> rows_;
    bool showAllHits_{false};
  };

  class IdentificationFilterProxy final : public QSortFilterProxyModel
  {
  public:
    explicit IdentificationFilterProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent)
    {
      setSortRole(Qt::UserRole);
      setDynamicSortFilter(true);
    }

    void setLinkedOnly(bool linkedOnly) { linkedOnly_ = linkedOnly; invalidateRowsFilter(); }
    void setSequence(QString sequence) { sequence_ = std::move(sequence); invalidateRowsFilter(); }
    void setMinimumScore(std::optional<double> score) { minimumScore_ = score; invalidateRowsFilter(); }

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex&) const override
    {
      const auto* model = static_cast<const IdentificationTableModel*>(sourceModel());
      const IdentificationRecord* identification = model ? model->identificationForRow(sourceRow) : nullptr;
      const PeptideHitRecord* hit = model ? model->hitForRow(sourceRow) : nullptr;
      if (!identification) return false;
      if (linkedOnly_ && !identification->spectrumIndex) return false;
      if (!sequence_.isEmpty() && (!hit || !hit->sequence.contains(sequence_, Qt::CaseInsensitive))) return false;
      if (minimumScore_ && (!hit || hit->score < *minimumScore_)) return false;
      return true;
    }

  private:
    bool linkedOnly_{false};
    QString sequence_;
    std::optional<double> minimumScore_;
  };

  IdentificationTableWidget::IdentificationTableWidget(QWidget* parent) : QWidget(parent)
  {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(new QLabel(tr("Select an identification to open its linked MS/MS spectrum"), this));

    auto* controls = new QHBoxLayout;
    viewMode_ = new QComboBox(this);
    viewMode_->addItems({tr("All identifications"), tr("Linked only")});
    controls->addWidget(viewMode_);
    sequenceFilter_ = new QLineEdit(this);
    sequenceFilter_->setPlaceholderText(tr("Sequence filter"));
    sequenceFilter_->setClearButtonEnabled(true);
    controls->addWidget(sequenceFilter_);
    minimumScore_ = new QLineEdit(this);
    minimumScore_->setPlaceholderText(tr("Minimum score"));
    minimumScore_->setMaximumWidth(120);
    controls->addWidget(minimumScore_);
    showAllHits_ = new QCheckBox(tr("All hits"), this);
    controls->addWidget(showAllHits_);
    auto* reset = new QPushButton(tr("Reset"), this);
    controls->addWidget(reset);
    controls->addStretch();
    countLabel_ = new QLabel(this);
    controls->addWidget(countLabel_);
    auto* exportButton = new QPushButton(tr("Export TSV…"), this);
    controls->addWidget(exportButton);
    layout->addLayout(controls);

    model_ = new IdentificationTableModel(this);
    proxy_ = new IdentificationFilterProxy(this);
    proxy_->setSourceModel(model_);
    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("identificationTable"));
    table_->setAccessibleName(tr("Filtered identification table"));
    table_->setModel(proxy_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSortingEnabled(true);
    table_->sortByColumn(IdentificationTableModel::Index, Qt::AscendingOrder);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 3);

    details_ = new QPlainTextEdit(this);
    details_->setReadOnly(true);
    details_->setPlaceholderText(tr("Identification and hit metadata"));
    details_->setMaximumBlockCount(500);
    layout->addWidget(details_, 1);

    connect(viewMode_, qOverload<int>(&QComboBox::currentIndexChanged), this, &IdentificationTableWidget::updateFilters);
    connect(sequenceFilter_, &QLineEdit::textChanged, this, &IdentificationTableWidget::updateFilters);
    connect(minimumScore_, &QLineEdit::textChanged, this, &IdentificationTableWidget::updateFilters);
    connect(showAllHits_, &QCheckBox::toggled, this, &IdentificationTableWidget::rebuildRows);
    connect(reset, &QPushButton::clicked, this, [this]
    {
      viewMode_->setCurrentIndex(0);
      sequenceFilter_->clear();
      minimumScore_->clear();
      showAllHits_->setChecked(false);
      updateFilters();
    });
    connect(exportButton, &QPushButton::clicked, this, &IdentificationTableWidget::exportTsv);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& proxyIndex)
    {
      if (!proxyIndex.isValid()) return;
      const QModelIndex source = proxy_->mapToSource(proxyIndex);
      const auto row = model_->rowRecord(source.row());
      updateDetails(source.row());
      if (!synchronizingSelection_
          && row.identificationIndex != std::numeric_limits<std::size_t>::max())
        emit identificationActivated(row.identificationIndex, row.hitIndex);
    });
    updateCountLabel();
  }

  void IdentificationTableWidget::setIdentifications(const std::vector<IdentificationRecord>& identifications)
  {
    model_->setIdentifications(identifications);
    details_->clear();
    updateCountLabel();
  }

  void IdentificationTableWidget::clear()
  {
    model_->setIdentifications({});
    details_->clear();
    updateCountLabel();
  }

  void IdentificationTableWidget::selectIdentification(std::size_t identificationIndex, std::size_t hitIndex)
  {
    const int row = model_->rowFor(identificationIndex, hitIndex);
    if (row < 0) return;
    const QModelIndex proxyIndex = proxy_->mapFromSource(model_->index(row, 0));
    if (!proxyIndex.isValid()) return;
    QScopedValueRollback guard(synchronizingSelection_, true);
    table_->selectRow(proxyIndex.row());
    table_->scrollTo(proxyIndex, QAbstractItemView::PositionAtCenter);
    updateDetails(row);
  }

  void IdentificationTableWidget::updateFilters()
  {
    auto* proxy = static_cast<IdentificationFilterProxy*>(proxy_);
    proxy->setLinkedOnly(viewMode_->currentIndex() == 1);
    proxy->setSequence(sequenceFilter_->text().trimmed());
    bool valid = false;
    const double score = minimumScore_->text().toDouble(&valid);
    proxy->setMinimumScore(valid ? std::optional<double>{score} : std::nullopt);
    updateCountLabel();
  }

  void IdentificationTableWidget::rebuildRows()
  {
    model_->setShowAllHits(showAllHits_->isChecked());
    updateCountLabel();
  }

  void IdentificationTableWidget::exportTsv()
  {
    const QString path = QFileDialog::getSaveFileName(this, tr("Export identifications"),
      QStringLiteral("identifications.tsv"), tr("Tab-separated values (*.tsv);;All files (*)"));
    if (path.isEmpty()) return;
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream stream(&output);
    stream << "#\tRT (s)\tm/z\tSequence\tZ\tScore\tRank\tSpectrum\n";
    for (int row = 0; row < proxy_->rowCount(); ++row)
    {
      for (int column = 0; column < IdentificationTableModel::ColumnCount; ++column)
      {
        if (column) stream << '\t';
        stream << proxy_->index(row, column).data(Qt::DisplayRole).toString();
      }
      stream << '\n';
    }
    output.commit();
  }

  void IdentificationTableWidget::updateCountLabel()
  {
    countLabel_->setText(tr("%1 of %2 rows").arg(proxy_->rowCount()).arg(model_->rowCount()));
  }

  void IdentificationTableWidget::updateDetails(int sourceRow)
  {
    const IdentificationRecord* identification = model_->identificationForRow(sourceRow);
    const PeptideHitRecord* hit = model_->hitForRow(sourceRow);
    if (!identification) return;
    QStringList lines;
    lines << tr("Identification #%1 · %2").arg(identification->index).arg(identification->identifier)
          << tr("Score type: %1 (%2 is better)").arg(identification->scoreType,
               identification->higherScoreBetter ? tr("higher") : tr("lower"));
    if (identification->spectrumIndex)
      lines << tr("Linked spectrum #%1 · ΔRT %2 s · Δm/z %3 Da")
        .arg(*identification->spectrumIndex + 1)
        .arg(identification->linkRtError, 0, 'f', 3)
        .arg(identification->linkMzError, 0, 'f', 5);
    for (const auto& [key, value] : identification->metaValues)
      lines << QStringLiteral("PID:%1 = %2").arg(key, value);
    if (hit)
    {
      lines << QString{} << tr("Hit #%1 · %2 · z=%3 · score %4")
        .arg(hit->index + 1).arg(hit->sequence).arg(hit->charge).arg(hit->score, 0, 'g', 8);
      for (const auto& [key, value] : hit->metaValues)
        lines << QStringLiteral("Hit:%1 = %2").arg(key, value);
      if (!hit->peakAnnotations.empty())
        lines << tr("External fragment annotations: %1").arg(hit->peakAnnotations.size());
    }
    details_->setPlainText(lines.join(QLatin1Char('\n')));
  }
}
