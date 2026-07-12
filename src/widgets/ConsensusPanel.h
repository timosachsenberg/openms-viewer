#pragma once

#include "model/ConsensusDocument.h"

#include <QWidget>

#include <memory>
#include <vector>

namespace OpenMS { class ConsensusMap; }

class QCheckBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QSpinBox;
class QTableView;

namespace OpenMSViewer
{
  class ConsensusQuantChart;
  class ConsensusFeatureModel;
  class ConsensusFilterProxy;

  // Multi-run quantification view: a searchable/filterable consensus-feature
  // table drives a per-map intensity chart (ConsensusQuantChart) plus a
  // coverage / CV / ID summary. Emits featureActivated so the peak map can
  // highlight the selected consensus feature.
  class ConsensusPanel final : public QWidget
  {
    Q_OBJECT

  public:
    explicit ConsensusPanel(QWidget* parent = nullptr);

    void setData(std::shared_ptr<OpenMS::ConsensusMap> map,
                 std::vector<ConsensusFeatureRecord> features,
                 std::vector<ConsensusColumn> columns,
                 const QString& experimentType);
    void clear();
    void selectFeature(std::size_t index);

  signals:
    void featureActivated(qint64 index);

  private:
    void onFeatureActivated(const QModelIndex& current);
    void reselectAfterFilter();
    void clearDetails();

    std::shared_ptr<OpenMS::ConsensusMap> map_;
    std::vector<ConsensusFeatureRecord> features_;
    std::vector<ConsensusColumn> columns_;
    QString experimentType_;

    QLineEdit* search_{nullptr};
    QSpinBox* minMaps_{nullptr};
    QCheckBox* logScale_{nullptr};
    QLabel* countLabel_{nullptr};
    QLabel* detailLabel_{nullptr};
    QTableView* view_{nullptr};
    ConsensusFeatureModel* model_{nullptr};
    ConsensusFilterProxy* proxy_{nullptr};
    ConsensusQuantChart* chart_{nullptr};
    bool synchronizing_{false};
  };
}
