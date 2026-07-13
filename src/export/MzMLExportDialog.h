#pragma once

#include "export/MzMLExporter.h"
#include "model/RunData.h"

#include <QDialog>

#include <optional>
#include <set>
#include <vector>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;

namespace OpenMSViewer
{
  class MzMLExportDialog final : public QDialog
  {
    Q_OBJECT

  public:
    MzMLExportDialog(const PlotRange& fullRange,
                     const PlotRange& currentRange,
                     const std::set<unsigned int>& msLevels,
                     const std::vector<SpectrumRecord>& spectra,
                     std::optional<double> activeFaimsCv,
                     QWidget* parent = nullptr,
                     bool rtInMinutes = false);

    [[nodiscard]] MzMLExportFilter filter() const;

  protected:
    void accept() override;

  private:
    void setRange(const PlotRange& range);
    void updatePreview();

    PlotRange fullRange_;
    PlotRange currentRange_;
    std::vector<SpectrumRecord> spectra_;
    std::optional<double> activeFaimsCv_;
    bool rtInMinutes_{false};
    QDoubleSpinBox* rtMinimum_{nullptr};
    QDoubleSpinBox* rtMaximum_{nullptr};
    QDoubleSpinBox* mzMinimum_{nullptr};
    QDoubleSpinBox* mzMaximum_{nullptr};
    std::vector<std::pair<unsigned int, QCheckBox*>> levelChecks_;
    QCheckBox* activeCvOnly_{nullptr};
    QLabel* preview_{nullptr};
  };
}
