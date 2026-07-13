#pragma once

#include "model/ChromatogramSource.h"
#include "model/OswStore.h"

#include <QFutureWatcher>
#include <QWidget>

#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QSortFilterProxyModel;
class QTableView;
class QTableWidget;

namespace OpenMSViewer
{
  class TransitionGroupPlot;
  class OswPrecursorModel;
  class OswPeakGroupModel;
  class OswPrecursorFilterProxy;

  // Interactive OpenSWATH results view: a searchable/filterable precursor table
  // drives a candidate peak-group table and a transition-group chromatogram plot
  // (peak-group boundaries shaded), with a per-peak-group score inspector.
  // Transition XICs are fetched off the GUI thread with stale-request coalescing
  // and a small cache; selection is local to this panel.
  class OswPanel final : public QWidget
  {
    Q_OBJECT

  public:
    explicit OswPanel(QWidget* parent = nullptr);

    void setData(std::shared_ptr<OswStore> store,
                 std::shared_ptr<ChromatogramSource> chromatograms,
                 const QString& chromatogramNote);
    void clear();
    void setRtInMinutes(bool minutes);

  private:
    void reloadPrecursors();
    void onPrecursorActivated(const QModelIndex& current);
    void onPeakGroupActivated();
    void refreshPlot();
    void requestTransitions(std::int64_t precursorId, std::int64_t runId);
    void applyFetchedTransitions();
    void updateScoreInspector();
    void reselectPrecursorAfterFilter();
    void clearPrecursorDetails();
    [[nodiscard]] std::int64_t currentRunId() const;
    [[nodiscard]] int selectedPeakGroupRow() const;

    std::shared_ptr<OswStore> store_;
    std::shared_ptr<ChromatogramSource> chromatograms_;
    std::vector<OswPrecursor> precursors_;
    std::vector<OswPeakGroup> peakGroups_;
    std::int64_t currentPrecursor_{-1};
    double currentLibraryRt_{0.0};
    std::vector<TransitionChromatogram> currentTransitions_;

    QComboBox* runSelector_{nullptr};
    QLineEdit* search_{nullptr};
    QDoubleSpinBox* maxQValue_{nullptr};
    QCheckBox* hideDecoys_{nullptr};
    QLabel* countLabel_{nullptr};
    QTableView* precursorView_{nullptr};
    OswPrecursorModel* precursorModel_{nullptr};
    OswPrecursorFilterProxy* precursorProxy_{nullptr};
    QTableView* peakGroupView_{nullptr};
    OswPeakGroupModel* peakGroupModel_{nullptr};
    QSortFilterProxyModel* peakGroupProxy_{nullptr};
    QCheckBox* showAllTransitions_{nullptr};
    QCheckBox* smoothTraces_{nullptr};
    TransitionGroupPlot* plot_{nullptr};
    QTableWidget* scoreTable_{nullptr};
    QLabel* chromatogramNote_{nullptr};

    QFutureWatcher<std::vector<TransitionChromatogram>>* fetchWatcher_{nullptr};
    std::int64_t pendingPrecursor_{-1};  ///< key of the in-flight fetch
    std::int64_t pendingRun_{-1};
    quint64 documentGeneration_{0};      ///< bumped on setData/clear; drops cross-document fetches
    quint64 pendingGeneration_{0};
    std::map<std::pair<std::int64_t, std::int64_t>, std::vector<TransitionChromatogram>> cache_;
    bool synchronizing_{false};
  };
}
