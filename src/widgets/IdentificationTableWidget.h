#pragma once

#include "model/IdentificationData.h"

#include <QWidget>

#include <cstddef>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSortFilterProxyModel;
class QTableView;

namespace OpenMSViewer
{
  class IdentificationTableModel;

  class IdentificationTableWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit IdentificationTableWidget(QWidget* parent = nullptr);

    void setIdentifications(const std::vector<IdentificationRecord>& identifications);
    void clear();
    void selectIdentification(std::size_t identificationIndex, std::size_t hitIndex = 0);

  signals:
    void identificationActivated(std::size_t identificationIndex, std::size_t hitIndex);

  private slots:
    void updateFilters();
    void rebuildRows();
    void exportTsv();

  private:
    void updateCountLabel();
    void updateDetails(int sourceRow);

    IdentificationTableModel* model_{nullptr};
    QSortFilterProxyModel* proxy_{nullptr};
    QTableView* table_{nullptr};
    QComboBox* viewMode_{nullptr};
    QLineEdit* sequenceFilter_{nullptr};
    QLineEdit* minimumScore_{nullptr};
    QCheckBox* showAllHits_{nullptr};
    QLabel* countLabel_{nullptr};
    QPlainTextEdit* details_{nullptr};
    bool synchronizingSelection_{false};
  };
}
