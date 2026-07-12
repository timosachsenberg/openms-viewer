#include "model/ChromatogramSource.h"

#include <OpenMS/FORMAT/XICParquetFile.h>

#include <QFileInfo>

namespace OpenMSViewer
{
  std::shared_ptr<XicChromatogramSource> XicChromatogramSource::open(
    const QStringList& paths, QString& error)
  {
    if (paths.isEmpty())
    {
      error = QStringLiteral("No .xic chromatogram file provided.");
      return nullptr;
    }
    // Run identity across multiple .xic files would need (source_file, run_id)
    // keying — colliding run IDs would otherwise merge. Single file for now.
    if (paths.size() != 1)
    {
      error = QStringLiteral("Multiple .xic files are not supported yet (ambiguous run identity).");
      return nullptr;
    }
    std::vector<std::string> files;
    files.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString& path : paths)
    {
      const QFileInfo info(path);
      if (!info.exists() || !info.isFile())
      {
        error = QStringLiteral("Chromatogram file not found: %1").arg(path);
        return nullptr;
      }
      files.push_back(info.absoluteFilePath().toStdString());
    }
    try
    {
      OpenMS::XICParquetFile file(files);
      std::vector<OpenMS::XICParquetFile::XICRunInfo> runInfos;
      file.getRuns(runInfos);  // never decodes RT/intensity arrays

      auto source = std::shared_ptr<XicChromatogramSource>(new XicChromatogramSource);
      source->paths_ = std::move(files);
      for (const auto& run : runInfos)
        source->runs_.push_back(
          {static_cast<std::int64_t>(run.run_id), QString::fromStdString(run.source_file)});
      source->provenance_ = QStringLiteral(".xic Parquet · %1 run(s)").arg(source->runs_.size());
      return source;
    }
    catch (const std::exception& exception)
    {
      error = QStringLiteral("Could not open .xic chromatograms: %1")
                .arg(QString::fromLocal8Bit(exception.what()));
    }
    catch (...)
    {
      error = QStringLiteral("Could not open .xic chromatograms (unknown error).");
    }
    return nullptr;
  }

  std::vector<TransitionChromatogram> XicChromatogramSource::fetch(
    std::int64_t precursorId, std::int64_t runId) const
  {
    std::vector<TransitionChromatogram> result;
    try
    {
      OpenMS::XICParquetFile file(paths_);
      std::vector<OpenMS::XICParquetFile::XICChromatogram> chromatograms;
      // Filter pushdown: only this precursor (and run) is decoded.
      file.getChromatograms(chromatograms, precursorId, /*transition_id*/ -1,
                            /*modified_sequence*/ "", /*precursor_charge*/ -1,
                            /*product_charge*/ -1, /*ms_level*/ -1, runId);
      result.reserve(chromatograms.size());
      for (auto& source : chromatograms)
      {
        TransitionChromatogram transition;
        transition.transitionId = source.has_transition_id
                                    ? static_cast<std::int64_t>(source.transition_id) : -1;
        transition.precursorId = source.has_precursor_id
                                   ? static_cast<std::int64_t>(source.precursor_id) : precursorId;
        transition.runId = static_cast<std::int64_t>(source.run_id);
        transition.msLevel = static_cast<int>(source.ms_level);
        transition.ordinal = source.has_transition_ordinal
                               ? static_cast<int>(source.transition_ordinal) : 0;
        transition.ionType = QString::fromStdString(source.transition_type);
        transition.annotation = QString::fromStdString(source.annotation);
        transition.detecting = source.has_detecting_transition && source.detecting_transition != 0;
        transition.rt = std::move(source.rt);
        transition.intensity = std::move(source.intensity);
        result.push_back(std::move(transition));
      }
    }
    catch (...)
    {
      // A read failure yields an empty result; the caller renders "no data".
    }
    return result;
  }
}
