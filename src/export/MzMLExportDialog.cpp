#include "export/MzMLExportDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace OpenMSViewer
{
  MzMLExportDialog::MzMLExportDialog(
    const PlotRange& fullRange, const PlotRange& currentRange,
    const std::set<unsigned int>& msLevels, const std::vector<SpectrumRecord>& spectra,
    std::optional<double> activeFaimsCv, QWidget* parent)
    : QDialog(parent), fullRange_(fullRange), currentRange_(currentRange), spectra_(spectra),
      activeFaimsCv_(activeFaimsCv)
  {
    setWindowTitle(tr("Export filtered mzML"));
    setMinimumWidth(480);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
      tr("Choose the RT/m/z region and MS levels. Peaks outside the m/z range are removed; "
         "spectrum and aligned data-array metadata are preserved."), this));

    auto* ranges = new QGroupBox(tr("Data range"), this);
    auto* form = new QFormLayout(ranges);
    auto makeInput = [this](const QString& name, int decimals)
    {
      auto* input = new QDoubleSpinBox(this);
      input->setObjectName(name);
      input->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
      input->setDecimals(decimals);
      input->setKeyboardTracking(false);
      connect(input, qOverload<double>(&QDoubleSpinBox::valueChanged),
              this, &MzMLExportDialog::updatePreview);
      return input;
    };
    rtMinimum_ = makeInput(QStringLiteral("exportRtMinimum"), 3);
    rtMaximum_ = makeInput(QStringLiteral("exportRtMaximum"), 3);
    mzMinimum_ = makeInput(QStringLiteral("exportMzMinimum"), 5);
    mzMaximum_ = makeInput(QStringLiteral("exportMzMaximum"), 5);
    auto* rtRow = new QWidget(this);
    auto* rtLayout = new QHBoxLayout(rtRow);
    rtLayout->setContentsMargins(0, 0, 0, 0);
    rtLayout->addWidget(rtMinimum_);
    rtLayout->addWidget(new QLabel(tr("to"), this));
    rtLayout->addWidget(rtMaximum_);
    form->addRow(tr("RT (seconds)"), rtRow);
    auto* mzRow = new QWidget(this);
    auto* mzLayout = new QHBoxLayout(mzRow);
    mzLayout->setContentsMargins(0, 0, 0, 0);
    mzLayout->addWidget(mzMinimum_);
    mzLayout->addWidget(new QLabel(tr("to"), this));
    mzLayout->addWidget(mzMaximum_);
    form->addRow(tr("m/z"), mzRow);
    auto* rangeButtons = new QWidget(this);
    auto* rangeButtonLayout = new QHBoxLayout(rangeButtons);
    rangeButtonLayout->setContentsMargins(0, 0, 0, 0);
    auto* current = new QPushButton(tr("Use current peak-map view"), this);
    auto* full = new QPushButton(tr("Use full data range"), this);
    rangeButtonLayout->addWidget(current);
    rangeButtonLayout->addWidget(full);
    rangeButtonLayout->addStretch();
    form->addRow(rangeButtons);
    layout->addWidget(ranges);

    auto* levels = new QGroupBox(tr("MS levels"), this);
    auto* levelLayout = new QHBoxLayout(levels);
    for (const unsigned int level : msLevels)
    {
      auto* check = new QCheckBox(tr("MS%1").arg(level), this);
      check->setChecked(true);
      check->setObjectName(QStringLiteral("exportMs%1").arg(level));
      levelChecks_.push_back({level, check});
      levelLayout->addWidget(check);
      connect(check, &QCheckBox::toggled, this, &MzMLExportDialog::updatePreview);
    }
    levelLayout->addStretch();
    layout->addWidget(levels);

    if (activeFaimsCv_)
    {
      activeCvOnly_ = new QCheckBox(
        tr("Only active FAIMS channel (%1 V)").arg(*activeFaimsCv_, 0, 'f', 1), this);
      activeCvOnly_->setObjectName(QStringLiteral("exportActiveFaimsOnly"));
      activeCvOnly_->setChecked(true);
      layout->addWidget(activeCvOnly_);
      connect(activeCvOnly_, &QCheckBox::toggled, this, &MzMLExportDialog::updatePreview);
    }

    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("exportPreview"));
    layout->addWidget(preview_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Choose output…"));
    connect(buttons, &QDialogButtonBox::accepted, this, &MzMLExportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(current, &QPushButton::clicked, this, [this] { setRange(currentRange_); });
    connect(full, &QPushButton::clicked, this, [this] { setRange(fullRange_); });
    setRange(currentRange_);
  }

  MzMLExportFilter MzMLExportDialog::filter() const
  {
    MzMLExportFilter result;
    result.range = {rtMinimum_->value(), rtMaximum_->value(),
                    mzMinimum_->value(), mzMaximum_->value()};
    for (const auto& [level, check] : levelChecks_)
      if (check->isChecked()) result.msLevels.insert(level);
    if (activeCvOnly_ && activeCvOnly_->isChecked())
      result.faimsCompensationVoltage = activeFaimsCv_;
    return result;
  }

  void MzMLExportDialog::accept()
  {
    const MzMLExportFilter settings = filter();
    if (!settings.range.isValid())
    {
      QMessageBox::warning(this, tr("Invalid range"),
                           tr("RT maximum and m/z maximum must be larger than their minima."));
      return;
    }
    if (settings.msLevels.empty())
    {
      QMessageBox::warning(this, tr("No MS level"), tr("Select at least one MS level."));
      return;
    }
    QDialog::accept();
  }

  void MzMLExportDialog::setRange(const PlotRange& range)
  {
    rtMinimum_->setValue(range.rtMin);
    rtMaximum_->setValue(range.rtMax);
    mzMinimum_->setValue(range.mzMin);
    mzMaximum_->setValue(range.mzMax);
    updatePreview();
  }

  void MzMLExportDialog::updatePreview()
  {
    const MzMLExportFilter settings = filter();
    std::size_t spectra = 0;
    std::size_t peaks = 0;
    for (const SpectrumRecord& record : spectra_)
    {
      if (!settings.msLevels.contains(record.msLevel)
          || record.rt < settings.range.rtMin || record.rt > settings.range.rtMax) continue;
      if (settings.faimsCompensationVoltage
          && (!record.compensationVoltage
              || std::abs(*record.compensationVoltage - *settings.faimsCompensationVoltage) > 1e-6)) continue;
      ++spectra;
      peaks += record.peakCount;
    }
    preview_->setText(tr("%1 spectra match the RT/MS/CV filters · up to %2 input peaks before m/z trimming")
      .arg(spectra).arg(peaks));
  }
}
