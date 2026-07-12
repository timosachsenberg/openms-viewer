#include "widgets/OswPanel.h"

#include "widgets/TransitionGroupPlot.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QScopedValueRollback>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <limits>

namespace OpenMSViewer
{
  // --- Models (no Q_OBJECT needed: no custom signals/slots) ---------------------

  class OswPrecursorModel final : public QAbstractTableModel
  {
  public:
    enum Column { Sequence, Charge, PrecursorMz, PeakGroups, BestQValue, Decoy, ColumnCount };
    using QAbstractTableModel::QAbstractTableModel;

    void setRows(std::vector<OswPrecursor> rows)
    {
      beginResetModel();
      rows_ = std::move(rows);
      endResetModel();
    }
    [[nodiscard]] const OswPrecursor& row(int index) const { return rows_[static_cast<std::size_t>(index)]; }

    int rowCount(const QModelIndex& parent = {}) const override
    { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    int columnCount(const QModelIndex& = {}) const override { return ColumnCount; }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
      if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
      switch (section)
      {
        case Sequence:    return QStringLiteral("Peptide");
        case Charge:      return QStringLiteral("z");
        case PrecursorMz: return QStringLiteral("Precursor m/z");
        case PeakGroups:  return QStringLiteral("#PG");
        case BestQValue:  return QStringLiteral("best q");
        case Decoy:       return QStringLiteral("Decoy");
      }
      return {};
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
      if (!index.isValid()) return {};
      const OswPrecursor& precursor = rows_[static_cast<std::size_t>(index.row())];
      const bool sortRole = role == Qt::UserRole;
      if (role != Qt::DisplayRole && !sortRole) return {};
      switch (index.column())
      {
        case Sequence:    return precursor.modifiedSequence;
        case Charge:      return precursor.charge;
        case PrecursorMz: return sortRole ? QVariant(precursor.precursorMz)
                                          : QVariant(QString::number(precursor.precursorMz, 'f', 4));
        case PeakGroups:  return precursor.peakGroupCount;
        case BestQValue:
          if (!precursor.bestQValue)
            return sortRole ? QVariant(std::numeric_limits<double>::max()) : QVariant(QStringLiteral("–"));
          return sortRole ? QVariant(*precursor.bestQValue)
                          : QVariant(QString::number(*precursor.bestQValue, 'g', 3));
        case Decoy:       return sortRole ? QVariant(precursor.decoy ? 1 : 0)
                                          : QVariant(precursor.decoy ? QStringLiteral("decoy") : QString());
      }
      return {};
    }

  private:
    std::vector<OswPrecursor> rows_;
  };

  class OswPeakGroupModel final : public QAbstractTableModel
  {
  public:
    enum Column { Rank, ApexRt, Left, Right, Area, Score, QValue, Pep, ColumnCount };
    using QAbstractTableModel::QAbstractTableModel;

    void setRows(std::vector<OswPeakGroup> rows)
    {
      beginResetModel();
      rows_ = std::move(rows);
      endResetModel();
    }

