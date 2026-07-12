#include "model/ViewerDocument.h"
#include "model/FormatRegistry.h"

#include <OpenMS/FORMAT/FileHandler.h>
#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/FORMAT/FeatureXMLFile.h>
#include <OpenMS/FORMAT/IdXMLFile.h>
// Vendor readers self-disable via WITH_THERMO_RAW / WITH_OPENTIMS (OpenMS PUBLIC
// compile definitions inherited through the OpenMS target), so these are safe to
// include unconditionally — the classes simply aren't declared when unsupported.
#include <OpenMS/FORMAT/ThermoRawFile.h>
#include <OpenMS/FORMAT/BrukerTimsFile.h>
#include <OpenMS/METADATA/MetaInfoInterface.h>
#include <OpenMS/IONMOBILITY/IMTypes.h>
#include <OpenMS/CONCEPT/ProgressLogger.h>

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace OpenMSViewer
{
  namespace
  {
    class LoadCancelled final : public std::exception
    {
    public:
      const char* what() const noexcept override { return "Loading cancelled"; }
    };

    class ViewerProgressLogger final : public OpenMS::ProgressLogger::ProgressLoggerImpl
    {
    public:
      ViewerProgressLogger(ViewerDocument::ProgressCallback callback,
                           ViewerDocument::CancellationCheck cancelled)
        : callback_(std::move(callback)), cancelled_(std::move(cancelled)) {}

      void startProgress(OpenMS::SignedSize begin, OpenMS::SignedSize end,
                         const std::string& label, int) const override
      {
        begin_ = begin;
        end_ = end;
        label_ = QString::fromStdString(label);
        lastPercent_ = -1;
        publish(begin);
      }

      void setProgress(OpenMS::SignedSize value, int) const override { publish(value); }

      OpenMS::SignedSize nextProgress() const override
      {
        return ++current_;
      }

      void endProgress(int, OpenMS::UInt64) const override
      {
        if (callback_) callback_(label_, 100);
        lastPercent_ = 100;
      }

    private:
      void publish(OpenMS::SignedSize value) const
      {
        checkCancelled();
        current_ = value;
        const int percent = end_ > begin_
          ? std::clamp(static_cast<int>((value - begin_) * 100 / (end_ - begin_)), 0, 100) : 0;
        if (percent == lastPercent_) return;
        lastPercent_ = percent;
        if (callback_) callback_(label_, percent);
      }

      void checkCancelled() const
      {
        if (cancelled_ && cancelled_()) throw LoadCancelled();
      }

      ViewerDocument::ProgressCallback callback_;
      ViewerDocument::CancellationCheck cancelled_;
      mutable OpenMS::SignedSize begin_{0};
      mutable OpenMS::SignedSize end_{0};
      mutable OpenMS::SignedSize current_{0};
      mutable int lastPercent_{-1};
      mutable QString label_;
    };

    MetaValues collectMetaValues(const OpenMS::MetaInfoInterface& source)
    {
      std::vector<std::string> keys;
      source.getKeys(keys);
      MetaValues result;
      result.reserve(keys.size());
      for (const std::string& key : keys)
      {
        result.emplace_back(QString::fromStdString(key),
                            QString::fromStdString(source.getMetaValue(key).toString()));
      }
      return result;
    }

    std::optional<double> compensationVoltage(const OpenMS::MSSpectrum& spectrum)
    {
      if (spectrum.getDriftTimeUnit() == OpenMS::DriftTimeUnit::FAIMS_COMPENSATION_VOLTAGE)
        return spectrum.getDriftTime();
      for (const std::string key : {std::string("FAIMS compensation voltage"),
                                    std::string("ion mobility drift time"),
                                    std::string("MS:1001581")})
      {
        if (!spectrum.metaValueExists(key)) continue;
        try
        {
          return static_cast<double>(spectrum.getMetaValue(key));
        }
        catch (...)
        {
        }
      }
      return std::nullopt;
    }
    // Shared post-load derivation over an in-memory experiment (mzML, Thermo RAW,
    // Bruker TDF): sorts, then fills summaries / TIC / chromatograms / ion-mobility
    // frames / FAIMS channels / bounds on `result`. Sets result.experiment on
    // success or result.error on a soft failure; throws LoadCancelled if cancelled.
    void deriveLoadResult(ViewerDocument::LoadResult& result,
                          std::shared_ptr<OpenMS::MSExperiment> experiment,
                          const ViewerDocument::CancellationCheck& cancelled)
    {
      if (experiment->empty() && experiment->getChromatograms().empty())
      {
        result.error = QStringLiteral("The file contains no spectra or chromatograms.");
        return;
      }

      // Rasterization and binary RT/m/z searches require sorted input.
      experiment->sortSpectra(true);
      experiment->sortChromatograms(true);

      double minRt = std::numeric_limits<double>::infinity();
      double maxRt = -std::numeric_limits<double>::infinity();
      double minMz = std::numeric_limits<double>::infinity();
      double maxMz = -std::numeric_limits<double>::infinity();
      unsigned int minLevel = std::numeric_limits<unsigned int>::max();
      unsigned int maxLevel = 0;

      result.statistics.spectrumCount = experiment->size();
      result.tic.reserve(experiment->size());
      result.spectra.reserve(experiment->size());
      std::map<double, FaimsChannelRecord> faimsChannels;

      for (std::size_t index = 0; index < experiment->size(); ++index)
      {
        if (cancelled && cancelled()) throw LoadCancelled();
        const auto& spectrum = (*experiment)[index];
        const unsigned int level = spectrum.getMSLevel();
        minLevel = std::min(minLevel, level);
        maxLevel = std::max(maxLevel, level);
        result.statistics.peakCount += spectrum.size();

        SpectrumRecord record;
        record.index = index;
        record.rt = spectrum.getRT();
        record.msLevel = level;
        record.nativeId = QString::fromStdString(spectrum.getNativeID());
        record.peakCount = spectrum.size();
        record.compensationVoltage = compensationVoltage(spectrum);
        if (level > 1 && !spectrum.getPrecursors().empty())
        {
          const auto& precursor = spectrum.getPrecursors().front();
          record.precursorMz = precursor.getMZ();
          record.precursorCharge = precursor.getCharge();
          record.isolationLowerOffset = precursor.getIsolationWindowLowerOffset();
          record.isolationUpperOffset = precursor.getIsolationWindowUpperOffset();
        }
        if (!spectrum.empty())
        {
          record.mzMin = spectrum.front().getMZ();
          record.mzMax = spectrum.back().getMZ();
        }
        for (const auto& peak : spectrum)
        {
          record.tic += peak.getIntensity();
          record.basePeakIntensity = std::max(record.basePeakIntensity,
                                              static_cast<double>(peak.getIntensity()));
        }

        if (spectrum.containsIMData() && !spectrum.empty())
        {
          const auto [arrayIndex, unit] = spectrum.getIMData();
          const auto& mobility = spectrum.getFloatDataArrays()[arrayIndex];
          if (mobility.size() == spectrum.size())
          {
            IonMobilityFrameRecord frame;
            frame.spectrumIndex = index;
            frame.rt = record.rt;
            frame.msLevel = level;
            frame.unit = QString::fromStdString(OpenMS::driftTimeUnitToString(unit));
            frame.mobilityMin = std::numeric_limits<double>::infinity();
            frame.mobilityMax = -std::numeric_limits<double>::infinity();
            frame.mzMin = record.mzMin;
            frame.mzMax = record.mzMax;
            for (const float value : mobility)
            {
              frame.mobilityMin = std::min(frame.mobilityMin, static_cast<double>(value));
              frame.mobilityMax = std::max(frame.mobilityMax, static_cast<double>(value));
            }
            if (level > 1 && !spectrum.getPrecursors().empty())
            {
              const auto& precursor = spectrum.getPrecursors().front();
              frame.precursorMz = precursor.getMZ();
              frame.isolationWindowLower = precursor.getMZ()
                - precursor.getIsolationWindowLowerOffset();
              frame.isolationWindowUpper = precursor.getMZ()
                + precursor.getIsolationWindowUpperOffset();
            }
            result.ionMobilityFrames.push_back(std::move(frame));
          }
        }
        if (level == 1 && record.compensationVoltage)
        {
          auto& channel = faimsChannels[*record.compensationVoltage];
          channel.compensationVoltage = *record.compensationVoltage;
          channel.tic.push_back({record.rt, record.tic, index});
          channel.totalIntensity += record.tic;
        }
        result.spectra.push_back(record);

        if (level != 1) continue;

        ++result.statistics.ms1SpectrumCount;
        result.statistics.ms1PeakCount += spectrum.size();
        const double rt = spectrum.getRT();
        minRt = std::min(minRt, rt);
        maxRt = std::max(maxRt, rt);
        result.tic.push_back({rt, record.tic, index});

        for (const auto& peak : spectrum)
        {
          minMz = std::min(minMz, static_cast<double>(peak.getMZ()));
          maxMz = std::max(maxMz, static_cast<double>(peak.getMZ()));
        }
      }

      result.chromatograms.reserve(experiment->getChromatograms().size());
      double chromatogramMinRt = std::numeric_limits<double>::infinity();
      double chromatogramMaxRt = -std::numeric_limits<double>::infinity();
      double chromatogramMinMz = std::numeric_limits<double>::infinity();
      double chromatogramMaxMz = -std::numeric_limits<double>::infinity();
      for (std::size_t index = 0; index < experiment->getChromatograms().size(); ++index)
      {
        if (cancelled && cancelled()) throw LoadCancelled();
        const auto& source = experiment->getChromatograms()[index];
        ChromatogramRecord record;
        record.index = index;
        record.nativeId = QString::fromStdString(source.getNativeID());
        const auto chromatogramType = source.getChromatogramType();
        record.type = QString::fromLatin1(OpenMS::ChromatogramSettings::ChromatogramNames[
          static_cast<std::size_t>(chromatogramType)]);
        record.isTic = chromatogramType
          == OpenMS::ChromatogramSettings::ChromatogramType::TOTAL_ION_CURRENT_CHROMATOGRAM;
        if (source.getPrecursor().getMZ() > 0.0)
        {
          record.precursorMz = source.getPrecursor().getMZ();
          chromatogramMinMz = std::min(chromatogramMinMz, *record.precursorMz);
          chromatogramMaxMz = std::max(chromatogramMaxMz, *record.precursorMz);
        }
        record.precursorCharge = source.getPrecursor().getCharge();
        if (source.getProduct().getMZ() > 0.0)
        {
          record.productMz = source.getProduct().getMZ();
          chromatogramMinMz = std::min(chromatogramMinMz, *record.productMz);
          chromatogramMaxMz = std::max(chromatogramMaxMz, *record.productMz);
        }
        record.points.reserve(source.size());
        if (!source.empty())
        {
          record.rtMin = source.front().getRT();
          record.rtMax = source.front().getRT();
        }
        for (const auto& peak : source)
        {
          record.rtMin = std::min(record.rtMin, peak.getRT());
          record.rtMax = std::max(record.rtMax, peak.getRT());
          chromatogramMinRt = std::min(chromatogramMinRt, peak.getRT());
          chromatogramMaxRt = std::max(chromatogramMaxRt, peak.getRT());
          record.maximumIntensity = std::max(record.maximumIntensity,
                                             static_cast<double>(peak.getIntensity()));
          record.totalIntensity += peak.getIntensity();
          record.points.push_back({peak.getRT(), peak.getIntensity()});
        }
        result.chromatograms.push_back(std::move(record));
      }

      result.statistics.minimumMsLevel = minLevel == std::numeric_limits<unsigned int>::max() ? 0 : minLevel;
      result.statistics.maximumMsLevel = maxLevel;
      result.ticLabel = QStringLiteral("MS1 TIC");

      // Unusual MSn-only files still get meaningful bounds and a BPC trace.
      if (result.statistics.ms1SpectrumCount == 0 && result.statistics.spectrumCount > 0)
      {
        const unsigned int fallbackLevel = result.statistics.minimumMsLevel;
        result.ticLabel = QStringLiteral("MS%1 BPC").arg(fallbackLevel);
        for (std::size_t index = 0; index < experiment->size(); ++index)
        {
          const auto& spectrum = (*experiment)[index];
          if (spectrum.getMSLevel() != fallbackLevel) continue;

          const double rt = spectrum.getRT();
          minRt = std::min(minRt, rt);
          maxRt = std::max(maxRt, rt);
          double bpi = 0.0;
          for (const auto& peak : spectrum)
          {
            minMz = std::min(minMz, static_cast<double>(peak.getMZ()));
            maxMz = std::max(maxMz, static_cast<double>(peak.getMZ()));
            bpi = std::max(bpi, static_cast<double>(peak.getIntensity()));
          }
          result.tic.push_back({rt, bpi, index});
        }
      }

      if (!std::isfinite(minRt) || !std::isfinite(minMz))
      {
        if (result.chromatograms.empty())
        {
          result.error = QStringLiteral("The file contains no displayable peaks.");
          return;
        }
        minRt = std::isfinite(chromatogramMinRt) ? chromatogramMinRt : 0.0;
        maxRt = std::isfinite(chromatogramMaxRt) ? chromatogramMaxRt : 1.0;
        minMz = std::isfinite(chromatogramMinMz) ? chromatogramMinMz : 0.0;
        maxMz = std::isfinite(chromatogramMaxMz) ? chromatogramMaxMz : 1.0;
        result.ticLabel = QStringLiteral("No spectral TIC");
      }

      if (maxRt <= minRt) maxRt = minRt + 1.0;
      if (maxMz <= minMz) maxMz = minMz + 1.0;
      if (faimsChannels.size() > 1)
      {
        result.faimsChannels.reserve(faimsChannels.size());
        result.faimsExperiments.reserve(faimsChannels.size());
        for (auto& [voltage, channel] : faimsChannels)
        {
          auto filtered = std::make_shared<OpenMS::MSExperiment>();
          for (const FaimsTracePoint& point : channel.tic)
            filtered->addSpectrum((*experiment)[point.spectrumIndex]);
          filtered->sortSpectra(true);
          result.faimsChannels.push_back(std::move(channel));
          result.faimsExperiments.push_back(std::move(filtered));
        }
      }
      result.bounds = {minRt, maxRt, minMz, maxMz};
      result.experiment = std::move(experiment);
    }

    // Flatten an OpenMS FeatureMap into UI FeatureRecords (rt/mz/intensity/charge/
    // quality + convex hulls + bounding box). Shared by every feature format.
    std::vector<FeatureRecord> buildFeatureRecords(const OpenMS::FeatureMap& map)
    {
      std::vector<FeatureRecord> features;
      features.reserve(map.size());
      for (std::size_t index = 0; index < map.size(); ++index)
      {
        const auto& source = map[index];
        FeatureRecord feature;
        feature.index = index;
        feature.rt = source.getRT();
        feature.mz = source.getMZ();
        feature.intensity = source.getIntensity();
        feature.charge = source.getCharge();
        feature.quality = source.getOverallQuality();

        double rtMin = std::numeric_limits<double>::infinity();
        double rtMax = -std::numeric_limits<double>::infinity();
        double mzMin = std::numeric_limits<double>::infinity();
        double mzMax = -std::numeric_limits<double>::infinity();
        feature.hulls.reserve(source.getConvexHulls().size());
        for (const auto& sourceHull : source.getConvexHulls())
        {
          std::vector<FeaturePoint> hull;
          hull.reserve(sourceHull.getHullPoints().size());
          for (const auto& point : sourceHull.getHullPoints())
          {
            const double rt = point[0];
            const double mz = point[1];
            hull.push_back({rt, mz});
            rtMin = std::min(rtMin, rt);
            rtMax = std::max(rtMax, rt);
            mzMin = std::min(mzMin, mz);
            mzMax = std::max(mzMax, mz);
          }
          if (!hull.empty()) feature.hulls.push_back(std::move(hull));
        }
        if (!std::isfinite(rtMin))
        {
          rtMin = feature.rt - 1.0;
          rtMax = feature.rt + 1.0;
          mzMin = feature.mz - 0.5;
          mzMax = feature.mz + 0.5;
        }
        if (rtMax <= rtMin) rtMax = rtMin + 1e-6;
        if (mzMax <= mzMin) mzMax = mzMin + 1e-8;
        feature.bounds = {rtMin, rtMax, mzMin, mzMax};
        features.push_back(std::move(feature));
      }
      return features;
    }

    // Flatten peptide identifications (+ hits, meta values, peak annotations) into
    // UI records. Shared by idXML, mzIdentML and idparquet.
    std::vector<IdentificationRecord> buildIdentificationRecords(
      const OpenMS::PeptideIdentificationList& peptides)
    {
      std::vector<IdentificationRecord> identifications;
      identifications.reserve(peptides.size());
      for (std::size_t index = 0; index < peptides.size(); ++index)
      {
        const auto& source = peptides[index];
        IdentificationRecord identification;
        identification.index = index;
        identification.rt = source.getRT();
        identification.mz = source.getMZ();
        identification.scoreType = QString::fromStdString(source.getScoreType());
        identification.higherScoreBetter = source.isHigherScoreBetter();
        identification.identifier = QString::fromStdString(source.getIdentifier());
        identification.spectrumReference = QString::fromStdString(source.getSpectrumReference());
        identification.metaValues = collectMetaValues(source);
        identification.hits.reserve(source.getHits().size());
        for (std::size_t hitIndex = 0; hitIndex < source.getHits().size(); ++hitIndex)
        {
          const auto& sourceHit = source.getHits()[hitIndex];
          PeptideHitRecord hit;
          hit.index = hitIndex;
          hit.sequence = QString::fromStdString(sourceHit.getSequence().toString());
          hit.score = sourceHit.getScore();
          hit.charge = sourceHit.getCharge();
          hit.metaValues = collectMetaValues(sourceHit);
          hit.peakAnnotations.reserve(sourceHit.getPeakAnnotations().size());
          for (const auto& sourceAnnotation : sourceHit.getPeakAnnotations())
          {
            hit.peakAnnotations.push_back({sourceAnnotation.mz, sourceAnnotation.intensity,
                                           sourceAnnotation.charge,
                                           QString::fromStdString(sourceAnnotation.annotation)});
          }
          identification.hits.push_back(std::move(hit));
        }
        identifications.push_back(std::move(identification));
      }
      return identifications;
    }
  }

  ViewerDocument::ViewerDocument(QObject* parent) : QObject(parent)
  {
  }

  ViewerDocument::LoadResult ViewerDocument::readMzML(const QString& path,
                                                       ProgressCallback progress,
                                                       CancellationCheck cancelled)
  {
    LoadResult result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();

    const QFileInfo file(result.sourcePath);
    if (!file.exists() || !file.isFile())
    {
      result.error = QStringLiteral("File does not exist: %1").arg(result.sourcePath);
      return result;
    }
    if (file.suffix().compare(QStringLiteral("mzML"), Qt::CaseInsensitive) != 0)
    {
      result.error = QStringLiteral("Unsupported file type '%1'. mzML is implemented in this milestone.")
                       .arg(file.suffix());
      return result;
    }

    try
    {
      auto experiment = std::make_shared<OpenMS::MSExperiment>();
      OpenMS::MzMLFile loader;
      if (progress || cancelled)
        loader.setLogger(new ViewerProgressLogger(progress, cancelled));
      loader.load(result.sourcePath.toStdString(), *experiment);
      if (cancelled && cancelled()) throw LoadCancelled();
      deriveLoadResult(result, std::move(experiment), cancelled);
    }
    catch (const LoadCancelled&)
    {
      result.error = QStringLiteral("Loading cancelled.");
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("OpenMS could not read the file: %1")
                       .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("OpenMS could not read the file (unknown error).");
    }
    return result;
  }

  ViewerDocument::LoadResult ViewerDocument::readThermoRaw(const QString& path,
                                                           ProgressCallback,
                                                           CancellationCheck cancelled)
  {
    LoadResult result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();
    const QFileInfo file(result.sourcePath);
    if (!file.exists() || !file.isFile())
    {
      result.error = QStringLiteral("File does not exist: %1").arg(result.sourcePath);
      return result;
    }
#ifdef WITH_THERMO_RAW
    try
    {
      auto experiment = std::make_shared<OpenMS::MSExperiment>();
      OpenMS::ThermoRawFile reader;
      reader.setLogType(OpenMS::ProgressLogger::NONE);
      // The .NET bridge reads in one blocking call (no progress/cancel hook); the
      // cancellation check still short-circuits the derivation phase below.
      reader.load(result.sourcePath.toStdString(), *experiment);
      if (cancelled && cancelled()) throw LoadCancelled();
      deriveLoadResult(result, std::move(experiment), cancelled);
    }
    catch (const LoadCancelled&)
    {
      result.error = QStringLiteral("Loading cancelled.");
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("Could not read the Thermo RAW file: %1")
                       .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("Could not read the Thermo RAW file (unknown error).");
    }
#else
    result.error = QStringLiteral(
      "This build of OpenMS was compiled without Thermo RAW support (WITH_THERMO_RAW).");
#endif
    return result;
  }

  ViewerDocument::LoadResult ViewerDocument::readBrukerTims(const QString& path,
                                                            ProgressCallback,
                                                            CancellationCheck cancelled)
  {
    LoadResult result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();
    const QFileInfo dataset(result.sourcePath);
    // A Bruker timsTOF .d dataset is a directory (analysis.tdf + binary blob).
    if (!dataset.exists() || !dataset.isDir())
    {
      result.error = QStringLiteral("Bruker .d dataset not found (expected a directory): %1")
                       .arg(result.sourcePath);
      return result;
    }
#ifdef WITH_OPENTIMS
    try
    {
      auto experiment = std::make_shared<OpenMS::MSExperiment>();
      OpenMS::BrukerTimsFile reader;
      reader.setLogType(OpenMS::ProgressLogger::NONE);
      reader.load(result.sourcePath.toStdString(), *experiment);
      if (cancelled && cancelled()) throw LoadCancelled();
      deriveLoadResult(result, std::move(experiment), cancelled);
    }
    catch (const LoadCancelled&)
    {
      result.error = QStringLiteral("Loading cancelled.");
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("Could not read the Bruker .d dataset: %1")
                       .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("Could not read the Bruker .d dataset (unknown error).");
    }
#else
    result.error = QStringLiteral(
      "This build of OpenMS was compiled without Bruker TDF support (WITH_OPENTIMS).");
#endif
    return result;
  }

  ViewerDocument::FeatureLoadResult ViewerDocument::readFeatureXML(const QString& path)
  {
    return readFeatures(path);
  }

  ViewerDocument::FeatureLoadResult ViewerDocument::readFeatures(const QString& path)
  {
    FeatureLoadResult result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();
    const QFileInfo file(result.sourcePath);
    if (!file.exists())
    {
      result.error = QStringLiteral("File does not exist: %1").arg(result.sourcePath);
      return result;
    }
    const auto info = FormatRegistry::detect(result.sourcePath);
    if (info.category != FormatRegistry::Category::Features || !info.supported)
    {
      result.error = QStringLiteral("Unsupported feature map: %1").arg(result.sourcePath);
      return result;
    }
    try
    {
      auto map = std::make_shared<OpenMS::FeatureMap>();
      OpenMS::FileHandler().loadFeatures(
        result.sourcePath.toStdString(), *map,
        FormatRegistry::allowedTypes(FormatRegistry::Category::Features));
      result.features = buildFeatureRecords(*map);
      result.featureMap = std::move(map);
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("OpenMS could not read the feature map: %1")
                       .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("OpenMS could not read the feature map (unknown error).");
    }
    return result;
  }

  ViewerDocument::IdentificationLoadResult ViewerDocument::readIdXML(const QString& path)
  {
    return readIdentifications(path);
  }

  ViewerDocument::IdentificationLoadResult ViewerDocument::readIdentifications(const QString& path)
  {
    IdentificationLoadResult result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();
    const QFileInfo file(result.sourcePath);
    if (!file.exists())
    {
      result.error = QStringLiteral("File does not exist: %1").arg(result.sourcePath);
      return result;
    }
    const auto info = FormatRegistry::detect(result.sourcePath);
    if (info.category != FormatRegistry::Category::Identifications || !info.supported)
    {
      result.error = QStringLiteral("Unsupported identification file: %1").arg(result.sourcePath);
      return result;
    }
    try
    {
      auto store = std::make_shared<IdentificationStore>();
      OpenMS::FileHandler().loadIdentifications(
        result.sourcePath.toStdString(), store->proteinIdentifications,
        store->peptideIdentifications,
        FormatRegistry::allowedTypes(FormatRegistry::Category::Identifications));
      result.identifications = buildIdentificationRecords(store->peptideIdentifications);
      result.store = std::move(store);
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("OpenMS could not read the identifications: %1")
                       .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("OpenMS could not read the identifications (unknown error).");
    }
    return result;
  }

  ViewerDocument::LoadResult ViewerDocument::readExperiment(const QString& path,
                                                            ProgressCallback,
                                                            CancellationCheck cancelled)
  {
    LoadResult result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();
    const QFileInfo file(result.sourcePath);
    if (!file.exists())
    {
      result.error = QStringLiteral("File does not exist: %1").arg(result.sourcePath);
      return result;
    }
    const auto info = FormatRegistry::detect(result.sourcePath);
    if (info.category != FormatRegistry::Category::Experiment || !info.supported)
    {
      result.error = QStringLiteral("Unsupported spectra file: %1").arg(result.sourcePath);
      return result;
    }
    try
    {
      auto experiment = std::make_shared<OpenMS::MSExperiment>();
      // No per-format progress hook here (unlike readMzML); large formats such as
      // sqMass load fully — MainWindow warns the user before starting.
      OpenMS::FileHandler().loadExperiment(
        result.sourcePath.toStdString(), *experiment,
        FormatRegistry::allowedTypes(FormatRegistry::Category::Experiment));
      if (cancelled && cancelled()) throw LoadCancelled();
      deriveLoadResult(result, std::move(experiment), cancelled);
    }
    catch (const LoadCancelled&)
    {
      result.error = QStringLiteral("Loading cancelled.");
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("OpenMS could not read the file: %1")
                       .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("OpenMS could not read the file (unknown error).");
    }
    return result;
  }

  bool ViewerDocument::adopt(LoadResult result)
  {
    if (!result.succeeded()) return false;
    experiment_ = std::move(result.experiment);
    sourcePath_ = std::move(result.sourcePath);
    bounds_ = result.bounds;
    tic_ = std::move(result.tic);
    ticLabel_ = std::move(result.ticLabel);
    statistics_ = result.statistics;
    spectra_ = std::move(result.spectra);
    chromatograms_ = std::move(result.chromatograms);
    ionMobilityFrames_ = std::move(result.ionMobilityFrames);
    faimsChannels_ = std::move(result.faimsChannels);
    faimsExperiments_ = std::move(result.faimsExperiments);
    relinkIdentifications();
    emit dataChanged();
    if (identificationStore_) emit identificationsChanged();
    return true;
  }

  bool ViewerDocument::adoptFeatures(FeatureLoadResult result)
  {
    if (!result.succeeded()) return false;
    featureMap_ = std::move(result.featureMap);
    featuresPath_ = std::move(result.sourcePath);
    features_ = std::move(result.features);
    emit featuresChanged();
    return true;
  }

  bool ViewerDocument::adoptIdentifications(IdentificationLoadResult result)
  {
    if (!result.succeeded()) return false;
    identificationStore_ = std::move(result.store);
    identificationsPath_ = std::move(result.sourcePath);
    identifications_ = std::move(result.identifications);
    relinkIdentifications();
    emit identificationsChanged();
    return true;
  }

  void ViewerDocument::clear()
  {
    experiment_.reset();
    sourcePath_.clear();
    bounds_ = {};
    tic_.clear();
    ticLabel_.clear();
    statistics_ = {};
    spectra_.clear();
    chromatograms_.clear();
    ionMobilityFrames_.clear();
    faimsChannels_.clear();
    faimsExperiments_.clear();
    featureMap_.reset();
    featuresPath_.clear();
    features_.clear();
    identificationStore_.reset();
    identificationsPath_.clear();
    identifications_.clear();
    identificationBySpectrum_.clear();
    identificationsBySpectrum_.clear();
    emit dataChanged();
    emit featuresChanged();
    emit identificationsChanged();
  }

  void ViewerDocument::clearFeatures()
  {
    featureMap_.reset();
    featuresPath_.clear();
    features_.clear();
    emit featuresChanged();
  }

  void ViewerDocument::clearIdentifications()
  {
    identificationStore_.reset();
    identificationsPath_.clear();
    identifications_.clear();
    identificationBySpectrum_.clear();
    identificationsBySpectrum_.clear();
    for (SpectrumRecord& spectrum : spectra_) spectrum.identificationIndex.reset();
    emit identificationsChanged();
  }

  bool ViewerDocument::isEmpty() const noexcept { return experiment_ == nullptr; }
  const QString& ViewerDocument::sourcePath() const noexcept { return sourcePath_; }
  const PlotRange& ViewerDocument::bounds() const noexcept { return bounds_; }
  const std::vector<TicPoint>& ViewerDocument::tic() const noexcept { return tic_; }
  const QString& ViewerDocument::ticLabel() const noexcept { return ticLabel_; }
  const DocumentStatistics& ViewerDocument::statistics() const noexcept { return statistics_; }
  const std::vector<SpectrumRecord>& ViewerDocument::spectra() const noexcept { return spectra_; }

  const SpectrumRecord* ViewerDocument::spectrumRecord(std::size_t index) const noexcept
  {
    if (index >= spectra_.size()) return nullptr;
    return &spectra_[index];
  }

  std::optional<std::size_t> ViewerDocument::spectrumIndexForNativeId(
    const QString& nativeId) const noexcept
  {
    if (nativeId.isEmpty()) return std::nullopt;
    // Linear scan: native-ID drill-down is a rare, user-initiated click, so a
    // per-call scan avoids maintaining a cache that would have to track adoption.
    for (const SpectrumRecord& spectrum : spectra_)
      if (spectrum.nativeId == nativeId) return spectrum.index;
    return std::nullopt;
  }

  const std::vector<ChromatogramRecord>& ViewerDocument::chromatograms() const noexcept
  {
    return chromatograms_;
  }

  bool ViewerDocument::hasChromatograms() const noexcept { return !chromatograms_.empty(); }

  bool ViewerDocument::hasIonMobility() const noexcept { return !ionMobilityFrames_.empty(); }

  const std::vector<IonMobilityFrameRecord>& ViewerDocument::ionMobilityFrames() const noexcept
  {
    return ionMobilityFrames_;
  }

  const IonMobilityFrameRecord* ViewerDocument::ionMobilityFrameForSpectrum(
    std::size_t spectrumIndex) const noexcept
  {
    const auto found = std::find_if(ionMobilityFrames_.begin(), ionMobilityFrames_.end(),
      [spectrumIndex](const IonMobilityFrameRecord& frame)
      {
        return frame.spectrumIndex == spectrumIndex;
      });
    return found == ionMobilityFrames_.end() ? nullptr : &*found;
  }

  bool ViewerDocument::hasFaims() const noexcept { return faimsChannels_.size() > 1; }

  const std::vector<FaimsChannelRecord>& ViewerDocument::faimsChannels() const noexcept
  {
    return faimsChannels_;
  }

  std::shared_ptr<const OpenMS::MSExperiment> ViewerDocument::faimsExperiment(
    std::size_t channelIndex) const noexcept
  {
    if (channelIndex >= faimsExperiments_.size()) return {};
    return faimsExperiments_[channelIndex];
  }
  bool ViewerDocument::hasFeatures() const noexcept { return featureMap_ != nullptr; }
  const QString& ViewerDocument::featuresPath() const noexcept { return featuresPath_; }
  const std::vector<FeatureRecord>& ViewerDocument::features() const noexcept { return features_; }

  const FeatureRecord* ViewerDocument::feature(std::size_t index) const noexcept
  {
    if (index >= features_.size()) return nullptr;
    return &features_[index];
  }

  bool ViewerDocument::hasIdentifications() const noexcept { return identificationStore_ != nullptr; }
  const QString& ViewerDocument::identificationsPath() const noexcept { return identificationsPath_; }
  const std::vector<IdentificationRecord>& ViewerDocument::identifications() const noexcept
  {
    return identifications_;
  }

  const IdentificationRecord* ViewerDocument::identification(std::size_t index) const noexcept
  {
    if (index >= identifications_.size()) return nullptr;
    return &identifications_[index];
  }

  const IdentificationRecord* ViewerDocument::identificationForSpectrum(std::size_t spectrumIndex) const noexcept
  {
    if (spectrumIndex >= identificationBySpectrum_.size()) return nullptr;
    const auto identificationIndex = identificationBySpectrum_[spectrumIndex];
    return identificationIndex ? identification(*identificationIndex) : nullptr;
  }

  const std::vector<std::size_t>& ViewerDocument::identificationsForSpectrum(
    std::size_t spectrumIndex) const noexcept
  {
    static const std::vector<std::size_t> empty;
    if (spectrumIndex >= identificationsBySpectrum_.size()) return empty;
    return identificationsBySpectrum_[spectrumIndex];
  }

  std::shared_ptr<const OpenMS::MSExperiment> ViewerDocument::experimentHandle() const noexcept
  {
    return experiment_;
  }

  const OpenMS::MSSpectrum* ViewerDocument::spectrum(std::size_t index) const noexcept
  {
    if (!experiment_ || index >= experiment_->size()) return nullptr;
    return &(*experiment_)[index];
  }

  std::optional<std::size_t> ViewerDocument::nearestSpectrumIndex(
    double rt, unsigned int msLevel) const noexcept
  {
    if (!experiment_ || experiment_->empty()) return std::nullopt;

    std::optional<std::size_t> best;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < experiment_->size(); ++index)
    {
      const auto& spectrum = (*experiment_)[index];
      if (msLevel != 0 && spectrum.getMSLevel() != msLevel) continue;
      const double distance = std::abs(spectrum.getRT() - rt);
      if (distance < bestDistance)
      {
        bestDistance = distance;
        best = index;
      }
      // Spectra are RT-sorted, so later points cannot improve after passing rt.
      if (best && spectrum.getRT() > rt && distance > bestDistance) break;
    }
    return best;
  }

  std::optional<std::size_t> ViewerDocument::adjacentSpectrumIndex(
    std::size_t current, int direction, unsigned int msLevel) const noexcept
  {
    if (!experiment_ || experiment_->empty() || direction == 0) return std::nullopt;
    if (current >= experiment_->size()) current = direction > 0 ? 0 : experiment_->size() - 1;

    std::ptrdiff_t index = static_cast<std::ptrdiff_t>(current) + (direction > 0 ? 1 : -1);
    const auto end = static_cast<std::ptrdiff_t>(experiment_->size());
    while (index >= 0 && index < end)
    {
      if (msLevel == 0 || (*experiment_)[static_cast<std::size_t>(index)].getMSLevel() == msLevel)
      {
        return static_cast<std::size_t>(index);
      }
      index += direction > 0 ? 1 : -1;
    }
    return std::nullopt;
  }

  std::optional<std::size_t> ViewerDocument::edgeSpectrumIndex(
    bool last, unsigned int msLevel) const noexcept
  {
    if (!experiment_ || experiment_->empty()) return std::nullopt;
    if (!last)
    {
      for (std::size_t index = 0; index < experiment_->size(); ++index)
      {
        if (msLevel == 0 || (*experiment_)[index].getMSLevel() == msLevel) return index;
      }
    }
    else
    {
      for (std::size_t index = experiment_->size(); index-- > 0;)
      {
        if (msLevel == 0 || (*experiment_)[index].getMSLevel() == msLevel) return index;
      }
    }
    return std::nullopt;
  }

  ViewerDocument::IdentificationLinks ViewerDocument::linkIdentifications(
    const OpenMS::MSExperiment& experiment,
    std::vector<IdentificationRecord>& identifications)
  {
    constexpr double kRtTolerance = 5.0;   // seconds
    constexpr double kMzTolerance = 0.5;   // Da (precursor)

    const std::size_t spectrumCount = experiment.size();
    IdentificationLinks links;
    links.bestBySpectrum.assign(spectrumCount, std::nullopt);
    links.allBySpectrum.assign(spectrumCount, {});

    for (IdentificationRecord& identification : identifications)
    {
      identification.spectrumIndex.reset();
      identification.linkMode = LinkMode::None;
      identification.linkRtError = 0.0;
      identification.linkMzError = 0.0;
    }
    if (spectrumCount == 0 || identifications.empty()) return links;

    // One pass over the experiment builds both indices: a native-ID → spectrum
    // map for exact spectrum_reference links, and an RT-sorted list of MS2
    // candidates for the fallback window search. Empty native IDs are skipped;
    // on the rare duplicate the lowest spectrum position wins (emplace keeps it).
    std::unordered_map<std::string, std::size_t> byNativeId;
    byNativeId.reserve(spectrumCount * 2);
    struct Candidate { double rt; double precursorMz; std::size_t index; };
    std::vector<Candidate> candidates;
    candidates.reserve(spectrumCount);
    for (std::size_t s = 0; s < spectrumCount; ++s)
    {
      const auto& spectrum = experiment[s];
      const std::string& native = spectrum.getNativeID();
      if (!native.empty()) byNativeId.emplace(native, s);
      if (spectrum.getMSLevel() > 1 && !spectrum.getPrecursors().empty())
      {
        const double rt = spectrum.getRT();
        const double precursorMz = spectrum.getPrecursors().front().getMZ();
        // Non-finite RT/m-z would violate std::sort's strict-weak-ordering and
        // poison the window search, so such spectra are excluded from the fallback.
        if (std::isfinite(rt) && std::isfinite(precursorMz))
          candidates.push_back({rt, precursorMz, s});
      }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b)
              { return a.rt != b.rt ? a.rt < b.rt : a.index < b.index; });

    // Preferred-id contest per spectrum: a native-ID link always outranks an
    // RT/m-z link; within a mode, lowest RT error then m-z error then id position
    // (making the outcome deterministic and load-order independent).
    struct Best { LinkMode mode; double rtError; double mzError; std::size_t idPos; };
    const auto beats = [](const Best& contender, const Best& incumbent)
    {
      if (contender.mode == LinkMode::NativeId && incumbent.mode != LinkMode::NativeId) return true;
      if (contender.mode != LinkMode::NativeId && incumbent.mode == LinkMode::NativeId) return false;
      if (contender.rtError != incumbent.rtError) return contender.rtError < incumbent.rtError;
      if (contender.mzError != incumbent.mzError) return contender.mzError < incumbent.mzError;
      return contender.idPos < incumbent.idPos;
    };
    std::vector<std::optional<Best>> best(spectrumCount);

    for (std::size_t idPos = 0; idPos < identifications.size(); ++idPos)
    {
      IdentificationRecord& identification = identifications[idPos];
      if (identification.hits.empty()) continue;

      std::optional<std::size_t> target;
      LinkMode mode = LinkMode::None;
      double rtError = 0.0;         // displayed (0 when the id carries no coordinate)
      double mzError = 0.0;
      // Contest keys separate an *unknown* coordinate (ranks last, +inf) from a
      // genuine 0.0 error, so a coordinate-bearing native link is preferred over a
      // scan-reference-only one that maps to the same spectrum.
      double contestRt = std::numeric_limits<double>::infinity();
      double contestMz = std::numeric_limits<double>::infinity();

      // 1) Exact native-ID link when the id carries a resolvable spectrum_reference.
      if (!identification.spectrumReference.isEmpty())
      {
        const auto it = byNativeId.find(identification.spectrumReference.toStdString());
        if (it != byNativeId.end())
        {
          target = it->second;
          mode = LinkMode::NativeId;
          const auto& spectrum = experiment[*target];
          // Errors are informational for native-ID links (an idparquet id may have
          // no RT/m-z at all, leaving the displayed error at 0 and the contest key
          // at +inf so it ranks behind a link with real coordinates).
          if (std::isfinite(identification.rt))
            contestRt = rtError = std::abs(spectrum.getRT() - identification.rt);
          if (std::isfinite(identification.mz) && spectrum.getMSLevel() > 1
              && !spectrum.getPrecursors().empty())
            contestMz = mzError = std::abs(spectrum.getPrecursors().front().getMZ() - identification.mz);
        }
      }

      // 2) RT/m-z window fallback: binary-search the ±tolerance RT band and keep
      //    the closest precursor within the m-z tolerance. Ties fall through to the
      //    lowest spectrum index so the result matches the former full-scan order.
      //    The binary-search bounds are a conservative superset (widened by a slack
      //    that dwarfs any FP rounding); the authoritative filter is the exact
      //    |dRt| <= tol / |dMz| <= tol test below, bit-identical to the old scan.
      if (!target && std::isfinite(identification.rt) && std::isfinite(identification.mz))
      {
        constexpr double kRtSlack = 1e-6;  // seconds; only widens the search range
        const double lo = identification.rt - kRtTolerance - kRtSlack;
        const double hi = identification.rt + kRtTolerance + kRtSlack;
        const auto begin = std::lower_bound(candidates.begin(), candidates.end(), lo,
          [](const Candidate& candidate, double value) { return candidate.rt < value; });
        double bestRt = std::numeric_limits<double>::infinity();
        double bestMz = std::numeric_limits<double>::infinity();
        for (auto it = begin; it != candidates.end() && it->rt <= hi; ++it)
        {
          const double dRt = std::abs(it->rt - identification.rt);
          if (dRt > kRtTolerance) continue;
          const double dMz = std::abs(it->precursorMz - identification.mz);
          if (dMz > kMzTolerance) continue;
          if (!target || dRt < bestRt
              || (dRt == bestRt && (dMz < bestMz
                  || (dMz == bestMz && it->index < *target))))
          {
            target = it->index;
            bestRt = dRt;
            bestMz = dMz;
          }
        }
        if (target) { mode = LinkMode::RtMz; contestRt = rtError = bestRt; contestMz = mzError = bestMz; }
      }

      if (!target) continue;
      identification.spectrumIndex = target;
      identification.linkMode = mode;
      identification.linkRtError = rtError;
      identification.linkMzError = mzError;

      links.allBySpectrum[*target].push_back(idPos);
      const Best contender{mode, contestRt, contestMz, idPos};
      auto& slot = best[*target];
      if (!slot || beats(contender, *slot)) slot = contender;
    }

    for (std::size_t s = 0; s < spectrumCount; ++s)
      if (best[s]) links.bestBySpectrum[s] = best[s]->idPos;
    return links;
  }

  void ViewerDocument::relinkIdentifications()
  {
    for (SpectrumRecord& spectrum : spectra_) spectrum.identificationIndex.reset();
    identificationBySpectrum_.clear();
    identificationsBySpectrum_.clear();
    if (!experiment_ || identifications_.empty())
    {
      for (IdentificationRecord& identification : identifications_)
      {
        identification.spectrumIndex.reset();
        identification.linkMode = LinkMode::None;
        identification.linkRtError = 0.0;
        identification.linkMzError = 0.0;
      }
      return;
    }

    IdentificationLinks links = linkIdentifications(*experiment_, identifications_);
    identificationBySpectrum_ = std::move(links.bestBySpectrum);
    identificationsBySpectrum_ = std::move(links.allBySpectrum);
    for (std::size_t spectrumIndex = 0;
         spectrumIndex < identificationBySpectrum_.size() && spectrumIndex < spectra_.size();
         ++spectrumIndex)
      spectra_[spectrumIndex].identificationIndex = identificationBySpectrum_[spectrumIndex];
  }
}
