#pragma once

#include "model/ConsensusDocument.h"

#include <QWidget>

#include <vector>

namespace OpenMSViewer
{
  // Per-map intensity of one consensus feature: a bar per input map (or a compact
  // dot strip when there are many), with MISSING maps shown as gaps (never
  // zero-height bars), a raw/log intensity scale, and per-map filename/label axis
  // labels. Coverage/CV are summarised by the hosting panel.
  class ConsensusQuantChart final : public QWidget
  {
    Q_OBJECT

  public:
    explicit ConsensusQuantChart(QWidget* parent = nullptr);

    void setData(std::vector<ConsensusHandle> handles, std::vector<ConsensusColumn> columns);
    void setLogScale(bool logScale);
    void clear();

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    [[nodiscard]] QRectF plotRect() const;

    std::vector<ConsensusHandle> handles_;
    std::vector<ConsensusColumn> columns_;
    bool logScale_{false};
  };
}
