#include "widgets/SpectrumTableWidget.h"
#include "widgets/CompactControls.h"

#include "model/RtUnit.h"

#include <QAbstractTableModel>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
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
#include <optional>

namespace OpenMSViewer
{
  namespace
  {
    QString formatMetadata(const IdentificationRecord* identification,
                           const PeptideHitRecord* hit)
    {
      QStringList values;
      if (identification)
      {
        for (const auto& [key, value] : identification->metaValues)
          values.push_back(QStringLiteral("ID:%1=%2").arg(key, value));
      }
      if (hit)
      {
        for (const auto& [key, value] : hit->metaValues)
          values.push_back(QStringLiteral("Hit:%1=%2").arg(key, value));
      }
      return values.join(QStringLiteral("; "));
    }

    void setNumericValidity(QLineEdit* edit, bool valid, const QString& normalTip,
                            const QString& errorTip = {})
    {
      edit->setProperty("invalidInput", !valid);
      edit->setStyleSheet(valid ? QString{}
                                : QStringLiteral("QLineEdit { border: 1px solid #c43c3c; }"));
      edit->setToolTip(valid ? normalTip : errorTip);
    }
  }

  class SpectrumTableModel final : public QAbstractTableModel
  {
  public:
    enum Column
    {
      Index, Rt, MsLevel, CompensationVoltage, PeakCount, Tic, BasePeak,
      MzRange, PrecursorMz, Charge, Rank, Sequence, Score, Metadata, ColumnCount
    };

    struct Row
    {
      std::size_t spectrumIndex{0};
      std::optional<std::size_t> identificationIndex;
      std::optional<std::size_t> hitIndex;
    };

    explicit SpectrumTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    void setData(const std::vector<SpectrumRecord>& spectra,
                 const std::vector<IdentificationRecord>& identifications)
    {
      beginResetModel();
      spectra_ = spectra;
      identifications_ = identifications;
      rebuildRows();
      endResetModel();
    }

    void setAllHits(bool enabled)
    {
      if (allHits_ == enabled) return;
      beginResetModel();
      allHits_ = enabled;
      rebuildRows();
      endResetModel();
    }

    void setRtInMinutes(bool minutes)
    {
      if (rtInMinutes_ == minutes) return;
      rtInMinutes_ = minutes;
      emit headerDataChanged(Qt::Horizontal, Rt, Rt);
      if (rowCount() > 0)
        emit dataChanged(index(0, Rt), index(rowCount() - 1, Rt), {Qt::DisplayRole});
    }

    [[nodiscard]] const SpectrumRecord* spectrumForRow(int row) const noexcept
    {
      if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return nullptr;
      const std::size_t index = rows_[static_cast<std::size_t>(row)].spectrumIndex;
      return index < spectra_.size() ? &spectra_[index] : nullptr;
    }

    [[nodiscard]] const IdentificationRecord* identificationForRow(int row) const noexcept
    {
      if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return nullptr;
      const auto& index = rows_[static_cast<std::size_t>(row)].identificationIndex;
      if (!index || *index >= identifications_.size()) return nullptr;
      return &identifications_[*index];
    }

    [[nodiscard]] const PeptideHitRecord* hitForRow(int row) const noexcept
    {
      if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return nullptr;
      const IdentificationRecord* identification = identificationForRow(row);
      const auto& hitIndex = rows_[static_cast<std::size_t>(row)].hitIndex;
      if (!identification || !hitIndex || *hitIndex >= identification->hits.size()) return nullptr;
      return &identification->hits[*hitIndex];
    }

    [[nodiscard]] std::optional<std::size_t> hitIndexForRow(int row) const noexcept
    {
      if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return std::nullopt;
      return rows_[static_cast<std::size_t>(row)].hitIndex;
    }

    [[nodiscard]] std::optional<std::size_t> identificationIndexForRow(int row) const noexcept
    {
      if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return std::nullopt;
      return rows_[static_cast<std::size_t>(row)].identificationIndex;
    }

