#pragma once

#include "model/ConsensusDocument.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace OpenMS { class ConsensusMap; }

namespace OpenMSViewer
{
  class ViewerDocument;

  // Resolves a consensus feature down to a concrete scan in the loaded raw run.
  // Pure (no UI) so the matching + exact/approximate fallback is unit-testable
  // independently of the peak-map wiring; MainWindow only turns the result into a
  // selection + toast.
  namespace ConsensusDrilldown
  {
    struct Target
    {
      bool runIsInputMap{false};                 ///< the loaded run matches one of the consensus columns
      bool ambiguousRun{false};                  ///< the run base name matches >1 column — can't safely resolve
      std::optional<std::int64_t> mapIndex;      ///< which input map the loaded run is, if uniquely matched
      std::optional<std::size_t> spectrumIndex;  ///< resolved scan index in the loaded run
      bool exact{false};                         ///< true: via spectrum_reference; false: nearest-by-RT
    };

    // Match the loaded run to a consensus column by base name, then resolve its
    // source scan: first exactly via a map_index-tagged peptide id's
    // spectrum_reference, else approximately via the map's handle RT.
    [[nodiscard]] Target resolve(const OpenMS::ConsensusMap& map, std::size_t featureIndex,
                                 const std::vector<ConsensusColumn>& columns,
                                 const ViewerDocument& document);
  }
}
