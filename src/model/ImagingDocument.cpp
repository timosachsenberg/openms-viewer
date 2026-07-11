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
