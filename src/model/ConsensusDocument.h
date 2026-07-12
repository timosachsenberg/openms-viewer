#pragma once

#include "plot/PlotRange.h"

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace OpenMS { class ConsensusMap; }

namespace OpenMSViewer
{
  // One input map/column of a consensus map (which run/sample it came from).
  struct ConsensusColumn
  {
    std::int64_t mapIndex{0};
    QString filename;
    QString label;
  };

  // One per-map sub-feature (FeatureHandle) of a consensus feature — the
  // quantification in a single input map. Derived on demand, not stored per row.
  struct ConsensusHandle
  {
    std::int64_t mapIndex{0};
    double rt{0.0};
    double mz{0.0};
    double intensity{0.0};
    double width{0.0};
    int charge{0};
  };

  // Compact per-row summary of a consensus feature (the raw handles stay in the
  // ConsensusMap and are derived on demand — see ConsensusDocument::handlesFor —
  // so a large map isn't duplicated into UI records).
  struct ConsensusFeatureRecord
  {
    std::size_t index{0};
    double rt{0.0};
    double mz{0.0};
    int charge{0};
    double quality{0.0};
    double storedIntensity{0.0};   ///< the consensus feature's own intensity (as stored)
    double sumIntensity{0.0};      ///< sum over present handles
    double meanIntensity{0.0};     ///< mean over present handles
    int coveredMaps{0};            ///< distinct input maps with a handle
    int totalMaps{0};
    PlotRange bounds;              ///< handle position range (the "alignment envelope")
    QString bestPeptide;           ///< best attached peptide ID (if any)
    int peptideIdCount{0};

    [[nodiscard]] int missingMaps() const noexcept { return totalMaps - coveredMaps; }
  };

  struct ConsensusLoadResult
  {
    std::shared_ptr<OpenMS::ConsensusMap> map;
    std::vector<ConsensusFeatureRecord> features;
    std::vector<ConsensusColumn> columns;
    QString experimentType;
    QString sourcePath;
    QString error;

    [[nodiscard]] bool succeeded() const noexcept { return map != nullptr && error.isEmpty(); }
  };

  // Loads consensusXML / CONSENSUSPARQUET into a ConsensusMap and builds compact
  // per-feature summaries + column metadata. Runs off the GUI thread.
  namespace ConsensusDocument
  {
    [[nodiscard]] ConsensusLoadResult read(const QString& path);
    // Per-map handles of one consensus feature, computed on demand from the map.
    [[nodiscard]] std::vector<ConsensusHandle> handlesFor(const OpenMS::ConsensusMap& map,
                                                          std::size_t index);
    // Write a consensus map back to consensusXML. Returns false with @p error set.
    [[nodiscard]] bool save(const OpenMS::ConsensusMap& map, const QString& path, QString& error);
  }
}
