#include "model/ChromatogramSource.h"

#include "model/OswStore.h"
#include "model/Sqlite3Api.h"

#include <OpenMS/FORMAT/XICParquetFile.h>
#include <OpenMS/FORMAT/HANDLERS/MzMLSqliteHandler.h>
#include <OpenMS/KERNEL/MSChromatogram.h>

#include <QFileInfo>

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

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

  std::shared_ptr<SqMassChromatogramSource> SqMassChromatogramSource::open(
    const QString& sqMassPath, std::shared_ptr<OswStore> store, QString& error)
  {
    if (!store)
    {
      error = QStringLiteral("A .sqMass chromatogram store needs its .osw for the "
                             "precursor→transition mapping.");
      return nullptr;
    }
    const QFileInfo info(sqMassPath);
    if (!info.exists() || !info.isFile())
    {
      error = QStringLiteral("Chromatogram file not found: %1").arg(sqMassPath);
      return nullptr;
    }

    auto source = std::shared_ptr<SqMassChromatogramSource>(new SqMassChromatogramSource);
    source->path_ = info.absoluteFilePath().toStdString();

    // Open our OWN read-only OswStore connection from the same .osw, so worker-thread
    // fetches never share the GUI's store handle. (The passed store is used only to
    // locate the .osw.)
    source->store_ = OswStore::open(store->sourcePath(), error);
    if (!source->store_) return nullptr;

    // Default the run to the OSW's first; the sqMass's own RUN.ID (read below) is
    // authoritative and needed so the run filter is correct for a multi-run OSW.
    source->runId_ = source->store_->runs().empty() ? 0 : source->store_->runs().front().id;

    // Metadata scan: NATIVE_ID → CHROMATOGRAM.ID (+ the sqMass's own run id). Opened
    // SQLITE_OPEN_READONLY and it never decodes the RT/intensity data blobs, so the
    // index build cannot mutate the input. (The per-precursor decode in fetch() uses
    // OpenMS's reader, which — like all OpenMS sqMass reading — opens read-write but
    // only issues SELECTs; see the note there.)
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(source->path_.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
      error = QStringLiteral("Could not open .sqMass read-only: %1")
                .arg(db ? QString::fromUtf8(sqlite3_errmsg(db)) : sqMassPath);
      if (db) sqlite3_close(db);
      return nullptr;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT ID, NATIVE_ID FROM CHROMATOGRAM;", -1, &stmt, nullptr)
        != SQLITE_OK)
    {
      error = QStringLiteral("The .sqMass file has no readable CHROMATOGRAM table.");
      sqlite3_close(db);
      return nullptr;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      const int id = sqlite3_column_int(stmt, 0);
      if (const unsigned char* native = sqlite3_column_text(stmt, 1))
        source->chromatogramIdByNativeId_.emplace(
          reinterpret_cast<const char*>(native), id);  // first wins on the (unexpected) duplicate
    }
    sqlite3_finalize(stmt);

    // The sqMass RUN.ID is the run this file belongs to — use it (not merely the
    // OSW's first run) so selecting the correct run in a multi-run OSW resolves here.
    sqlite3_stmt* runStmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT ID FROM RUN LIMIT 1;", -1, &runStmt, nullptr) == SQLITE_OK)
    {
      if (sqlite3_step(runStmt) == SQLITE_ROW)
        source->runId_ = sqlite3_column_int64(runStmt, 0);
      sqlite3_finalize(runStmt);
    }
    sqlite3_close(db);

    if (source->chromatogramIdByNativeId_.empty())
    {
      error = QStringLiteral("The .sqMass file contains no chromatograms.");
      return nullptr;
    }
    source->runs_.push_back({source->runId_, QString::fromStdString(source->path_)});
    source->provenance_ = QStringLiteral(".sqMass · %1 chromatograms (lazy)")
                            .arg(source->chromatogramIdByNativeId_.size());
    return source;
  }

  std::vector<TransitionChromatogram> SqMassChromatogramSource::fetch(
    std::int64_t precursorId, std::int64_t runId) const
  {
    std::vector<TransitionChromatogram> result;
    if (!store_) return result;
    // This sqMass covers a single run; honour an explicit, non-matching run filter.
    if (runId >= 0 && runId != runId_) return result;

    // Transitions come from our private store connection; serialize since overlapping
    // fetches (rapid precursor clicks) can run on separate worker threads.
    std::vector<OswTransition> transitions;
    {
      const std::lock_guard<std::mutex> lock(storeMutex_);
      transitions = store_->transitions(precursorId);
    }

    // Resolve each of the precursor's library transitions to its sqMass chromatogram
    // (NATIVE_ID == the transition id), collecting the ids to decode and seeding the
    // result rows with the OSW library metadata the sqMass file itself lacks. Native
    // ids are de-duplicated so a duplicated mapping row (merged/IPF libraries) can't
    // put the same id in the WHERE-IN list twice (which the reader rejects).
    std::vector<int> ids;
    std::unordered_map<std::string, std::size_t> rowByNative;
    std::unordered_set<std::string> seen;
    const auto addRow = [&](const std::string& native, TransitionChromatogram row)
    {
      if (!seen.insert(native).second) return;             // already handled this native id
      const auto it = chromatogramIdByNativeId_.find(native);
      if (it == chromatogramIdByNativeId_.end()) return;    // no chromatogram for this transition
      ids.push_back(it->second);
      rowByNative.emplace(native, result.size());
      result.push_back(std::move(row));
    };

    for (const OswTransition& transition : transitions)
    {
      TransitionChromatogram row;
      row.transitionId = transition.id;
      row.precursorId = precursorId;
      row.runId = runId_;
      row.msLevel = 2;
      row.ordinal = transition.ordinal;
      row.ionType = transition.type;
      row.annotation = transition.annotation;
      row.detecting = transition.detecting;
      addRow(std::to_string(transition.id), std::move(row));
    }
    // MS1 precursor trace, best-effort (OpenSWATH names it "<group>_Precursor_i0";
    // the group id is the precursor id for the common single-peptide case).
    {
      TransitionChromatogram ms1;
      ms1.transitionId = -1;
      ms1.precursorId = precursorId;
      ms1.runId = runId_;
      ms1.msLevel = 1;
      ms1.annotation = QStringLiteral("Precursor");
      addRow(std::to_string(precursorId) + "_Precursor_i0", std::move(ms1));
    }
    if (ids.empty()) return result;

    // Guard against the file vanishing between open() and fetch(): OpenMS's reader
    // opens READWRITE_OR_CREATE, so a missing path would otherwise create an empty DB.
    if (!QFileInfo::exists(QString::fromStdString(path_))) return result;

    // Decode only the collected chromatograms (WHERE ID IN (...)) — not the whole
    // file. OpenMS's sqMass reader opens the DB read-write (as every OpenMS sqMass
    // read does), but the decode path issues only SELECTs, so the data is not
    // mutated; on genuinely read-only storage the open fails and traces stay empty.
    //
    // readChromatograms throws for the WHOLE batch if any id can't be resolved 1:1
    // (e.g. a chromatogram missing its PRODUCT row), so on failure fall back to
    // decoding each id alone — one bad chromatogram then can't blank the precursor.
    std::vector<OpenMS::MSChromatogram> decoded;
    try
    {
      OpenMS::Internal::MzMLSqliteHandler handler(path_, /*run_id (ignored on read)*/ 0);
      handler.readChromatograms(decoded, ids, /*meta_only*/ false);
    }
    catch (...)
    {
      decoded.clear();
      for (const int id : ids)
      {
        try
        {
          OpenMS::Internal::MzMLSqliteHandler handler(path_, 0);
          std::vector<OpenMS::MSChromatogram> one;
          handler.readChromatograms(one, {id}, /*meta_only*/ false);
          for (OpenMS::MSChromatogram& chromatogram : one) decoded.push_back(std::move(chromatogram));
        }
        catch (...)
        {
          // Skip just this chromatogram; the others still render.
        }
      }
    }

    for (const OpenMS::MSChromatogram& chromatogram : decoded)
    {
      const auto it = rowByNative.find(chromatogram.getNativeID());
      if (it == rowByNative.end()) continue;  // match by native id — order-independent
      TransitionChromatogram& row = result[it->second];
      row.rt.reserve(chromatogram.size());
      row.intensity.reserve(chromatogram.size());
      for (const auto& point : chromatogram)
      {
        row.rt.push_back(point.getRT());
        row.intensity.push_back(point.getIntensity());
      }
    }
    return result;
  }
}
