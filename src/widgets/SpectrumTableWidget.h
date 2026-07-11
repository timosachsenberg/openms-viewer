#pragma once

#include "model/IdentificationData.h"
#include "model/RunData.h"

#include <QWidget>

#include <cstddef>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QTableView;

namespace OpenMSViewer
{
  class SpectrumTableModel;

  class SpectrumTableWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit SpectrumTableWidget(QWidget* parent = nullptr);

    void setData(const std::vector<SpectrumRecord>& spectra,
                 const std::vector<IdentificationRecord>& identifications);
    void clear();
    void selectSpectrum(std::size_t spectrumIndex,
                        std::optional<std::size_t> identificationIndex = std::nullopt,
                        std::optional<std::size_t> hitIndex = std::nullopt);
    [[nodiscard]] int filteredRowCount() const noexcept;

  signals:
    void spectrumActivated(std::size_t spectrumIndex, int identificationIndex, int hitIndex);

  private slots:
    void updateFilters();
    void updateColumns();
    void resetFilters();
    void exportTsv();

  private:
    void updateCountLabel();

    SpectrumTableModel* model_{nullptr};
    QSortFilterProxyModel* proxy_{nullptr};
    QTableView* table_{nullptr};
    QComboBox* mode_{nullptr};
    QLineEdit* minimumRt_{nullptr};
    QLineEdit* maximumRt_{nullptr};
    QLineEdit* sequence_{nullptr};
    QLineEdit* minimumScore_{nullptr};
    QCheckBox* advanced_{nullptr};
    QCheckBox* metadata_{nullptr};
    QCheckBox* allHits_{nullptr};
    QLabel* countLabel_{nullptr};
    bool synchronizingSelection_{false};
  };
}
