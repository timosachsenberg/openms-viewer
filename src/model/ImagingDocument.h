#pragma once

#include <OpenMS/IMAGING/IonImage.h>
#include <OpenMS/KERNEL/MSSpectrum.h>
#include <OpenMS/KERNEL/OnDiscImzMLExperiment.h>

#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

class QTemporaryDir;

namespace OpenMSViewer
{
  struct ImagingPixelRecord
  {
    std::uint32_t x{0};
    std::uint32_t y{0};
    std::size_t spectrumIndex{0};
    double tic{0.0};
  };

  // Aggregate (whole-image) spectrum: for each m/z bin, the mean intensity across
  // all pixels and the max ("skyline") intensity in any pixel. Only occupied bins
  // are kept. Scanning this and clicking peaks is the core MSI discovery workflow.
  struct AggregateSpectrum
  {
    std::vector<double> mz;
    std::vector<double> mean;
    std::vector<double> maxIntensity;
  };

  struct ImagingSummary
  {
    QString sourcePath;
    std::uint32_t width{0};
    std::uint32_t height{0};
    double pixelSizeX{0.0};
    double pixelSizeY{0.0};
    QString pixelSizeUnit;
    QString imagingMode;
    double mzMin{0.0};
    double mzMax{0.0};
    std::size_t peakCount{0};
    std::vector<ImagingPixelRecord> pixels;
  };

  class ImagingStore final
  {
  public:
    using CancellationCheck = std::function<bool()>;

    static constexpr double kDefaultAggregateBinPpm = 5.0;

    explicit ImagingStore(const QString& path);

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] std::size_t spectrumCount() const noexcept;
    [[nodiscard]] OpenMS::MSSpectrum spectrum(std::size_t index) const;
    [[nodiscard]] OpenMS::IonImage extractIonImage(double mz, double tolerancePpm) const;
    // Log-spaced aggregate over every geometry pixel (mean + skyline), matching
    // pyopenms-viewer compute_aggregate. `binPpm` is the relative bin width
    // (default 5 ppm). Mean = sum / (spectra that hit the bin); skyline =
    // max per-peak intensity in the bin; reported m/z is the bin centre.
    // `cancelled` is polled between pixels; on cancel returns an empty spectrum.
    [[nodiscard]] AggregateSpectrum aggregateSpectrum(
      double mzMin, double mzMax, double binPpm = kDefaultAggregateBinPpm,
      CancellationCheck cancelled = {}) const;

    OpenMS::OnDiscImzMLExperiment& experiment() noexcept;
    const OpenMS::OnDiscImzMLExperiment& experiment() const noexcept;

    // Bind the lifetime of a temporary directory (holding a converted imzML/ibd)
    // to this store, so a Bruker-MALDI-sourced image keeps its backing file for
    // as long as the store is alive.
    void retainTempDir(std::shared_ptr<QTemporaryDir> dir);

  private:
    mutable std::mutex mutex_;
    // Declared before experiment_ so it is destroyed *after* it: experiment_ closes
    // its imzML/ibd file handles first, then the temp dir removes those files (which
    // matters on platforms that refuse to delete an open file).
    std::shared_ptr<QTemporaryDir> tempDir_;
    OpenMS::OnDiscImzMLExperiment experiment_;
  };

  struct ImagingLoadResult
  {
    std::shared_ptr<ImagingStore> store;
    ImagingSummary summary;
    // Built during the same disk pass as TIC / m/z range (default bin width).
    AggregateSpectrum aggregate;
    double aggregateBinPpm{ImagingStore::kDefaultAggregateBinPpm};
    QString error;

    [[nodiscard]] bool succeeded() const noexcept
    {
      return store != nullptr && error.isEmpty();
    }
  };

  class ImagingDocument
  {
  public:
    [[nodiscard]] static ImagingLoadResult readImzML(const QString& path);
    // Loads a Bruker timsTOF MALDI imaging .d directory (.tdf) by delegating to
    // OpenMS BrukerTimsImagingFile, converting the result to a temporary imzML,
    // and reusing the imzML pipeline. Returns a clear error for non-imaging .d,
    // single-quad .tsf datasets, or builds without opentims support.
    [[nodiscard]] static ImagingLoadResult readBrukerMaldi(const QString& path);
  };
}
