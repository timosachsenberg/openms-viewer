#include "model/ConsensusDrilldown.h"

#include "model/ViewerDocument.h"

#include <OpenMS/KERNEL/ConsensusFeature.h>
#include <OpenMS/KERNEL/ConsensusMap.h>
#include <OpenMS/KERNEL/FeatureHandle.h>
#include <OpenMS/METADATA/PeptideIdentification.h>

#include <QFileInfo>
#include <QString>

#include <cmath>

namespace OpenMSViewer::ConsensusDrilldown
{
  Target resolve(const OpenMS::ConsensusMap& map, std::size_t featureIndex,
                 const std::vector<ConsensusColumn>& columns, const ViewerDocument& document)
  {
    Target target;
    if (featureIndex >= map.size()) return target;

    // Which input map (if any) is the loaded raw run? Match on base name — a run may
    // be listed as run.mzML here yet run.featureXML in the consensus columns. Prefer
    // a full-path match when present; fall back to base name only if it is UNIQUE,
    // since two batches can share a base name (batchA/sample vs batchB/sample) and a
    // wrong "exact" hit would open a same-named scan from the wrong sample.
    const QFileInfo runInfo(document.sourcePath());
    const QString runBase = runInfo.completeBaseName();
    if (runBase.isEmpty()) return target;
    const QString runAbsolute = runInfo.absoluteFilePath();
    std::optional<std::int64_t> exactPathMap;
    std::vector<std::int64_t> baseMatches;
    for (const ConsensusColumn& column : columns)
    {
      const QFileInfo columnInfo(column.filename);
      if (!column.filename.isEmpty()
          && columnInfo.absoluteFilePath().compare(runAbsolute, Qt::CaseInsensitive) == 0)
        exactPathMap = column.mapIndex;
      const QString columnBase = columnInfo.completeBaseName();
      if (!columnBase.isEmpty() && columnBase.compare(runBase, Qt::CaseInsensitive) == 0)
        baseMatches.push_back(column.mapIndex);
    }
    if (exactPathMap)
    {
      target.runIsInputMap = true;
      target.mapIndex = exactPathMap;
    }
    else if (baseMatches.size() == 1)
    {
      target.runIsInputMap = true;
      target.mapIndex = baseMatches.front();
    }
    else if (baseMatches.size() > 1)
    {
      target.runIsInputMap = true;
      target.ambiguousRun = true;  // name matches multiple input maps — refuse to guess
    }
    if (!target.mapIndex) return target;

    const OpenMS::ConsensusFeature& feature = map[featureIndex];

    // 1) Exact: a peptide id tagged with this map_index carries the source scan's
    //    spectrum_reference (native ID). Resolve it against the loaded run.
    for (const auto& peptideId : feature.getPeptideIdentifications())
    {
      if (!peptideId.metaValueExists("map_index")) continue;
      bool parsed = false;
      // map_index may round-trip as an int or a string DataValue; parse leniently.
      const qlonglong pidMap = QString::fromStdString(
        peptideId.getMetaValue("map_index").toString()).toLongLong(&parsed);
      if (!parsed || pidMap != *target.mapIndex) continue;
      const QString reference = QString::fromStdString(peptideId.getSpectrumReference());
      if (const auto spectrumIndex = document.spectrumIndexForNativeId(reference))
      {
        target.spectrumIndex = spectrumIndex;
        target.exact = true;
        return target;
      }
    }

    // 2) Approximate: navigate to the nearest scan to this map's handle (by RT).
    //    Consensus features are quantified from MS1 survey scans, so prefer MS1 and
    //    only fall back to any level when the run has none (e.g. an MS2-only method).
    for (const auto& handle : feature.getFeatureList())
    {
      if (static_cast<std::int64_t>(handle.getMapIndex()) != *target.mapIndex) continue;
      if (!std::isfinite(handle.getRT())) break;
      auto spectrumIndex = document.nearestSpectrumIndex(handle.getRT(), 1);
      if (!spectrumIndex) spectrumIndex = document.nearestSpectrumIndex(handle.getRT());
      if (spectrumIndex)
      {
        target.spectrumIndex = spectrumIndex;
        target.exact = false;
        return target;
      }
    }
    return target;  // run is an input map, but this feature has no handle in it
  }
}
