#include "widgets/FeatureEditDialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

namespace OpenMSViewer
{
  FeatureEditDialog::FeatureEditDialog(double rt, double mz, double intensity, int charge,
                                       QWidget* parent)
    : QDialog(parent)
  {
    setWindowTitle(tr("Edit feature"));
    setObjectName(QStringLiteral("featureEditDialog"));

    // High precision so accepting the dialog with an untouched field does not quantize
    // away real RT / m·z digits.
    rt_ = new QDoubleSpinBox(this);
    rt_->setObjectName(QStringLiteral("featureEditRt"));
    rt_->setRange(0.0, 1.0e7);
    rt_->setDecimals(4);
    rt_->setSuffix(tr(" s"));
    rt_->setValue(rt);

    mz_ = new QDoubleSpinBox(this);
    mz_->setObjectName(QStringLiteral("featureEditMz"));
    mz_->setRange(0.0, 1.0e5);
    mz_->setDecimals(6);
    mz_->setValue(mz);

    intensity_ = new QDoubleSpinBox(this);
    intensity_->setObjectName(QStringLiteral("featureEditIntensity"));
    intensity_->setRange(0.0, 1.0e12);
    intensity_->setDecimals(2);
    intensity_->setValue(intensity);

    charge_ = new QSpinBox(this);
    charge_->setObjectName(QStringLiteral("featureEditCharge"));
    charge_->setRange(-20, 20);
    charge_->setValue(charge);

    auto* form = new QFormLayout;
    form->addRow(tr("Retention time"), rt_);
    form->addRow(tr("m/z"), mz_);
    form->addRow(tr("Intensity"), intensity_);
    form->addRow(tr("Charge"), charge_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
  }

  double FeatureEditDialog::rt() const { return rt_->value(); }
  double FeatureEditDialog::mz() const { return mz_->value(); }
  double FeatureEditDialog::intensity() const { return intensity_->value(); }
  int FeatureEditDialog::charge() const { return charge_->value(); }
}
