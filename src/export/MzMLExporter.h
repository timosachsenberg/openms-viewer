#pragma once

#include "plot/PlotRange.h"

#include <OpenMS/KERNEL/MSExperiment.h>

#include <QString>

#include <cstddef>
#include <memory>
#include <optional>
#include <set>

namespace OpenMSViewer
{
  struct MzMLExportFilter
  {
    PlotRange range;
    std::set<unsigned int> msLevels;
    std::optional<double> faimsCompensationVoltage;
  };

  struct MzMLExportResult
  {
    QString outputPath;
    QString error;
    std::size_t spectrumCount{0};
    std::size_t peakCount{0};

    [[nodiscard]] bool succeeded() const noexcept { return error.isEmpty(); }
  };

  class MzMLExporter
  {
  public:
    [[nodiscard]] static std::shared_ptr<OpenMS::MSExperiment> filter(
      const OpenMS::MSExperiment& source,
      const MzMLExportFilter& filter);
    [[nodiscard]] static MzMLExportResult write(
      std::shared_ptr<const OpenMS::MSExperiment> source,
      const QString& outputPath,
      const MzMLExportFilter& filter);
  };
}
