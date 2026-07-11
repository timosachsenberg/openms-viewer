#include "model/ViewerDocument.h"

#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/FORMAT/FeatureXMLFile.h>
#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/METADATA/MetaInfoInterface.h>
#include <OpenMS/IONMOBILITY/IMTypes.h>
#include <OpenMS/CONCEPT/ProgressLogger.h>

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
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
      if (experiment->empty() && experiment->getChromatograms().empty())
      {
        result.error = QStringLiteral("The mzML file contains no spectra or chromatograms.");
        return result;
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
          record.precursorMz = spectrum.getPrecursors().front().getMZ();
          record.precursorCharge = spectrum.getPrecursors().front().getCharge();
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
          result.error = QStringLiteral("The mzML file contains no displayable peaks.");
          return result;
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
      return result;
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

  ViewerDocument::FeatureLoadResult ViewerDocument::readFeatureXML(const QString& path)
  {
    FeatureLoadResult result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();

    const QFileInfo file(result.sourcePath);
    if (!file.exists() || !file.isFile())
    {
      result.error = QStringLiteral("File does not exist: %1").arg(result.sourcePath);
      return result;
    }
    if (file.suffix().compare(QStringLiteral("featureXML"), Qt::CaseInsensitive) != 0)
    {
      result.error = QStringLiteral("Unsupported feature file type '%1'.").arg(file.suffix());
      return result;
    }

    try
    {
      auto map = std::make_shared<OpenMS::FeatureMap>();
      OpenMS::FeatureXMLFile().load(result.sourcePath.toStdString(), *map);
      result.features.reserve(map->size());

      for (std::size_t index = 0; index < map->size(); ++index)
      {
        const auto& source = (*map)[index];
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
        result.features.push_back(std::move(feature));
      }

      result.featureMap = std::move(map);
      return result;
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("OpenMS could not read the FeatureXML file: %1")
                       .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("OpenMS could not read the FeatureXML file (unknown error).");
    }
    return result;
  }

  ViewerDocument::IdentificationLoadResult ViewerDocument::readIdXML(const QString& path)
  {
    IdentificationLoadResult result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();

    const QFileInfo file(result.sourcePath);
    if (!file.exists() || !file.isFile())
    {
      result.error = QStringLiteral("File does not exist: %1").arg(result.sourcePath);
      return result;
    }
    if (file.suffix().compare(QStringLiteral("idXML"), Qt::CaseInsensitive) != 0)
    {
      result.error = QStringLiteral("Unsupported identification file type '%1'.").arg(file.suffix());
      return result;
    }

    try
    {
      auto store = std::make_shared<IdentificationStore>();
      OpenMS::IdXMLFile().load(result.sourcePath.toStdString(),
                               store->proteinIdentifications,
                               store->peptideIdentifications);
      result.identifications.reserve(store->peptideIdentifications.size());

      for (std::size_t index = 0; index < store->peptideIdentifications.size(); ++index)
      {
        const auto& source = store->peptideIdentifications[index];
        IdentificationRecord identification;
        identification.index = index;
        identification.rt = source.getRT();
        identification.mz = source.getMZ();
        identification.scoreType = QString::fromStdString(source.getScoreType());
        identification.higherScoreBetter = source.isHigherScoreBetter();
        identification.identifier = QString::fromStdString(source.getIdentifier());
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
        result.identifications.push_back(std::move(identification));
      }
      result.store = std::move(store);
      return result;
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("OpenMS could not read the idXML file: %1")
                       .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("OpenMS could not read the idXML file (unknown error).");
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

  void ViewerDocument::relinkIdentifications()
  {
    for (SpectrumRecord& spectrum : spectra_) spectrum.identificationIndex.reset();
    for (IdentificationRecord& identification : identifications_)
    {
      identification.spectrumIndex.reset();
      identification.linkRtError = 0.0;
      identification.linkMzError = 0.0;
    }
    identificationBySpectrum_.clear();
    identificationsBySpectrum_.clear();
    if (!experiment_ || identifications_.empty()) return;

    identificationBySpectrum_.resize(experiment_->size());
    identificationsBySpectrum_.resize(experiment_->size());
    std::vector<double> assignedRtError(experiment_->size(), std::numeric_limits<double>::infinity());
    std::vector<double> assignedMzError(experiment_->size(), std::numeric_limits<double>::infinity());

    for (std::size_t identificationIndex = 0;
         identificationIndex < identifications_.size(); ++identificationIndex)
    {
      IdentificationRecord& identification = identifications_[identificationIndex];
      if (identification.hits.empty() || !std::isfinite(identification.rt)
          || !std::isfinite(identification.mz)) continue;

      std::optional<std::size_t> bestSpectrum;
      double bestRtError = std::numeric_limits<double>::infinity();
      double bestMzError = std::numeric_limits<double>::infinity();
      for (std::size_t spectrumIndex = 0; spectrumIndex < experiment_->size(); ++spectrumIndex)
      {
        const auto& spectrum = (*experiment_)[spectrumIndex];
        if (spectrum.getMSLevel() <= 1 || spectrum.getPrecursors().empty()) continue;
        const double rtError = std::abs(spectrum.getRT() - identification.rt);
        const double mzError = std::abs(spectrum.getPrecursors().front().getMZ() - identification.mz);
        if (rtError > 5.0 || mzError > 0.5) continue;
        if (rtError < bestRtError || (rtError == bestRtError && mzError < bestMzError))
        {
          bestSpectrum = spectrumIndex;
          bestRtError = rtError;
          bestMzError = mzError;
        }
      }

      if (!bestSpectrum) continue;
      const std::size_t spectrumIndex = *bestSpectrum;
      identification.spectrumIndex = spectrumIndex;
      identification.linkRtError = bestRtError;
      identification.linkMzError = bestMzError;
      identificationsBySpectrum_[spectrumIndex].push_back(identificationIndex);

      const bool replacesExisting = bestRtError < assignedRtError[spectrumIndex]
        || (bestRtError == assignedRtError[spectrumIndex] && bestMzError < assignedMzError[spectrumIndex]);
      if (!replacesExisting) continue;

      identificationBySpectrum_[spectrumIndex] = identificationIndex;
      if (spectrumIndex < spectra_.size())
        spectra_[spectrumIndex].identificationIndex = identificationIndex;
      assignedRtError[spectrumIndex] = bestRtError;
      assignedMzError[spectrumIndex] = bestMzError;
    }
  }
}