    int rowCount(const QModelIndex& parent = {}) const override
    { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    int columnCount(const QModelIndex& = {}) const override { return ColumnCount; }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
      if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
      switch (section)
      {
        case Rank:   return QStringLiteral("Rank");
        case ApexRt: return QStringLiteral("Apex RT");
        case Left:   return QStringLiteral("Left");
        case Right:  return QStringLiteral("Right");
        case Area:   return QStringLiteral("Area");
        case Score:  return QStringLiteral("Score");
        case QValue: return QStringLiteral("q-value");
        case Pep:    return QStringLiteral("PEP");
      }
      return {};
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
      if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::UserRole)) return {};
      const OswPeakGroup& group = rows_[static_cast<std::size_t>(index.row())];
      const bool sortRole = role == Qt::UserRole;
      const auto optional = [&](const std::optional<double>& value, char format, int precision) -> QVariant
      {
        if (!value) return sortRole ? QVariant(std::numeric_limits<double>::max()) : QVariant(QStringLiteral("–"));
        return sortRole ? QVariant(*value) : QVariant(QString::number(*value, format, precision));
      };
      switch (index.column())
      {
        case Rank:   return group.rank ? QVariant(*group.rank) : (sortRole ? QVariant(1 << 30) : QVariant(QStringLiteral("–")));
        case ApexRt: return sortRole ? QVariant(group.apexRt) : QVariant(QString::number(group.apexRt, 'f', 1));
        case Left:   return sortRole ? QVariant(group.leftWidth) : QVariant(QString::number(group.leftWidth, 'f', 1));
        case Right:  return sortRole ? QVariant(group.rightWidth) : QVariant(QString::number(group.rightWidth, 'f', 1));
        case Area:   return optional(group.areaIntensity, 'g', 4);
        case Score:  return optional(group.score, 'g', 4);
        case QValue: return optional(group.qValue, 'g', 3);
        case Pep:    return optional(group.pep, 'g', 3);
      }
      return {};
    }

  private:
    std::vector<OswPeakGroup> rows_;
  };

  class OswPrecursorFilterProxy final : public QSortFilterProxyModel
  {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setSearch(const QString& text) { search_ = text.trimmed(); invalidateFilter(); }
    void setMaxQValue(double value) { maxQValue_ = value; invalidateFilter(); }
    void setHideDecoys(bool hide) { hideDecoys_ = hide; invalidateFilter(); }

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
      // The source model is always our OswPrecursorModel (set in the panel ctor).
      const auto* model = static_cast<const OswPrecursorModel*>(sourceModel());
      if (!model) return true;
      const OswPrecursor& precursor = model->row(sourceRow);
      Q_UNUSED(sourceParent);
      if (hideDecoys_ && precursor.decoy) return false;
      if (!search_.isEmpty()
          && !precursor.modifiedSequence.contains(search_, Qt::CaseInsensitive))
        return false;
      // A max-q filter only excludes scored precursors above the threshold;
      // unscored ones are kept (they simply have no q-value).
      if (maxQValue_ < 1.0 && precursor.bestQValue && *precursor.bestQValue > maxQValue_)
        return false;
      return true;
    }

  private:
    QString search_;
    double maxQValue_{1.0};
    bool hideDecoys_{false};
  };

  // --- Panel --------------------------------------------------------------------

  OswPanel::OswPanel(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("oswPanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* controls = new QHBoxLayout;
    runSelector_ = new QComboBox(this);
    runSelector_->setObjectName(QStringLiteral("oswRunSelector"));
    runSelector_->setToolTip(tr("Run"));
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("oswSearch"));
    search_->setPlaceholderText(tr("Search peptide…"));
    search_->setClearButtonEnabled(true);
    maxQValue_ = new QDoubleSpinBox(this);
    maxQValue_->setObjectName(QStringLiteral("oswMaxQValue"));
    maxQValue_->setRange(0.0, 1.0);
    maxQValue_->setDecimals(3);
    maxQValue_->setSingleStep(0.01);
    maxQValue_->setValue(1.0);
    maxQValue_->setPrefix(tr("q ≤ "));
    hideDecoys_ = new QCheckBox(tr("Hide decoys"), this);
    hideDecoys_->setObjectName(QStringLiteral("oswHideDecoys"));
    countLabel_ = new QLabel(this);
    controls->addWidget(runSelector_);
    controls->addWidget(search_, 1);
    controls->addWidget(maxQValue_);
    controls->addWidget(hideDecoys_);
    controls->addWidget(countLabel_);
    layout->addLayout(controls);

    auto* splitter = new QSplitter(Qt::Vertical, this);

    precursorModel_ = new OswPrecursorModel(this);
    precursorProxy_ = new OswPrecursorFilterProxy(this);
    precursorProxy_->setSourceModel(precursorModel_);
    precursorProxy_->setSortRole(Qt::UserRole);
    precursorView_ = new QTableView(this);
    precursorView_->setObjectName(QStringLiteral("oswPrecursorTable"));
    precursorView_->setModel(precursorProxy_);
    precursorView_->setSortingEnabled(true);
    precursorView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    precursorView_->setSelectionMode(QAbstractItemView::SingleSelection);
    precursorView_->horizontalHeader()->setStretchLastSection(true);
    precursorView_->verticalHeader()->setVisible(false);
    splitter->addWidget(precursorView_);

    peakGroupModel_ = new OswPeakGroupModel(this);
    peakGroupView_ = new QTableView(this);
    peakGroupView_->setObjectName(QStringLiteral("oswPeakGroupTable"));
    peakGroupView_->setModel(peakGroupModel_);
    peakGroupView_->setSortingEnabled(false);
    peakGroupView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    peakGroupView_->setSelectionMode(QAbstractItemView::SingleSelection);
    peakGroupView_->horizontalHeader()->setStretchLastSection(true);
    peakGroupView_->verticalHeader()->setVisible(false);
    splitter->addWidget(peakGroupView_);

    auto* plotArea = new QWidget(this);
    auto* plotLayout = new QVBoxLayout(plotArea);
    plotLayout->setContentsMargins(0, 0, 0, 0);
    auto* plotControls = new QHBoxLayout;
    showAllTransitions_ = new QCheckBox(tr("Show all transitions"), plotArea);
    showAllTransitions_->setObjectName(QStringLiteral("oswShowAllTransitions"));
    chromatogramNote_ = new QLabel(plotArea);
    chromatogramNote_->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    plotControls->addWidget(showAllTransitions_);
    plotControls->addStretch();
    plotControls->addWidget(chromatogramNote_);
    plotLayout->addLayout(plotControls);
    auto* plotSplitter = new QSplitter(Qt::Horizontal, plotArea);
    plot_ = new TransitionGroupPlot(plotArea);
    plotSplitter->addWidget(plot_);
    scoreTable_ = new QTableWidget(plotArea);
    scoreTable_->setObjectName(QStringLiteral("oswScoreTable"));
    scoreTable_->setColumnCount(2);
    scoreTable_->setHorizontalHeaderLabels({tr("Score"), tr("Value")});
    scoreTable_->horizontalHeader()->setStretchLastSection(true);
    scoreTable_->verticalHeader()->setVisible(false);
    scoreTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    scoreTable_->setMaximumWidth(240);
    plotSplitter->addWidget(scoreTable_);
    plotSplitter->setStretchFactor(0, 1);
    plotLayout->addWidget(plotSplitter, 1);
    splitter->addWidget(plotArea);
    splitter->setStretchFactor(2, 1);
    layout->addWidget(splitter, 1);

    fetchWatcher_ = new QFutureWatcher<std::vector<TransitionChromatogram>>(this);

    connect(precursorView_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current) { onPrecursorActivated(current); });
    connect(peakGroupView_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this] { onPeakGroupActivated(); });
    connect(runSelector_, &QComboBox::currentIndexChanged, this, [this]
            { if (!synchronizing_ && currentPrecursor_ >= 0) onPrecursorActivated(precursorView_->currentIndex()); });
    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString& text) { precursorProxy_->setSearch(text); reselectPrecursorAfterFilter(); });
    connect(maxQValue_, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { precursorProxy_->setMaxQValue(value); reselectPrecursorAfterFilter(); });
    connect(hideDecoys_, &QCheckBox::toggled, this,
            [this](bool hide) { precursorProxy_->setHideDecoys(hide); reselectPrecursorAfterFilter(); });
    connect(showAllTransitions_, &QCheckBox::toggled, this,
            [this](bool showAll) { if (plot_) plot_->setShowAllTransitions(showAll); });
    connect(fetchWatcher_, &QFutureWatcher<std::vector<TransitionChromatogram>>::finished,
            this, [this] { applyFetchedTransitions(); });
  }

  void OswPanel::setData(std::shared_ptr<OswStore> store,
                         std::shared_ptr<ChromatogramSource> chromatograms,
                         const QString& chromatogramNote)
  {
    ++documentGeneration_;  // any in-flight fetch from a previous document is now stale
    store_ = std::move(store);
    chromatograms_ = std::move(chromatograms);
    cache_.clear();
    currentPrecursor_ = -1;
    peakGroupModel_->setRows({});
    plot_->clear();
    scoreTable_->setRowCount(0);
    chromatogramNote_->setText(chromatograms_ ? QString() : chromatogramNote);

    {
      // Populate the run selector without its currentIndexChanged handler firing.
      QScopedValueRollback<bool> guard(synchronizing_, true);
      runSelector_->clear();
      runSelector_->addItem(tr("All runs"), QVariant::fromValue<qint64>(-1));
      if (store_)
        for (const OswRun& run : store_->runs())
          runSelector_->addItem(run.filename.isEmpty() ? tr("run %1").arg(run.id) : run.filename,
                                QVariant::fromValue<qint64>(run.id));
      runSelector_->setVisible(store_ && store_->runs().size() > 1);
    }

    reloadPrecursors();
  }

  void OswPanel::clear()
  {
    ++documentGeneration_;
    store_.reset();
    chromatograms_.reset();
    cache_.clear();
    precursors_.clear();
    currentPrecursor_ = -1;
    precursorModel_->setRows({});
    peakGroupModel_->setRows({});
    plot_->clear();
    scoreTable_->setRowCount(0);
    countLabel_->clear();
    chromatogramNote_->clear();
  }

  void OswPanel::reloadPrecursors()
  {
    precursors_ = store_ ? store_->precursors() : std::vector<OswPrecursor>{};
    precursorModel_->setRows(precursors_);
    precursorView_->resizeColumnsToContents();
    countLabel_->setText(tr("%1 precursors").arg(precursors_.size()));
    if (!precursors_.empty())
      precursorView_->selectRow(0);
  }

  std::int64_t OswPanel::currentRunId() const
  {
    return runSelector_->currentData().isValid()
             ? static_cast<std::int64_t>(runSelector_->currentData().toLongLong()) : -1;
  }

  void OswPanel::onPrecursorActivated(const QModelIndex& current)
  {
    if (synchronizing_ || !store_) return;
    if (!current.isValid()) { clearPrecursorDetails(); return; }  // selection filtered away
    const int sourceRow = precursorProxy_->mapToSource(current).row();
    if (sourceRow < 0 || sourceRow >= static_cast<int>(precursors_.size())) return;
    const OswPrecursor& precursor = precursorModel_->row(sourceRow);
    currentPrecursor_ = precursor.id;
    currentLibraryRt_ = precursor.libraryRt;

    // Peak groups for this precursor, restricted to the selected run.
    const std::int64_t runId = currentRunId();
    peakGroups_.clear();
    for (const OswPeakGroup& group : store_->peakGroups(precursor.id))
      if (runId < 0 || group.runId == runId) peakGroups_.push_back(group);
    peakGroupModel_->setRows(peakGroups_);
    peakGroupView_->resizeColumnsToContents();
    if (!peakGroups_.empty()) peakGroupView_->selectRow(0);

    requestTransitions(precursor.id, runId);
    updateScoreInspector();
  }

  int OswPanel::selectedPeakGroupRow() const
  {
    const QModelIndex index = peakGroupView_->currentIndex();
    return index.isValid() ? index.row() : (peakGroups_.empty() ? -1 : 0);
  }

  void OswPanel::onPeakGroupActivated()
  {
    refreshPlot();
    updateScoreInspector();
  }

  void OswPanel::requestTransitions(std::int64_t precursorId, std::int64_t runId)
  {
    currentTransitions_.clear();
    if (!chromatograms_)
    {
      refreshPlot();
      return;
    }
    const auto key = std::make_pair(precursorId, runId);
    if (const auto it = cache_.find(key); it != cache_.end())
    {
      currentTransitions_ = it->second;
      refreshPlot();
      return;
    }
    // Off-thread fetch; QFutureWatcher::setFuture drops any earlier in-flight
    // result, so only the latest selection is applied.
    pendingPrecursor_ = precursorId;
    pendingRun_ = runId;
    pendingGeneration_ = documentGeneration_;
    auto source = chromatograms_;
    fetchWatcher_->setFuture(QtConcurrent::run(
      [source, precursorId, runId] { return source->fetch(precursorId, runId); }));
  }

  void OswPanel::applyFetchedTransitions()
  {
    // Drop a result whose document was replaced while it was in flight (a stale
    // fetch must not populate a different document's cache or plot).
    if (pendingGeneration_ != documentGeneration_) return;
    auto transitions = fetchWatcher_->result();
    cache_[std::make_pair(pendingPrecursor_, pendingRun_)] = transitions;
    // Ignore a result that no longer matches the current selection.
    if (pendingPrecursor_ != currentPrecursor_ || pendingRun_ != currentRunId()) return;
    currentTransitions_ = std::move(transitions);
    refreshPlot();
  }

  void OswPanel::refreshPlot()
  {
    if (!plot_) return;
    plot_->setData(currentTransitions_, peakGroups_, selectedPeakGroupRow(), currentLibraryRt_);
  }

  void OswPanel::clearPrecursorDetails()
  {
    currentPrecursor_ = -1;
    peakGroups_.clear();
    peakGroupModel_->setRows({});
    currentTransitions_.clear();
    if (plot_) plot_->clear();
    if (scoreTable_) scoreTable_->setRowCount(0);
  }

  void OswPanel::reselectPrecursorAfterFilter()
  {
    // A filter can hide the selected precursor: reselect the first visible row so
    // the detail views stay in sync, or clear them when nothing matches.
    if (precursorProxy_->rowCount() == 0) { clearPrecursorDetails(); return; }
    if (!precursorView_->currentIndex().isValid()) precursorView_->selectRow(0);
  }

  void OswPanel::updateScoreInspector()
  {
    scoreTable_->setRowCount(0);
    if (!store_) return;
    const int row = selectedPeakGroupRow();
    if (row < 0 || row >= static_cast<int>(peakGroups_.size())) return;
    const auto subScores = store_->subScores(peakGroups_[static_cast<std::size_t>(row)].featureId);
    scoreTable_->setRowCount(static_cast<int>(subScores.size()));
    for (int index = 0; index < static_cast<int>(subScores.size()); ++index)
    {
      scoreTable_->setItem(index, 0, new QTableWidgetItem(subScores[static_cast<std::size_t>(index)].name));
      scoreTable_->setItem(index, 1, new QTableWidgetItem(
        QString::number(subScores[static_cast<std::size_t>(index)].value, 'g', 4)));
    }
  }
}
