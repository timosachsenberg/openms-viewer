#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QSpinBox;

namespace OpenMSViewer
{
  // Edits a single feature's RT / m/z / intensity / charge (TOPPView's
  // FeatureEditDialog). Used for both creating and editing manual features.
  class FeatureEditDialog final : public QDialog
  {
    Q_OBJECT

  public:
    FeatureEditDialog(double rt, double mz, double intensity, int charge,
                      QWidget* parent = nullptr);

    [[nodiscard]] double rt() const;
    [[nodiscard]] double mz() const;
    [[nodiscard]] double intensity() const;
    [[nodiscard]] int charge() const;

  private:
    QDoubleSpinBox* rt_{nullptr};
    QDoubleSpinBox* mz_{nullptr};
    QDoubleSpinBox* intensity_{nullptr};
    QSpinBox* charge_{nullptr};
  };
}
