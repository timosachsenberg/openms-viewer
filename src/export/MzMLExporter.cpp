#include "export/MzMLExporter.h"

#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/IONMOBILITY/IMTypes.h>

#include <QFileInfo>

#include <cmath>
#include <string>
#include <vector>

namespace OpenMSViewer
{
  namespace
  {
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

  std::shared_ptr<OpenMS::MSExperiment> MzMLExporter::filter(
    const OpenMS::MSExperiment& source, const MzMLExportFilter& filterSettings)
  {
    auto output = std::make_shared<OpenMS::MSExperiment>();
    output->getExperimentalSettings() = source.getExperimentalSettings();
    const PlotRange range = filterSettings.range.normalized();
    for (const OpenMS::MSSpectrum& sourceSpectrum : source)
    {
      if (!filterSettings.msLevels.empty()
          && !filterSettings.msLevels.contains(sourceSpectrum.getMSLevel())) continue;
      if (sourceSpectrum.getRT() < range.rtMin || sourceSpectrum.getRT() > range.rtMax) continue;
      if (filterSettings.faimsCompensationVoltage)
      {
        const auto cv = compensationVoltage(sourceSpectrum);
        if (!cv || std::abs(*cv - *filterSettings.faimsCompensationVoltage) > 1e-6) continue;
      }

      OpenMS::MSSpectrum spectrum = sourceSpectrum;
      std::vector<OpenMS::Size> selected;
      selected.reserve(spectrum.size());
      for (OpenMS::Size index = 0; index < spectrum.size(); ++index)
      {
        const double mz = spectrum[index].getMZ();
        if (mz >= range.mzMin && mz <= range.mzMax) selected.push_back(index);
      }
      spectrum.select(selected);
      output->addSpectrum(std::move(spectrum));
    }
    output->sortSpectra(true);
    return output;
  }

  MzMLExportResult MzMLExporter::write(
    std::shared_ptr<const OpenMS::MSExperiment> source, const QString& outputPath,
    const MzMLExportFilter& filterSettings)
  {
    MzMLExportResult result;
    result.outputPath = QFileInfo(outputPath).absoluteFilePath();
    if (!source)
    {
      result.error = QStringLiteral("No mzML experiment is loaded.");
      return result;
    }
    if (!filterSettings.range.isValid())
    {
      result.error = QStringLiteral("The export RT/m/z range is invalid.");
      return result;
    }
    if (filterSettings.msLevels.empty())
    {
      result.error = QStringLiteral("Select at least one MS level for export.");
      return result;
    }
    try
    {
      const auto output = filter(*source, filterSettings);
      result.spectrumCount = output->size();
      for (const auto& spectrum : *output) result.peakCount += spectrum.size();
      OpenMS::MzMLFile().store(result.outputPath.toStdString(), *output);
    }
    catch (const std::exception& error)
    {
      result.error = QStringLiteral("OpenMS could not export mzML: %1")
        .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
      result.error = QStringLiteral("OpenMS could not export mzML (unknown error).");
    }
    return result;
  }
}
