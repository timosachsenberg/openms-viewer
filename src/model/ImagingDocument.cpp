#include "model/ImagingDocument.h"

#include <OpenMS/FORMAT/BrukerTimsImagingFile.h>
#include <OpenMS/FORMAT/ImzMLFile.h>
#include <OpenMS/IMAGING/MSImagingExperiment.h>
#include <OpenMS/IMAGING/MSImagingGeometry.h>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace OpenMSViewer
{
  namespace
  {
    struct PeakSample
    {
      double mz{0.0};
      double intensity{0.0};
    };

    // Log-spaced binning over one spectrum's peaks (pyopenms-viewer compute_aggregate).
    void accumulateSpectrum(const std::vector<PeakSample>& peaks,
                            const std::vector<double>& edges, std::size_t binCount,
                            std::vector<double>& sum, std::vector<double>& maximum,
                            std::vector<std::size_t>& hitCount, std::vector<char>& touched,
                            std::vector<std::size_t>& touchedBins, double mzMin, double mzMax)
    {
      touchedBins.clear();
      for (const PeakSample& peak : peaks)
      {
        if (!(peak.intensity > 0.0) || !std::isfinite(peak.intensity) || !std::isfinite(peak.mz)
            || peak.mz < mzMin || peak.mz > mzMax)
          continue;
        // searchsorted(edges, mz, side="right") - 1
        auto it = std::upper_bound(edges.begin(), edges.end(), peak.mz);
        if (it == edges.begin()) continue;
        auto bin = static_cast<std::size_t>(std::distance(edges.begin(), it) - 1);
        if (bin >= binCount) bin = binCount - 1;
        sum[bin] += peak.intensity;
        maximum[bin] = std::max(maximum[bin], peak.intensity);
        if (!touched[bin])
        {
          touched[bin] = 1;
          touchedBins.push_back(bin);
        }
      }
      for (const std::size_t bin : touchedBins)
      {
        ++hitCount[bin];
        touched[bin] = 0;
      }
    }

    AggregateSpectrum finalizeAggregate(const std::vector<double>& edges, std::size_t binCount,
                                        const std::vector<double>& sum,
                                        const std::vector<double>& maximum,
                                        const std::vector<std::size_t>& hitCount)
    {
      AggregateSpectrum result;
      for (std::size_t bin = 0; bin < binCount; ++bin)
      {
        if (maximum[bin] <= 0.0 || hitCount[bin] == 0) continue;
        // Bin centre (pyopenms-viewer centers = 0.5 * (edges[:-1] + edges[1:])).
        result.mz.push_back(0.5 * (edges[bin] + edges[bin + 1]));
        result.mean.push_back(sum[bin] / static_cast<double>(hitCount[bin]));
        result.maxIntensity.push_back(maximum[bin]);
      }
      return result;
    }

    bool prepareBins(double mzMin, double mzMax, double binPpm, std::vector<double>& edges,
                     std::size_t& binCount)
    {
      if (!(mzMax > mzMin) || !(binPpm > 0.0) || !std::isfinite(mzMin) || !std::isfinite(mzMax)
          || !std::isfinite(binPpm) || !(mzMin > 0.0))
        return false;
      const double step = std::log1p(binPpm * 1e-6);
      if (!(step > 0.0)) return false;
      const double logMin = std::log(mzMin);
      const double logMax = std::log(mzMax);
      const auto edgeCount = static_cast<std::size_t>(
        std::max(2.0, std::ceil((logMax - logMin) / step) + 1.0));
      edges.resize(edgeCount);
      for (std::size_t i = 0; i < edgeCount; ++i)
        edges[i] = mzMin * std::exp(static_cast<double>(i) * step);
      binCount = edgeCount - 1;
      return binCount > 0;
    }

    AggregateSpectrum aggregateCachedPeaks(const std::vector<std::vector<PeakSample>>& perPixel,
                                           double mzMin, double mzMax, double binPpm)
    {
      AggregateSpectrum result;
      std::vector<double> edges;
      std::size_t binCount = 0;
      if (!prepareBins(mzMin, mzMax, binPpm, edges, binCount)) return result;

      std::vector<double> sum(binCount, 0.0);
      std::vector<double> maximum(binCount, 0.0);
      std::vector<std::size_t> hitCount(binCount, 0);
      std::vector<char> touched(binCount, 0);
      std::vector<std::size_t> touchedBins;
      for (const auto& peaks : perPixel)
        accumulateSpectrum(peaks, edges, binCount, sum, maximum, hitCount, touched, touchedBins,
                           mzMin, mzMax);
      return finalizeAggregate(edges, binCount, sum, maximum, hitCount);
    }
  }

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

  AggregateSpectrum ImagingStore::aggregateSpectrum(double mzMin, double mzMax, double binPpm,
                                                    CancellationCheck cancelled) const
  {
    AggregateSpectrum result;
    std::vector<double> edges;
    std::size_t binCount = 0;
    if (!prepareBins(mzMin, mzMax, binPpm, edges, binCount)) return result;

    std::vector<double> sum(binCount, 0.0);
    std::vector<double> maximum(binCount, 0.0);      // skyline: max per-peak intensity
    std::vector<std::size_t> hitCount(binCount, 0);  // spectra that contributed to each bin
    std::vector<char> touched(binCount, 0);
    std::vector<std::size_t> touchedBins;
    std::vector<PeakSample> peaks;

    // Iterate geometry pixels only (same domain as pyopenms-viewer). spectrum()
    // locks per call so ion-image extraction can still interleave.
    const auto& pixels = experiment_.getGeometry().getPixels();
    for (const auto& pixel : pixels)
    {
      if (cancelled && cancelled()) return {};
      const OpenMS::MSSpectrum spectrum = this->spectrum(pixel.spectrum_index);
      peaks.clear();
      peaks.reserve(spectrum.size());
      for (const auto& peak : spectrum)
      {
        const double intensity = peak.getIntensity();
        const double mz = peak.getMZ();
        if (intensity > 0.0 && std::isfinite(intensity) && std::isfinite(mz))
          peaks.push_back({mz, intensity});
      }
      accumulateSpectrum(peaks, edges, binCount, sum, maximum, hitCount, touched, touchedBins,
                         mzMin, mzMax);
    }
    return finalizeAggregate(edges, binCount, sum, maximum, hitCount);
  }

  OpenMS::OnDiscImzMLExperiment& ImagingStore::experiment() noexcept { return experiment_; }
  const OpenMS::OnDiscImzMLExperiment& ImagingStore::experiment() const noexcept { return experiment_; }

  void ImagingStore::retainTempDir(std::shared_ptr<QTemporaryDir> dir) { tempDir_ = std::move(dir); }

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

      // One disk decode per geometry pixel: TIC / m/z range and a peak cache for
      // the default-bin aggregate, so open does not re-read the IBD for sticks.
      std::vector<std::vector<PeakSample>> peakCache;
      peakCache.reserve(geometry.getPixels().size());
      double mzMinimum = std::numeric_limits<double>::infinity();
      double mzMaximum = -std::numeric_limits<double>::infinity();
      for (const auto& pixel : geometry.getPixels())
      {
        OpenMS::MSSpectrum spectrum = store->spectrum(pixel.spectrum_index);
        double tic = 0.0;
        std::vector<PeakSample> peaks;
        peaks.reserve(spectrum.size());
        for (const auto& peak : spectrum)
        {
          const double intensity = peak.getIntensity();
          if (!(intensity > 0.0) || !std::isfinite(intensity)) continue;
          const double mz = peak.getMZ();
          if (!std::isfinite(mz)) continue;
          tic += intensity;
          mzMinimum = std::min(mzMinimum, mz);
          mzMaximum = std::max(mzMaximum, mz);
          ++result.summary.peakCount;
          peaks.push_back({mz, intensity});
        }
        result.summary.pixels.push_back({pixel.x, pixel.y, pixel.spectrum_index, tic});
        peakCache.push_back(std::move(peaks));
      }
      if (!std::isfinite(mzMinimum))
      {
        result.error = QStringLiteral("The imzML file contains no displayable peaks.");
        return result;
      }
      result.summary.mzMin = mzMinimum;
      result.summary.mzMax = mzMaximum > mzMinimum ? mzMaximum : mzMinimum + 1.0;
      result.aggregateBinPpm = ImagingStore::kDefaultAggregateBinPpm;
      result.aggregate = aggregateCachedPeaks(peakCache, result.summary.mzMin, result.summary.mzMax,
                                              result.aggregateBinPpm);
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

  ImagingLoadResult ImagingDocument::readBrukerMaldi(const QString& path)
  {
    ImagingLoadResult result;
    const QString absPath = QFileInfo(path).absoluteFilePath();
    result.summary.sourcePath = absPath;
#ifndef WITH_OPENTIMS
    result.error = QStringLiteral(
      "This build lacks Bruker timsTOF (opentims) support, so MALDI .d imaging cannot be read.");
    return result;
#else
    const QFileInfo info(absPath);
    if (!info.exists() || !info.isDir())
    {
      result.error = QStringLiteral("Not a directory: %1").arg(absPath);
      return result;
    }
    try
    {
      // Single-quad .tsf MALDI has no analysis.tdf, and non-imaging .d datasets
      // report a different MaldiApplicationType — reject both with a clear message
      // rather than delegating to a load that would throw deep inside OpenMS.
      if (!OpenMS::BrukerTimsImagingFile::isImagingDataset(absPath.toStdString()))
      {
        result.error = QStringLiteral(
          "Not a Bruker MALDI imaging .d dataset (needs analysis.tdf with "
          "MaldiApplicationType = 'Imaging'). Single-quad .tsf MALDI is not supported.");
        return result;
      }

      OpenMS::MSImagingExperiment imaging;
      OpenMS::BrukerTimsImagingFile().load(absPath.toStdString(), imaging);

      // Convert to a session-lifetime temporary imzML and reuse the imzML pipeline
      // (lazy on-disc reads, ion images, aggregate spectrum). The temp dir is bound
      // to the returned store so it survives for the whole imaging session.
      auto tempDir = std::make_shared<QTemporaryDir>();
      if (!tempDir->isValid())
      {
        result.error = QStringLiteral("Could not create a temporary directory for the converted image.");
        return result;
      }
      const QString imzml = tempDir->filePath(info.completeBaseName() + QStringLiteral(".imzML"));
      OpenMS::ImzMLFile().store(imzml.toStdString(), imaging);

      result = readImzML(imzml);
      if (result.succeeded())
      {
        result.store->retainTempDir(tempDir);
        result.summary.sourcePath = absPath;  // show the .d, not the temp imzML
      }
      return result;
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("OpenMS could not read the MALDI .d dataset: %1")
        .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("OpenMS could not read the MALDI .d dataset (unknown error).");
    }
    return result;
#endif
  }
}
