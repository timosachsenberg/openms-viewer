#pragma once

#include <OpenMS/IMAGING/IonImage.h>
#include <OpenMS/KERNEL/MSSpectrum.h>
#include <OpenMS/KERNEL/OnDiscImzMLExperiment.h>

#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

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
    explicit ImagingStore(const QString& path);

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] std::size_t spectrumCount() const noexcept;
    [[nodiscard]] OpenMS::MSSpectrum spectrum(std::size_t index) const;
    [[nodiscard]] OpenMS::IonImage extractIonImage(double mz, double tolerancePpm) const;
    [[nodiscard]] AggregateSpectrum aggregateSpectrum(double mzMin, double mzMax, int bins) const;

    OpenMS::OnDiscImzMLExperiment& experiment() noexcept;
    const OpenMS::OnDiscImzMLExperiment& experiment() const noexcept;

  private:
    mutable std::mutex mutex_;
    OpenMS::OnDiscImzMLExperiment experiment_;
  };

  struct ImagingLoadResult
  {
    std::shared_ptr<ImagingStore> store;
    ImagingSummary summary;
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
  };
}
