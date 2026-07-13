#pragma once

#include "model/ViewerDocument.h"

#include <QWidget>

#include <cstddef>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QTableView;

namespace OpenMSViewer
{
  class FeatureTableModel;

  class FeatureTableWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit FeatureTableWidget(QWidget* parent = nullptr);

    void setFeatures(const std::vector<FeatureRecord>& features);
    void clear();
    void selectFeature(std::size_t featureIndex);
    void setRtInMinutes(bool minutes);

  signals:
    void featureActivated(std::size_t featureIndex);

  private slots:
    void updateFilters();
    void resetFilters();
    void exportTsv();

  private:
    void updateCountLabel();

    FeatureTableModel* model_{nullptr};
    QSortFilterProxyModel* proxy_{nullptr};
    QTableView* table_{nullptr};
    QLineEdit* search_{nullptr};
    QDoubleSpinBox* minimumIntensity_{nullptr};
    QDoubleSpinBox* minimumQuality_{nullptr};
    QComboBox* charge_{nullptr};
    QLabel* countLabel_{nullptr};
    bool synchronizingSelection_{false};
  };
}
