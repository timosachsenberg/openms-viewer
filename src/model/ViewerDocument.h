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
    // Category loaders that route through OpenMS::FileHandler with an explicit
    // allowlist, so one path serves every format of a kind (featureXML +
    // featureparquet; idXML + mzIdentML + idparquet; mzML + mzXML + mzData +
    // sqMass). readFeatureXML/readIdXML delegate here.
    [[nodiscard]] static LoadResult readExperiment(const QString& path,
                                                   ProgressCallback progress = {},
                                                   CancellationCheck cancelled = {});
    // A standalone OpenSWATH .xic (CHROMPARQUET) chromatogram store: every stored
    // transition XIC becomes an MSChromatogram, yielding the same chromatogram-only
    // LoadResult the app already renders for chromatogram-only mzML. Without the
    // paired .osw there is no peak-group context — these are flat per-transition
    // traces (issue: standalone .xic opening).
    [[nodiscard]] static LoadResult readXic(const QString& path,
                                            CancellationCheck cancelled = {});
    [[nodiscard]] static FeatureLoadResult readFeatures(const QString& path);
    [[nodiscard]] static IdentificationLoadResult readIdentifications(const QString& path);
    bool adopt(LoadResult result);
    bool adoptFeatures(FeatureLoadResult result);
    bool adoptIdentifications(IdentificationLoadResult result);
    void clear();
    void clearFeatures();
    void clearIdentifications();

    // Write the currently loaded feature map / identifications back to a native
    // OpenMS format (featureXML / idXML). Returns false with @p error set on failure
    // or when there is nothing to save. The in-memory maps carry any edits.
    [[nodiscard]] bool saveFeatures(const QString& path, QString& error) const;
    [[nodiscard]] bool saveIdentifications(const QString& path, QString& error) const;

    // Manual feature-map editing (TOPPView EDIT mode). Each mutates the in-memory
    // FeatureMap, rebuilds the derived records and emits featuresChanged. addFeature
    // creates the map if none is loaded, so features can be drawn onto a bare run.
    std::size_t addFeature(double rt, double mz, double intensity, int charge);
    std::size_t insertFeature(std::size_t index, const OpenMS::Feature& feature);
    void replaceFeature(std::size_t index, const OpenMS::Feature& feature);
    void updateFeature(std::size_t index, double rt, double mz, double intensity, int charge);
    void removeFeature(std::size_t index);
    [[nodiscard]] std::optional<OpenMS::Feature> featureCopy(std::size_t index) const;

    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] const QString& sourcePath() const noexcept;
    [[nodiscard]] const PlotRange& bounds() const noexcept;
    [[nodiscard]] const std::vector<TicPoint>& tic() const noexcept;
    [[nodiscard]] const QString& ticLabel() const noexcept;
    [[nodiscard]] const DocumentStatistics& statistics() const noexcept;
    [[nodiscard]] const std::vector<SpectrumRecord>& spectra() const noexcept;
    [[nodiscard]] const SpectrumRecord* spectrumRecord(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::size_t> spectrumIndexForNativeId(
      const QString& nativeId) const noexcept;
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

    // Per-spectrum link tables produced by linkIdentifications(). Values are
    // positions into the identifications vector, not IdentificationRecord::index.
    struct IdentificationLinks
    {
      std::vector<std::optional<std::size_t>> bestBySpectrum;  // preferred id per spectrum
      std::vector<std::vector<std::size_t>> allBySpectrum;     // every id linked to a spectrum
    };

    // Pure, reentrant identification↔spectrum linker. Mutates each record's
    // spectrumIndex/linkMode/linkRtError/linkMzError in place and returns the
    // per-spectrum tables. spectrum_reference (native ID) matches are exact and
    // authoritative; the rest fall back to an indexed ±5 s / ±0.5 Da window search.
    // O(N + M log M + N log M) rather than the former O(N·M) full scan.
    [[nodiscard]] static IdentificationLinks linkIdentifications(
      const OpenMS::MSExperiment& experiment,
      std::vector<IdentificationRecord>& identifications);

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
