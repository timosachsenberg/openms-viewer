#pragma once

#include "plot/PlotRange.h"
#include "model/IdentificationData.h"
#include "model/RunData.h"

#include <OpenMS/KERNEL/MSExperiment.h>
#include <OpenMS/KERNEL/FeatureMap.h>
#include <OpenMS/METADATA/PeptideIdentificationList.h>
#include <OpenMS/METADATA/ProteinIdentification.h>

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace OpenMSViewer
{
  struct TicPoint
  {
    double rt{0.0};
    double intensity{0.0};
    std::size_t spectrumIndex{0};
  };

  struct DocumentStatistics
  {
    std::size_t spectrumCount{0};
    std::size_t ms1SpectrumCount{0};
    std::uint64_t peakCount{0};
    std::uint64_t ms1PeakCount{0};
    unsigned int minimumMsLevel{0};
    unsigned int maximumMsLevel{0};
  };

  struct FeaturePoint
  {
    double rt{0.0};
    double mz{0.0};
  };

  struct FeatureRecord
  {
    std::size_t index{0};
    double rt{0.0};
    double mz{0.0};
    double intensity{0.0};
    int charge{0};
    double quality{0.0};
    PlotRange bounds;
    std::vector<std::vector<FeaturePoint>> hulls;

    [[nodiscard]] double rtWidth() const noexcept { return bounds.rtSpan(); }
    [[nodiscard]] double mzWidth() const noexcept { return bounds.mzSpan(); }
  };

  class ViewerDocument final : public QObject
  {
    Q_OBJECT

  public:
    using ProgressCallback = std::function<void(const QString& label, int percent)>;
    using CancellationCheck = std::function<bool()>;
    struct LoadResult
    {
      std::shared_ptr<OpenMS::MSExperiment> experiment;
      QString sourcePath;
      QString error;
      PlotRange bounds;
      std::vector<TicPoint> tic;
      QString ticLabel;
      DocumentStatistics statistics;
      std::vector<SpectrumRecord> spectra;
      std::vector<ChromatogramRecord> chromatograms;
      std::vector<IonMobilityFrameRecord> ionMobilityFrames;
      std::vector<FaimsChannelRecord> faimsChannels;
      std::vector<std::shared_ptr<OpenMS::MSExperiment>> faimsExperiments;

      [[nodiscard]] bool succeeded() const noexcept
      {
        return experiment != nullptr && error.isEmpty();
      }
    };

    struct FeatureLoadResult
    {
      std::shared_ptr<OpenMS::FeatureMap> featureMap;
      QString sourcePath;
      QString error;
      std::vector<FeatureRecord> features;

      [[nodiscard]] bool succeeded() const noexcept
      {
        return featureMap != nullptr && error.isEmpty();
      }
    };

    struct IdentificationStore
    {
      std::vector<OpenMS::ProteinIdentification> proteinIdentifications;
      OpenMS::PeptideIdentificationList peptideIdentifications;
    };

    struct IdentificationLoadResult
    {
      std::shared_ptr<IdentificationStore> store;
      QString sourcePath;
      QString error;
      std::vector<IdentificationRecord> identifications;

      [[nodiscard]] bool succeeded() const noexcept
      {
        return store != nullptr && error.isEmpty();
      }
    };

    explicit ViewerDocument(QObject* parent = nullptr);

    [[nodiscard]] static LoadResult readMzML(const QString& path,
                                             ProgressCallback progress = {},
                                             CancellationCheck cancelled = {});
    // Vendor formats via the OpenMS reader backends: Thermo .raw (openms-thermo
    // bridge) and Bruker timsTOF .d/TDF (opentims). Both yield the same
    // LoadResult as readMzML; each returns an error result if the linked OpenMS
    // was built without the corresponding backend.
    [[nodiscard]] static LoadResult readThermoRaw(const QString& path,
                                                  ProgressCallback progress = {},
                                                  CancellationCheck cancelled = {});
    [[nodiscard]] static LoadResult readBrukerTims(const QString& path,
                                                   ProgressCallback progress = {},
                                                   CancellationCheck cancelled = {});
    [[nodiscard]] static FeatureLoadResult readFeatureXML(const QString& path);
    [[nodiscard]] static IdentificationLoadResult readIdXML(const QString& path);
    bool adopt(LoadResult result);
    bool adoptFeatures(FeatureLoadResult result);
    bool adoptIdentifications(IdentificationLoadResult result);
    void clear();
    void clearFeatures();
    void clearIdentifications();

    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] const QString& sourcePath() const noexcept;
    [[nodiscard]] const PlotRange& bounds() const noexcept;
    [[nodiscard]] const std::vector<TicPoint>& tic() const noexcept;
    [[nodiscard]] const QString& ticLabel() const noexcept;
    [[nodiscard]] const DocumentStatistics& statistics() const noexcept;
    [[nodiscard]] const std::vector<SpectrumRecord>& spectra() const noexcept;
    [[nodiscard]] const SpectrumRecord* spectrumRecord(std::size_t index) const noexcept;
    [[nodiscard]] const std::vector<ChromatogramRecord>& chromatograms() const noexcept;
    [[nodiscard]] bool hasChromatograms() const noexcept;
    [[nodiscard]] bool hasIonMobility() const noexcept;
    [[nodiscard]] const std::vector<IonMobilityFrameRecord>& ionMobilityFrames() const noexcept;
    [[nodiscard]] const IonMobilityFrameRecord* ionMobilityFrameForSpectrum(
      std::size_t spectrumIndex) const noexcept;
    [[nodiscard]] bool hasFaims() const noexcept;
    [[nodiscard]] const std::vector<FaimsChannelRecord>& faimsChannels() const noexcept;
    [[nodiscard]] std::shared_ptr<const OpenMS::MSExperiment> faimsExperiment(
      std::size_t channelIndex) const noexcept;
    [[nodiscard]] bool hasFeatures() const noexcept;
    [[nodiscard]] const QString& featuresPath() const noexcept;
    [[nodiscard]] const std::vector<FeatureRecord>& features() const noexcept;
    [[nodiscard]] const FeatureRecord* feature(std::size_t index) const noexcept;
    [[nodiscard]] bool hasIdentifications() const noexcept;
    [[nodiscard]] const QString& identificationsPath() const noexcept;
    [[nodiscard]] const std::vector<IdentificationRecord>& identifications() const noexcept;
    [[nodiscard]] const IdentificationRecord* identification(std::size_t index) const noexcept;
    [[nodiscard]] const IdentificationRecord* identificationForSpectrum(std::size_t spectrumIndex) const noexcept;
    [[nodiscard]] const std::vector<std::size_t>& identificationsForSpectrum(
      std::size_t spectrumIndex) const noexcept;
    [[nodiscard]] std::shared_ptr<const OpenMS::MSExperiment> experimentHandle() const noexcept;
    [[nodiscard]] const OpenMS::MSSpectrum* spectrum(std::size_t index) const noexcept;

    [[nodiscard]] std::optional<std::size_t> nearestSpectrumIndex(
      double rt, unsigned int msLevel = 0) const noexcept;
    [[nodiscard]] std::optional<std::size_t> adjacentSpectrumIndex(
      std::size_t current, int direction, unsigned int msLevel = 0) const noexcept;
    [[nodiscard]] std::optional<std::size_t> edgeSpectrumIndex(
      bool last, unsigned int msLevel = 0) const noexcept;

  signals:
    void dataChanged();
    void featuresChanged();
    void identificationsChanged();

  private:
    std::shared_ptr<OpenMS::MSExperiment> experiment_;
    QString sourcePath_;
    PlotRange bounds_;
    std::vector<TicPoint> tic_;
    QString ticLabel_;
    DocumentStatistics statistics_;
    std::vector<SpectrumRecord> spectra_;
    std::vector<ChromatogramRecord> chromatograms_;
    std::vector<IonMobilityFrameRecord> ionMobilityFrames_;
    std::vector<FaimsChannelRecord> faimsChannels_;
    std::vector<std::shared_ptr<OpenMS::MSExperiment>> faimsExperiments_;
    std::shared_ptr<OpenMS::FeatureMap> featureMap_;
    QString featuresPath_;
    std::vector<FeatureRecord> features_;
    std::shared_ptr<IdentificationStore> identificationStore_;
    QString identificationsPath_;
    std::vector<IdentificationRecord> identifications_;
    std::vector<std::optional<std::size_t>> identificationBySpectrum_;
    std::vector<std::vector<std::size_t>> identificationsBySpectrum_;

    void relinkIdentifications();
  };
}