    [[nodiscard]] int rowForSpectrum(std::size_t spectrumIndex) const noexcept
    {
      const auto found = std::find_if(rows_.begin(), rows_.end(), [spectrumIndex](const Row& row)
      {
        return row.spectrumIndex == spectrumIndex;
      });
      return found == rows_.end() ? -1 : static_cast<int>(std::distance(rows_.begin(), found));
    }

    [[nodiscard]] int rowForSpectrumSelection(
      std::size_t spectrumIndex, std::optional<std::size_t> identificationIndex,
      std::optional<std::size_t> hitIndex) const noexcept
    {
      const auto found = std::find_if(rows_.begin(), rows_.end(),
        [spectrumIndex, identificationIndex, hitIndex](const Row& row)
        {
          return row.spectrumIndex == spectrumIndex
            && (!identificationIndex || row.identificationIndex == identificationIndex)
            && (!hitIndex || row.hitIndex == hitIndex);
        });
      return found == rows_.end() ? rowForSpectrum(spectrumIndex)
                                 : static_cast<int>(std::distance(rows_.begin(), found));
    }

    [[nodiscard]] bool hasCompensationVoltage() const noexcept
    {
      return std::any_of(spectra_.begin(), spectra_.end(), [](const SpectrumRecord& spectrum)
      {
        return spectrum.compensationVoltage.has_value();
      });
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
      if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
      switch (section)
      {
        case Index: return QStringLiteral("#");
        case Rt: return RtUnit::columnHeader(rtInMinutes_);
        case MsLevel: return QStringLiteral("MS");
        case CompensationVoltage: return QStringLiteral("CV");
        case PeakCount: return QStringLiteral("Peaks");
        case Tic: return QStringLiteral("TIC");
        case BasePeak: return QStringLiteral("BPI");
        case MzRange: return QStringLiteral("m/z range");
        case PrecursorMz: return QStringLiteral("Prec m/z");
        case Charge: return QStringLiteral("Z");
        case Rank: return QStringLiteral("Rank");
        case Sequence: return QStringLiteral("Sequence");
        case Score: return QStringLiteral("Score");
        case Metadata: return QStringLiteral("Metadata");
        default: return {};
      }
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
      const SpectrumRecord* spectrum = spectrumForRow(index.row());
      const PeptideHitRecord* hit = hitForRow(index.row());
      const IdentificationRecord* identification = identificationForRow(index.row());
      if (!spectrum || index.column() < 0 || index.column() >= ColumnCount) return {};

      if (role == Qt::TextAlignmentRole)
      {
        return index.column() == Sequence || index.column() == Metadata || index.column() == MzRange
          ? QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter)
          : QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
      }
      if (role == Qt::UserRole)
      {
        switch (index.column())
        {
          case Index: return static_cast<qulonglong>(spectrum->index);
          case Rt: return spectrum->rt;
          case MsLevel: return spectrum->msLevel;
          case CompensationVoltage: return spectrum->compensationVoltage.value_or(0.0);
          case PeakCount: return static_cast<qulonglong>(spectrum->peakCount);
          case Tic: return spectrum->tic;
          case BasePeak: return spectrum->basePeakIntensity;
          case MzRange: return spectrum->mzMin;
          case PrecursorMz: return spectrum->precursorMz.value_or(0.0);
          case Charge: return spectrum->precursorCharge;
          case Rank: return static_cast<qulonglong>(hitIndexForRow(index.row()).value_or(0) + 1);
          case Sequence: return hit ? hit->sequence : QString();
          case Score: return hit ? hit->score : 0.0;
          case Metadata: return formatMetadata(identification, hit);
          default: return {};
        }
      }
      if (role != Qt::DisplayRole) return {};
      switch (index.column())
      {
        case Index: return static_cast<qulonglong>(spectrum->index);
        case Rt: return RtUnit::format(spectrum->rt, rtInMinutes_);
        case MsLevel: return spectrum->msLevel;
        case CompensationVoltage:
          return spectrum->compensationVoltage
            ? QString::number(*spectrum->compensationVoltage, 'f', 1) : QStringLiteral("-");
        case PeakCount: return static_cast<qulonglong>(spectrum->peakCount);
        case Tic: return QString::number(spectrum->tic, 'e', 2);
        case BasePeak: return QString::number(spectrum->basePeakIntensity, 'e', 2);
        case MzRange:
          return spectrum->peakCount > 0
            ? QStringLiteral("%1–%2").arg(spectrum->mzMin, 0, 'f', 1).arg(spectrum->mzMax, 0, 'f', 1)
            : QStringLiteral("-");
        case PrecursorMz:
          return spectrum->precursorMz ? QString::number(*spectrum->precursorMz, 'f', 4)
                                       : QStringLiteral("-");
        case Charge: return spectrum->precursorCharge > 0 ? QString::number(spectrum->precursorCharge)
                                                          : QStringLiteral("-");
        case Rank: return hit ? QString::number(hitIndexForRow(index.row()).value_or(0) + 1)
                              : QStringLiteral("-");
        case Sequence: return hit ? hit->sequence : QStringLiteral("-");
        case Score: return hit ? QString::number(hit->score, 'g', 7) : QStringLiteral("-");
        case Metadata: return formatMetadata(identification, hit);
        default: return {};
      }
    }

