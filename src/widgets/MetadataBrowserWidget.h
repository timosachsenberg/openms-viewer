#pragma once

#include <OpenMS/KERNEL/MSExperiment.h>

#include <QWidget>

#include <cstddef>
#include <memory>
#include <optional>

class QTreeWidget;

namespace OpenMSViewer
{
  // Read-only tree over an MSExperiment's metadata (TOPPView's MetaDataBrowser):
  // experiment-level instrument / sample / source files / data processing, plus the
  // selected scan's acquisition + precursor detail and any MetaInfo user params.
  // Answers "what is this file" without mutating anything.
  class MetadataBrowserWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit MetadataBrowserWidget(QWidget* parent = nullptr);

    void setExperiment(std::shared_ptr<const OpenMS::MSExperiment> experiment);
    void setSpectrumIndex(std::optional<std::size_t> index);
    void clear();

  private:
    void rebuild();

    std::shared_ptr<const OpenMS::MSExperiment> experiment_;
    std::optional<std::size_t> spectrumIndex_;
    QTreeWidget* tree_{nullptr};
  };
}
