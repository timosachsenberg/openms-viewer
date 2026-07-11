#include "model/ImagingDocument.h"

#include <OpenMS/IMAGING/MSImagingGeometry.h>

#include <QFileInfo>
#include <QDir>

#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenMSViewer
{
  ImagingStore::ImagingStore(const QString& path)
  {
    experiment_.open(path.toStdString());
  }

  bool ImagingStore::isOpen() const noexcept { return experiment_.isOpen(); }
  std::size_t ImagingStore::spectrumCount() const noexcept { return experiment_.getNrSpectra(); }

  OpenMS::MSSpectrum ImagingStore::spectrum(std::size_t index) const
  {
    const std::scoped_lock lock(mutex_);
    return experiment_.getSpectrum(index);
  }

  OpenMS::IonImage ImagingStore::extractIonImage(double mz, double tolerancePpm) const
  {
    const std::scoped_lock lock(mutex_);
    return experiment_.extractIonImage(mz, tolerancePpm);
  }

  AggregateSpectrum ImagingStore::aggregateSpectrum(double mzMin, double mzMax, int bins) const
  {
    AggregateSpectrum result;
    if (!(mzMax > mzMin) || bins < 1 || !std::isfinite(mzMin) || !std::isfinite(mzMax)) return result;
    const std::size_t binCount = static_cast<std::size_t>(bins);
    std::vector<double> sum(binCount, 0.0);          // total intensity across all pixels
    std::vector<double> maximum(binCount, 0.0);      // skyline: max per-pixel bin total
    std::vector<double> weightedMz(binCount, 0.0);   // Σ m/z·intensity, for a representative m/z
    std::vector<double> pixelBin(binCount, 0.0);     // reused per pixel
    std::vector<std::size_t> touched;
    const double scale = bins / (mzMax - mzMin);
    // spectrum(i) locks per call, so a concurrent ion-image extraction can still
    // interleave rather than being blocked for the whole (potentially long) scan.
    const std::size_t pixelCount = spectrumCount();
    for (std::size_t index = 0; index < pixelCount; ++index)
    {
      const OpenMS::MSSpectrum pixel = spectrum(index);
      touched.clear();
      for (const auto& peak : pixel)
      {
        const double intensity = peak.getIntensity();
        const double mz = peak.getMZ();
        if (!(intensity > 0.0) || !std::isfinite(intensity) || !std::isfinite(mz)
            || mz < mzMin || mz > mzMax) continue;
        const auto bin = static_cast<std::size_t>(
          std::clamp(static_cast<int>((mz - mzMin) * scale), 0, bins - 1));
        if (pixelBin[bin] == 0.0) touched.push_back(bin);
        pixelBin[bin] += intensity;             // accumulate this pixel's bin total
        weightedMz[bin] += mz * intensity;
      }
      for (const std::size_t bin : touched)     // fold the pixel's totals into the aggregate
      {
        sum[bin] += pixelBin[bin];
        maximum[bin] = std::max(maximum[bin], pixelBin[bin]);   // skyline = max pixel-bin total
        pixelBin[bin] = 0.0;
      }
    }
    const double invPixels = pixelCount > 0 ? 1.0 / static_cast<double>(pixelCount) : 1.0;
    for (std::size_t bin = 0; bin < binCount; ++bin)
    {
      if (maximum[bin] <= 0.0) continue;   // keep occupied bins only
      // Representative m/z = intensity-weighted centroid (the real peak location), NOT
      // the bin centre, so a ppm extraction around it actually hits the peak.
      result.mz.push_back(sum[bin] > 0.0 ? weightedMz[bin] / sum[bin] : 0.0);
      result.mean.push_back(sum[bin] * invPixels);
      result.maxIntensity.push_back(maximum[bin]);
    }
    return result;
  }

  OpenMS::OnDiscImzMLExperiment& ImagingStore::experiment() noexcept { return experiment_; }
  const OpenMS::OnDiscImzMLExperiment& ImagingStore::experiment() const noexcept { return experiment_; }

  ImagingLoadResult ImagingDocument::readImzML(const QString& path)
  {
    ImagingLoadResult result;
    result.summary.sourcePath = QFileInfo(path).absoluteFilePath();
    const QFileInfo file(result.summary.sourcePath);
    if (!file.exists() || !file.isFile())
    {
      result.error = QStringLiteral("File does not exist: %1").arg(result.summary.sourcePath);
      return result;
    }
    if (file.suffix().compare(QStringLiteral("imzML"), Qt::CaseInsensitive) != 0)
    {
      result.error = QStringLiteral("Unsupported imaging file type '%1'.").arg(file.suffix());
      return result;
    }
    const QString ibdPath = file.dir().filePath(file.completeBaseName() + QStringLiteral(".ibd"));
    if (!QFileInfo::exists(ibdPath))
    {
      result.error = QStringLiteral("The companion IBD file is missing: %1").arg(ibdPath);
      return result;
    }

    try
    {
      auto store = std::make_shared<ImagingStore>(result.summary.sourcePath);
      if (!store->isOpen() || store->spectrumCount() == 0)
      {
        result.error = QStringLiteral("The imzML file contains no imaging spectra.");
        return result;
      }
      const auto& experiment = store->experiment();
      const auto& geometry = experiment.getGeometry();
      const auto& meta = experiment.getImzMLMeta();
      result.summary.width = geometry.getWidth();
      result.summary.height = geometry.getHeight();
      result.summary.pixelSizeX = geometry.getPixelSizeX();
      result.summary.pixelSizeY = geometry.getPixelSizeY();
      result.summary.pixelSizeUnit = QString::fromStdString(geometry.getPixelSizeUnit());
      result.summary.imagingMode = QString::fromStdString(meta.imaging_mode);
      result.summary.pixels.reserve(geometry.getPixels().size());
      double mzMinimum = std::numeric_limits<double>::infinity();
      double mzMaximum = -std::numeric_limits<double>::infinity();
      for (const auto& pixel : geometry.getPixels())
      {
        OpenMS::MSSpectrum spectrum = store->spectrum(pixel.spectrum_index);
        double tic = 0.0;
        for (const auto& peak : spectrum)
        {
          if (peak.getIntensity() <= 0.0F) continue;
          tic += peak.getIntensity();
          mzMinimum = std::min(mzMinimum, static_cast<double>(peak.getMZ()));
          mzMaximum = std::max(mzMaximum, static_cast<double>(peak.getMZ()));
          ++result.summary.peakCount;
        }
        result.summary.pixels.push_back({pixel.x, pixel.y, pixel.spectrum_index, tic});
      }
      if (!std::isfinite(mzMinimum))
      {
        result.error = QStringLiteral("The imzML file contains no displayable peaks.");
        return result;
      }
      result.summary.mzMin = mzMinimum;
      result.summary.mzMax = mzMaximum > mzMinimum ? mzMaximum : mzMinimum + 1.0;
      result.store = std::move(store);
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("OpenMS could not read the imzML dataset: %1")
        .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("OpenMS could not read the imzML dataset (unknown error).");
    }
    return result;
  }
}