  private:
    void rebuildRows()
    {
      rows_.clear();
      std::vector<std::vector<std::size_t>> linkedIdentifications(spectra_.size());
      for (std::size_t identificationIndex = 0;
           identificationIndex < identifications_.size(); ++identificationIndex)
      {
        const auto spectrumIndex = identifications_[identificationIndex].spectrumIndex;
        if (spectrumIndex && *spectrumIndex < linkedIdentifications.size())
          linkedIdentifications[*spectrumIndex].push_back(identificationIndex);
      }
      for (const SpectrumRecord& spectrum : spectra_)
      {
        if (allHits_ && spectrum.index < linkedIdentifications.size()
            && !linkedIdentifications[spectrum.index].empty())
        {
          for (const std::size_t identificationIndex : linkedIdentifications[spectrum.index])
          {
            const auto& identification = identifications_[identificationIndex];
            if (identification.hits.empty())
            {
              rows_.push_back({spectrum.index, identificationIndex, std::nullopt});
              continue;
            }
            for (std::size_t hit = 0; hit < identification.hits.size(); ++hit)
              rows_.push_back({spectrum.index, identificationIndex, hit});
          }
        }
        else
        {
          const auto identificationIndex = spectrum.identificationIndex
            && *spectrum.identificationIndex < identifications_.size()
              ? spectrum.identificationIndex : std::nullopt;
          const auto hitIndex = identificationIndex
            && !identifications_[*identificationIndex].hits.empty()
              ? std::optional<std::size_t>{0} : std::nullopt;
          rows_.push_back({spectrum.index, identificationIndex, hitIndex});
        }
      }
    }

