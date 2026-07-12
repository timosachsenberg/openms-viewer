#include "model/ConsensusDocument.h"

#include "model/FormatRegistry.h"

#include <OpenMS/FORMAT/FileHandler.h>
#include <OpenMS/KERNEL/ConsensusMap.h>

#include <QFileInfo>

#include <algorithm>
#include <limits>
#include <set>

namespace OpenMSViewer::ConsensusDocument
{
  namespace
  {
    QString bestPeptide(const OpenMS::ConsensusFeature& feature, int& count)
    {
      const auto& ids = feature.getPeptideIdentifications();
      count = static_cast<int>(ids.size());
      for (const auto& identification : ids)
        if (!identification.getHits().empty())
          return QString::fromStdString(identification.getHits().front().getSequence().toString());
      return {};
    }
  }

  ConsensusLoadResult read(const QString& path)
  {
    ConsensusLoadResult result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();
    const QFileInfo file(result.sourcePath);
    if (!file.exists())
    {
      result.error = QStringLiteral("File does not exist: %1").arg(result.sourcePath);
      return result;
    }
    const auto info = FormatRegistry::detect(result.sourcePath);
    if (info.category != FormatRegistry::Category::Consensus || !info.supported)
    {
      result.error = QStringLiteral("Unsupported consensus map: %1").arg(result.sourcePath);
      return result;
    }

    try
    {
      auto map = std::make_shared<OpenMS::ConsensusMap>();
      OpenMS::FileHandler().loadConsensusFeatures(
        result.sourcePath.toStdString(), *map,
        FormatRegistry::allowedTypes(FormatRegistry::Category::Consensus));

      // Column metadata (which input run/sample each map index came from).
      for (const auto& [mapIndex, header] : map->getColumnHeaders())
        result.columns.push_back({static_cast<std::int64_t>(mapIndex),
                                  QString::fromStdString(header.filename),
                                  QString::fromStdString(header.label)});
      std::sort(result.columns.begin(), result.columns.end(),
                [](const ConsensusColumn& a, const ConsensusColumn& b) { return a.mapIndex < b.mapIndex; });
      const int totalMaps = static_cast<int>(result.columns.size());
      result.experimentType = QString::fromStdString(map->getExperimentType());

      result.features.reserve(map->size());
      for (std::size_t index = 0; index < map->size(); ++index)
      {
        const OpenMS::ConsensusFeature& feature = (*map)[index];
        ConsensusFeatureRecord record;
        record.index = index;
        record.rt = feature.getRT();
        record.mz = feature.getMZ();
        record.charge = feature.getCharge();
        record.quality = feature.getQuality();
        record.storedIntensity = feature.getIntensity();
        record.totalMaps = totalMaps;

        std::set<OpenMS::UInt64> coveredMaps;
        double sum = 0.0;
        int handleCount = 0;
        for (const OpenMS::FeatureHandle& handle : feature.getFeatureList())
        {
          coveredMaps.insert(handle.getMapIndex());
          sum += handle.getIntensity();
          ++handleCount;
        }
        record.coveredMaps = static_cast<int>(coveredMaps.size());
        record.sumIntensity = sum;
        record.meanIntensity = handleCount > 0 ? sum / handleCount : 0.0;

        // Alignment envelope = the RT/m/z span of the handle centroids (NOT a
        // feature hull). Pad a degenerate span so it stays drawable.
        const OpenMS::DRange<2> range = feature.getPositionRange();
        double rtMin = range.minPosition()[OpenMS::Peak2D::RT];
        double rtMax = range.maxPosition()[OpenMS::Peak2D::RT];
        double mzMin = range.minPosition()[OpenMS::Peak2D::MZ];
        double mzMax = range.maxPosition()[OpenMS::Peak2D::MZ];
        if (!(rtMax > rtMin)) { rtMin = record.rt - 1.0; rtMax = record.rt + 1.0; }
        if (!(mzMax > mzMin)) { mzMin = record.mz - 0.01; mzMax = record.mz + 0.01; }
        record.bounds = {rtMin, rtMax, mzMin, mzMax};

        record.bestPeptide = bestPeptide(feature, record.peptideIdCount);
        result.features.push_back(std::move(record));
      }
      result.map = std::move(map);
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("OpenMS could not read the consensus map: %1")
                       .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("OpenMS could not read the consensus map (unknown error).");
    }
    return result;
  }

  std::vector<ConsensusHandle> handlesFor(const OpenMS::ConsensusMap& map, std::size_t index)
  {
    std::vector<ConsensusHandle> handles;
    if (index >= map.size()) return handles;
    for (const OpenMS::FeatureHandle& handle : map[index].getFeatureList())
      handles.push_back({static_cast<std::int64_t>(handle.getMapIndex()), handle.getRT(),
                         handle.getMZ(), handle.getIntensity(), handle.getWidth(),
                         handle.getCharge()});
    return handles;
  }

  bool save(const OpenMS::ConsensusMap& map, const QString& path, QString& error)
  {
    try
    {
      OpenMS::FileHandler().storeConsensusFeatures(path.toStdString(), map);
      return true;
    }
    catch (const std::exception& exception)
    {
      error = QStringLiteral("Could not save consensus map: %1")
                .arg(QString::fromLocal8Bit(exception.what()));
    }
    catch (...)
    {
      error = QStringLiteral("Could not save consensus map (unknown error).");
    }
    return false;
  }
}