    std::vector<SpectrumRecord> spectra_;
    std::vector<IdentificationRecord> identifications_;
    std::vector<Row> rows_;
    bool allHits_{false};
    bool rtInMinutes_{false};
  };

  class SpectrumFilterProxyModel final : public QSortFilterProxyModel
  {
  public:
    explicit SpectrumFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent)
    {
      setSortRole(Qt::UserRole);
      setDynamicSortFilter(true);
    }

    void setMode(int mode) { mode_ = mode; invalidateRowsFilter(); }
    void setMinimumRt(std::optional<double> value) { minimumRt_ = value; invalidateRowsFilter(); }
    void setMaximumRt(std::optional<double> value) { maximumRt_ = value; invalidateRowsFilter(); }
    void setSequence(const QString& value) { sequence_ = value; invalidateRowsFilter(); }
    void setScoreThreshold(std::optional<double> value) { scoreThreshold_ = value; invalidateRowsFilter(); }

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex&) const override
    {
      const auto* model = static_cast<const SpectrumTableModel*>(sourceModel());
      const SpectrumRecord* spectrum = model ? model->spectrumForRow(sourceRow) : nullptr;
      const IdentificationRecord* identification = model ? model->identificationForRow(sourceRow) : nullptr;
      const PeptideHitRecord* hit = model ? model->hitForRow(sourceRow) : nullptr;
      if (!spectrum) return false;
      if (mode_ == 1 && spectrum->msLevel != 2) return false;
      if (mode_ == 2 && !spectrum->identificationIndex) return false;
      if (minimumRt_ && spectrum->rt < *minimumRt_) return false;
      if (maximumRt_ && spectrum->rt > *maximumRt_) return false;
      if (!sequence_.isEmpty() && (!hit || !hit->sequence.contains(sequence_, Qt::CaseInsensitive)))
        return false;
      if (scoreThreshold_)
      {
        if (!hit || !identification) return false;
        const bool keep = identification->higherScoreBetter ? hit->score >= *scoreThreshold_
                                                            : hit->score <= *scoreThreshold_;
        if (!keep) return false;
      }
      return true;
    }

  private:
    int mode_{0};
    std::optional<double> minimumRt_;
    std::optional<double> maximumRt_;
    QString sequence_;
    std::optional<double> scoreThreshold_;
  };

  SpectrumTableWidget::SpectrumTableWidget(QWidget* parent) : QWidget(parent)
  {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* first = new QHBoxLayout;
    mode_ = new QComboBox(this);
    mode_->setObjectName(QStringLiteral("spectrumModeFilter"));
    mode_->addItems({tr("All spectra"), tr("MS2"), tr("Identified")});
    first->addWidget(mode_);
    sequence_ = new QLineEdit(this);
    sequence_->setObjectName(QStringLiteral("spectrumSequenceFilter"));
    sequence_->setPlaceholderText(tr("Sequence contains…"));
    sequence_->setClearButtonEnabled(true);
    sequence_->setMaximumWidth(220);
    first->addWidget(sequence_, 1);

    auto* filters = new QToolButton(this);
    filters->setObjectName(QStringLiteral("spectrumFilterOptions"));
    filters->setText(tr("Filters"));
    filters->setPopupMode(QToolButton::InstantPopup);
    filters->setAccessibleName(tr("Spectrum table filters"));
    auto* filterMenu = new QMenu(filters);
    filterMenu->setObjectName(QStringLiteral("spectrumFilterMenu"));
    rtFilterLabel_ = new QLabel(RtUnit::columnHeader(rtInMinutes_), this);
    minimumRt_ = new QLineEdit(this);
    minimumRt_->setObjectName(QStringLiteral("spectrumMinimumRt"));
    minimumRt_->setPlaceholderText(tr("minimum"));
    minimumRt_->setMaximumWidth(85);
    auto* rtValidator = new QDoubleValidator(0.0, 1.0e300, 12, minimumRt_);
    rtValidator->setNotation(QDoubleValidator::ScientificNotation);
    minimumRt_->setValidator(rtValidator);
    maximumRt_ = new QLineEdit(this);
    maximumRt_->setObjectName(QStringLiteral("spectrumMaximumRt"));
    maximumRt_->setPlaceholderText(tr("maximum"));
    maximumRt_->setMaximumWidth(85);
    auto* maximumRtValidator = new QDoubleValidator(0.0, 1.0e300, 12, maximumRt_);
    maximumRtValidator->setNotation(QDoubleValidator::ScientificNotation);
    maximumRt_->setValidator(maximumRtValidator);
    auto* rtRange = new QWidget(filterMenu);
    auto* rtRangeLayout = new QHBoxLayout(rtRange);
    rtRangeLayout->setContentsMargins(0, 0, 0, 0);
    rtRangeLayout->setSpacing(6);
    rtRangeLayout->addWidget(minimumRt_);
    rtRangeLayout->addWidget(new QLabel(QStringLiteral("–"), rtRange));
    rtRangeLayout->addWidget(maximumRt_);
    CompactControls::addLabeledMenuControl(filterMenu, rtFilterLabel_, rtRange, 110, 190);
    scoreThresholdLabel_ = new QLabel(tr("Score threshold"), this);
    scoreThresholdLabel_->setObjectName(QStringLiteral("spectrumScoreThresholdLabel"));
    minimumScore_ = new QLineEdit(this);
    minimumScore_->setObjectName(QStringLiteral("spectrumMinimumScore"));
    minimumScore_->setPlaceholderText(tr("threshold"));
    minimumScore_->setMaximumWidth(105);
    auto* scoreValidator = new QDoubleValidator(-1.0e300, 1.0e300, 12, minimumScore_);
    scoreValidator->setNotation(QDoubleValidator::ScientificNotation);
    minimumScore_->setValidator(scoreValidator);
    CompactControls::addLabeledMenuControl(
      filterMenu, scoreThresholdLabel_, minimumScore_, 150, 110);
    allHits_ = new QCheckBox(tr("All peptide hits"), this);
    allHits_->setObjectName(QStringLiteral("spectrumAllHits"));
    CompactControls::addMenuControl(filterMenu, allHits_);
    filterMenu->addSeparator();
    auto* reset = filterMenu->addAction(
      QIcon(QStringLiteral(":/icons/material-clear-all.svg")), tr("Reset filters"));
    reset->setObjectName(QStringLiteral("spectrumResetFilters"));
    reset->setToolTip(tr("Reset all spectrum table filters"));
    filters->setMenu(filterMenu);
    first->addWidget(filters);

    auto* display = new QToolButton(this);
    display->setObjectName(QStringLiteral("spectrumColumnOptions"));
    display->setText(tr("Columns"));
    display->setPopupMode(QToolButton::InstantPopup);
    display->setAccessibleName(tr("Spectrum table columns"));
    auto* displayMenu = new QMenu(display);
    displayMenu->setObjectName(QStringLiteral("spectrumColumnMenu"));
    advanced_ = new QCheckBox(tr("Advanced statistics"), this);
    advanced_->setObjectName(QStringLiteral("spectrumAdvancedColumns"));
    CompactControls::addMenuControl(displayMenu, advanced_);
    metadata_ = new QCheckBox(tr("Metadata"), this);
    metadata_->setObjectName(QStringLiteral("spectrumMetadataColumn"));
    CompactControls::addMenuControl(displayMenu, metadata_);
    display->setMenu(displayMenu);
    first->addWidget(display);
    first->addStretch();
    countLabel_ = new QLabel(this);
    countLabel_->setObjectName(QStringLiteral("spectrumCountLabel"));
    first->addWidget(countLabel_);
    auto* exportButton = CompactControls::makeIconButton(
      this, QIcon(QStringLiteral(":/icons/material-file-download.svg")),
      tr("Export filtered spectra as TSV"), QStringLiteral("spectrumExportTsv"));
    first->addWidget(exportButton);
    layout->addLayout(first);

    selectionNotice_ = new QWidget(this);
    selectionNotice_->setObjectName(QStringLiteral("spectrumHiddenSelectionNotice"));
    auto* selectionNoticeLayout = new QHBoxLayout(selectionNotice_);
    selectionNoticeLayout->setContentsMargins(6, 2, 2, 2);
    selectionNoticeLayout->setSpacing(6);
    selectionNoticeLabel_ = new QLabel(selectionNotice_);
    selectionNoticeLabel_->setObjectName(QStringLiteral("spectrumHiddenSelectionLabel"));
    selectionNoticeLabel_->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    selectionNoticeLayout->addWidget(selectionNoticeLabel_, 1);
    auto* resetHiddenSelectionFilters = new QToolButton(selectionNotice_);
    resetHiddenSelectionFilters->setObjectName(
      QStringLiteral("spectrumResetHiddenSelectionFilters"));
    resetHiddenSelectionFilters->setIcon(
      QIcon(QStringLiteral(":/icons/material-clear-all.svg")));
    resetHiddenSelectionFilters->setText(tr("Reset filters"));
    resetHiddenSelectionFilters->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    resetHiddenSelectionFilters->setAutoRaise(true);
    resetHiddenSelectionFilters->setToolTip(tr("Show the selected spectrum in the table"));
    selectionNoticeLayout->addWidget(resetHiddenSelectionFilters);
    selectionNotice_->setVisible(false);
    layout->addWidget(selectionNotice_);

    model_ = new SpectrumTableModel(this);
    proxy_ = new SpectrumFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("spectraTable"));
    table_->setAccessibleName(tr("Filtered spectrum table"));
    table_->setToolTip(tr("Click a row to open that spectrum"));
    table_->setModel(proxy_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSortingEnabled(true);
    table_->sortByColumn(SpectrumTableModel::Index, Qt::AscendingOrder);
    table_->verticalHeader()->setVisible(false);
    // Interactive (not ResizeToContents) so the header is not re-measured on every
    // filter keystroke; widths are fitted once per data load in setData().
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_);

    connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this, &SpectrumTableWidget::updateFilters);
    connect(minimumRt_, &QLineEdit::textChanged, this, &SpectrumTableWidget::updateFilters);
    connect(maximumRt_, &QLineEdit::textChanged, this, &SpectrumTableWidget::updateFilters);
    connect(sequence_, &QLineEdit::textChanged, this, &SpectrumTableWidget::updateFilters);
    connect(minimumScore_, &QLineEdit::textChanged, this, &SpectrumTableWidget::updateFilters);
    connect(advanced_, &QCheckBox::toggled, this, &SpectrumTableWidget::updateColumns);
    connect(metadata_, &QCheckBox::toggled, this, &SpectrumTableWidget::updateColumns);
    connect(allHits_, &QCheckBox::toggled, this, [this](bool enabled)
    {
      model_->setAllHits(enabled);
      updateColumns();
      updateCountLabel();
      synchronizeSelectedSpectrum();
    });
    connect(reset, &QAction::triggered, this, &SpectrumTableWidget::resetFilters);
    connect(resetHiddenSelectionFilters, &QToolButton::clicked,
            this, &SpectrumTableWidget::resetFilters);
    connect(exportButton, &QToolButton::clicked, this, &SpectrumTableWidget::exportTsv);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& index)
    {
      if (!index.isValid() || synchronizingSelection_) return;
      const QModelIndex source = proxy_->mapToSource(index);
      if (const SpectrumRecord* spectrum = model_->spectrumForRow(source.row()))
      {
        const auto identification = model_->identificationIndexForRow(source.row());
        const auto hit = model_->hitIndexForRow(source.row());
        emit spectrumActivated(spectrum->index,
                               identification ? static_cast<int>(*identification) : -1,
                               hit ? static_cast<int>(*hit) : -1);
      }
    });
    connect(proxy_, &QAbstractItemModel::modelReset, this, &SpectrumTableWidget::updateCountLabel);
    connect(proxy_, &QAbstractItemModel::rowsInserted, this, &SpectrumTableWidget::updateCountLabel);
    connect(proxy_, &QAbstractItemModel::rowsRemoved, this, &SpectrumTableWidget::updateCountLabel);
    updateColumns();
    updateCountLabel();
  }

  void SpectrumTableWidget::setData(const std::vector<SpectrumRecord>& spectra,
                                    const std::vector<IdentificationRecord>& identifications)
  {
    model_->setData(spectra, identifications);
    bool haveScores = false;
    bool mixedDirection = false;
    bool higherIsBetter = true;
    QString scoreType;
    bool mixedTypes = false;
    for (const IdentificationRecord& identification : identifications)
    {
      if (identification.hits.empty()) continue;
      if (!haveScores)
      {
        haveScores = true;
        higherIsBetter = identification.higherScoreBetter;
        scoreType = identification.scoreType;
      }
      else
      {
        mixedDirection |= higherIsBetter != identification.higherScoreBetter;
        mixedTypes |= scoreType != identification.scoreType;
      }
    }
    const QString scoreName = !mixedTypes && !scoreType.isEmpty() ? scoreType : tr("Score");
    const bool scoreFilterable = haveScores && !mixedTypes && !mixedDirection;
    scoreThresholdLabel_->setText(mixedTypes || mixedDirection
      ? tr("Mixed score semantics") : !haveScores ? tr("Score threshold")
      : tr("%1 %2").arg(scoreName, higherIsBetter ? QStringLiteral("≥") : QStringLiteral("≤")));
    minimumScore_->setEnabled(scoreFilterable);
    if (!scoreFilterable) minimumScore_->clear();
    minimumScore_->setToolTip(!haveScores
      ? tr("Load identifications to filter by score")
      : !scoreFilterable
      ? tr("Score filtering is disabled because the loaded score types are not comparable")
      : higherIsBetter ? tr("Keep hits with a score greater than or equal to this value")
                       : tr("Keep hits with a score less than or equal to this value"));
    updateFilters();
    updateColumns();
    table_->resizeColumnsToContents();
    updateCountLabel();
  }

  void SpectrumTableWidget::clear()
  {
    selectedSpectrumIndex_.reset();
    selectedIdentificationIndex_.reset();
    selectedHitIndex_.reset();
    selectionNotice_->setVisible(false);
    setData({}, {});
  }

  void SpectrumTableWidget::selectSpectrum(
    std::size_t spectrumIndex, std::optional<std::size_t> identificationIndex,
    std::optional<std::size_t> hitIndex)
  {
    selectedSpectrumIndex_ = spectrumIndex;
    selectedIdentificationIndex_ = identificationIndex;
    selectedHitIndex_ = hitIndex;
    synchronizeSelectedSpectrum();
  }

  void SpectrumTableWidget::synchronizeSelectedSpectrum()
  {
    if (!selectedSpectrumIndex_)
    {
      selectionNotice_->setVisible(false);
      return;
    }
    const int row = model_->rowForSpectrumSelection(
      *selectedSpectrumIndex_, selectedIdentificationIndex_, selectedHitIndex_);
    QScopedValueRollback guard(synchronizingSelection_, true);
    if (row < 0)
    {
      table_->selectionModel()->clear();
      selectionNotice_->setVisible(false);
      return;
    }
    const QModelIndex proxyIndex = proxy_->mapFromSource(model_->index(row, 0));
    if (!proxyIndex.isValid())
    {
      table_->selectionModel()->clear();
      selectionNoticeLabel_->setText(
        tr("Spectrum #%1 is hidden by the current filters.")
          .arg(*selectedSpectrumIndex_ + 1));
      selectionNotice_->setVisible(true);
      return;
    }
    selectionNotice_->setVisible(false);
    table_->selectionModel()->setCurrentIndex(
      proxyIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    table_->scrollTo(proxyIndex, QAbstractItemView::PositionAtCenter);
  }

  int SpectrumTableWidget::filteredRowCount() const noexcept { return proxy_->rowCount(); }

  void SpectrumTableWidget::setRtInMinutes(bool minutes)
  {
    rtInMinutes_ = minutes;
    model_->setRtInMinutes(minutes);
    if (rtFilterLabel_) rtFilterLabel_->setText(RtUnit::columnHeader(minutes));
    // Re-run so the RT boxes are re-interpreted in the newly active unit.
    updateFilters();
  }

  void SpectrumTableWidget::updateFilters()
  {
    auto optionalNumber = [](QLineEdit* edit, const QString& normalTip) -> std::optional<double>
    {
      const QString text = edit->text().trimmed();
      if (text.isEmpty())
      {
        setNumericValidity(edit, true, normalTip);
        return std::nullopt;
      }
      bool valid = false;
      const double value = text.toDouble(&valid);
      valid = valid && edit->hasAcceptableInput();
      setNumericValidity(edit, valid, normalTip, QObject::tr("Enter a valid number"));
      return valid ? std::optional<double>{value} : std::nullopt;
    };
    // RT boxes are entered in the displayed unit; the proxy filters raw seconds.
    const double rtScale = RtUnit::scale(rtInMinutes_);
    auto toSeconds = [rtScale](std::optional<double> value) -> std::optional<double>
    {
      return value ? std::optional<double>{*value * rtScale} : std::nullopt;
    };
    auto* filter = static_cast<SpectrumFilterProxyModel*>(proxy_);
    const QString rtTip = tr("Enter retention time in %1").arg(RtUnit::unit(rtInMinutes_));
    std::optional<double> minimumRt = optionalNumber(minimumRt_, rtTip);
    std::optional<double> maximumRt = optionalNumber(maximumRt_, rtTip);
    if (minimumRt && maximumRt && *minimumRt > *maximumRt)
    {
      const QString error = tr("Minimum RT must not exceed maximum RT");
      setNumericValidity(minimumRt_, false, rtTip, error);
      setNumericValidity(maximumRt_, false, rtTip, error);
      minimumRt.reset();
      maximumRt.reset();
    }
    const QString scoreTip = scoreThresholdLabel_->text().contains(QChar(0x2264))
      ? tr("Keep hits at or below this score") : scoreThresholdLabel_->text().contains(QChar(0x2265))
      ? tr("Keep hits at or above this score") : tr("Filter using each score type's confidence direction");
    filter->setMode(mode_->currentIndex());
    filter->setMinimumRt(toSeconds(minimumRt));
    filter->setMaximumRt(toSeconds(maximumRt));
    filter->setSequence(sequence_->text().trimmed());
    filter->setScoreThreshold(minimumScore_->isEnabled()
      ? optionalNumber(minimumScore_, scoreTip) : std::nullopt);
    updateCountLabel();
    synchronizeSelectedSpectrum();
  }

  void SpectrumTableWidget::updateColumns()
  {
    table_->setColumnHidden(SpectrumTableModel::CompensationVoltage,
                            !model_->hasCompensationVoltage());
    for (const int column : {SpectrumTableModel::PeakCount, SpectrumTableModel::Tic,
                             SpectrumTableModel::BasePeak, SpectrumTableModel::MzRange})
      table_->setColumnHidden(column, !advanced_->isChecked());
    table_->setColumnHidden(SpectrumTableModel::Rank, !allHits_->isChecked());
    table_->setColumnHidden(SpectrumTableModel::Metadata, !metadata_->isChecked());
  }

  void SpectrumTableWidget::resetFilters()
  {
    mode_->setCurrentIndex(0);
    minimumRt_->clear();
    maximumRt_->clear();
    sequence_->clear();
    minimumScore_->clear();
    allHits_->setChecked(false);
    updateFilters();
  }

  void SpectrumTableWidget::exportTsv()
  {
    const QString path = QFileDialog::getSaveFileName(
      this, tr("Export filtered spectra"), QStringLiteral("spectra.tsv"),
      tr("Tab-separated values (*.tsv);;All files (*)"));
    if (path.isEmpty()) return;
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream stream(&output);
    bool first = true;
    for (int column = 0; column < model_->columnCount(); ++column)
    {
      if (table_->isColumnHidden(column)) continue;
      if (!first) stream << '\t';
      // Export stays canonical seconds regardless of the on-screen RT unit toggle.
      stream << (column == SpectrumTableModel::Rt
                   ? RtUnit::columnHeader(false)
                   : model_->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
      first = false;
    }
    stream << '\n';
    for (int row = 0; row < proxy_->rowCount(); ++row)
    {
      first = true;
      for (int column = 0; column < model_->columnCount(); ++column)
      {
        if (table_->isColumnHidden(column)) continue;
        if (!first) stream << '\t';
        // RT is exported in canonical seconds; UserRole carries the raw seconds value.
        QString value = column == SpectrumTableModel::Rt
          ? RtUnit::format(proxy_->data(proxy_->index(row, column), Qt::UserRole).toDouble(), false)
          : proxy_->data(proxy_->index(row, column), Qt::DisplayRole).toString();
        value.replace('\t', ' ');
        value.replace('\n', ' ');
        stream << value;
        first = false;
      }
      stream << '\n';
    }
    output.commit();
  }

  void SpectrumTableWidget::updateCountLabel()
  {
    countLabel_->setText(tr("%1 of %2 rows").arg(proxy_->rowCount()).arg(model_->rowCount()));
  }
}
