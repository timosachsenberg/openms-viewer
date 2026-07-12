#include "model/OswStore.h"

#include "model/Sqlite3Api.h"

#include <QStringList>

#include <algorithm>
#include <limits>

namespace OpenMSViewer
{
  struct OswStore::Sqlite
  {
    sqlite3* db{nullptr};
  };

  namespace
  {
    // sqlite3_column_* wrappers that map SQL NULL to std::nullopt.
    std::optional<double> columnDouble(sqlite3_stmt* stmt, int index)
    {
      if (sqlite3_column_type(stmt, index) == SQLITE_NULL) return std::nullopt;
      return sqlite3_column_double(stmt, index);
    }
    std::optional<int> columnInt(sqlite3_stmt* stmt, int index)
    {
      if (sqlite3_column_type(stmt, index) == SQLITE_NULL) return std::nullopt;
      return sqlite3_column_int(stmt, index);
    }
    QString columnText(sqlite3_stmt* stmt, int index)
    {
      const auto* text = sqlite3_column_text(stmt, index);
      return text ? QString::fromUtf8(reinterpret_cast<const char*>(text)) : QString{};
    }

    // "VAR_XCORR_SHAPE" → "xcorr shape".
    QString prettySubScore(const QString& column)
    {
      QString name = column;
      if (name.startsWith(QStringLiteral("VAR_"), Qt::CaseInsensitive)) name.remove(0, 4);
      return name.toLower().replace(QLatin1Char('_'), QLatin1Char(' '));
    }

    // Quote a SQL identifier (doubling embedded quotes). Column/table names here
    // come from PRAGMA, but quoting keeps a hostile schema from breaking the query.
    QString quoteIdentifier(const QString& identifier)
    {
      QString quoted = identifier;
      quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
      return QLatin1Char('"') + quoted + QLatin1Char('"');
    }
  }

  OswStore::~OswStore()
  {
    if (db_ && db_->db) sqlite3_close(db_->db);
  }

  std::shared_ptr<OswStore> OswStore::open(const QString& path, QString& error)
  {
    sqlite3* handle = nullptr;
    // READONLY: never mutate the input (OpenMS's OSWFile creates indexes on open).
    const int rc = sqlite3_open_v2(path.toUtf8().constData(), &handle,
                                   SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || handle == nullptr)
    {
      error = QStringLiteral("Could not open OpenSWATH database: %1")
                .arg(handle ? QString::fromUtf8(sqlite3_errmsg(handle))
                            : QStringLiteral("out of memory"));
      if (handle) sqlite3_close(handle);
      return nullptr;
    }

    auto store = std::shared_ptr<OswStore>(new OswStore);
    store->db_ = std::make_unique<Sqlite>();
    store->db_->db = handle;
    store->sourcePath_ = path;

    // A valid OSW must have PRECURSOR + FEATURE with the columns we query — check
    // the columns too, so a table that merely shares the name (but not the schema)
    // is rejected up front instead of silently yielding empty results later.
    const bool schemaOk =
      store->columnExists(QStringLiteral("PRECURSOR"), QStringLiteral("PRECURSOR_MZ"))
      && store->columnExists(QStringLiteral("PRECURSOR"), QStringLiteral("CHARGE"))
      && store->columnExists(QStringLiteral("FEATURE"), QStringLiteral("PRECURSOR_ID"))
      && store->columnExists(QStringLiteral("FEATURE"), QStringLiteral("EXP_RT"))
      && store->columnExists(QStringLiteral("FEATURE"), QStringLiteral("LEFT_WIDTH"))
      && store->columnExists(QStringLiteral("FEATURE"), QStringLiteral("RIGHT_WIDTH"));
    if (!schemaOk)
    {
      error = QStringLiteral("Not an OpenSWATH results database (unexpected PRECURSOR/FEATURE schema).");
      return nullptr;
    }
    store->hasScoreMs2_ = store->tableExists(QStringLiteral("SCORE_MS2"));
    store->hasFeatureMs2_ = store->tableExists(QStringLiteral("FEATURE_MS2"));

    // Runs.
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(handle, "SELECT ID, FILENAME FROM RUN ORDER BY ID", -1, &stmt, nullptr)
          == SQLITE_OK)
    {
      while (sqlite3_step(stmt) == SQLITE_ROW)
        store->runs_.push_back({sqlite3_column_int64(stmt, 0), columnText(stmt, 1)});
      sqlite3_finalize(stmt);
    }

    // Discover FEATURE_MS2 VAR_* subscore columns for the score inspector.
    if (store->hasFeatureMs2_
        && sqlite3_prepare_v2(handle, "PRAGMA table_info(FEATURE_MS2)", -1, &stmt, nullptr)
             == SQLITE_OK)
    {
      while (sqlite3_step(stmt) == SQLITE_ROW)
      {
        const QString name = columnText(stmt, 1);  // column 1 = name
        if (name.startsWith(QStringLiteral("VAR_"), Qt::CaseInsensitive))
          store->ms2SubScoreColumns_.push_back(name);
      }
      sqlite3_finalize(stmt);
    }
    return store;
  }

  bool OswStore::tableExists(const QString& name) const
  {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->db,
          "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1 LIMIT 1",
          -1, &stmt, nullptr) != SQLITE_OK)
      return false;
    sqlite3_bind_text(stmt, 1, name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    const bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
  }

  bool OswStore::columnExists(const QString& table, const QString& column) const
  {
    // PRAGMA can't bind parameters; the table names are literal constants but are
    // quoted defensively anyway.
    const QString sql = QStringLiteral("PRAGMA table_info(") + quoteIdentifier(table)
      + QStringLiteral(")");
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK)
      return false;
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      if (columnText(stmt, 1).compare(column, Qt::CaseInsensitive) == 0) { found = true; break; }
    }
    sqlite3_finalize(stmt);
    return found;
  }

  std::vector<OswPrecursor> OswStore::precursors() const
  {
    // Modified sequence + peak-group count are correlated subqueries; best q-value
    // is added only when SCORE_MS2 exists.
    QString sql = QStringLiteral(
      "SELECT P.ID, P.CHARGE, P.PRECURSOR_MZ, P.LIBRARY_RT, P.DECOY,"
      " (SELECT PEP.MODIFIED_SEQUENCE FROM PRECURSOR_PEPTIDE_MAPPING M"
      "  JOIN PEPTIDE PEP ON PEP.ID = M.PEPTIDE_ID WHERE M.PRECURSOR_ID = P.ID LIMIT 1),"
      " (SELECT COUNT(*) FROM FEATURE F WHERE F.PRECURSOR_ID = P.ID)");
    if (hasScoreMs2_)
      sql += QStringLiteral(
        ", (SELECT MIN(S.QVALUE) FROM FEATURE F JOIN SCORE_MS2 S ON S.FEATURE_ID = F.ID"
        "   WHERE F.PRECURSOR_ID = P.ID)");
    sql += QStringLiteral(" FROM PRECURSOR P");

    std::vector<OswPrecursor> result;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK)
      return result;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      OswPrecursor precursor;
      precursor.id = sqlite3_column_int64(stmt, 0);
      precursor.charge = sqlite3_column_int(stmt, 1);
      precursor.precursorMz = sqlite3_column_double(stmt, 2);
      precursor.libraryRt = sqlite3_column_double(stmt, 3);
      precursor.decoy = sqlite3_column_int(stmt, 4) != 0;
      precursor.modifiedSequence = columnText(stmt, 5);
      precursor.peakGroupCount = sqlite3_column_int(stmt, 6);
      if (hasScoreMs2_) precursor.bestQValue = columnDouble(stmt, 7);
      result.push_back(std::move(precursor));
    }
    sqlite3_finalize(stmt);

    // Best (lowest) q-value first; unscored/absent sink to the bottom by sequence.
    std::sort(result.begin(), result.end(), [](const OswPrecursor& a, const OswPrecursor& b)
    {
      const double qa = a.bestQValue.value_or(std::numeric_limits<double>::infinity());
      const double qb = b.bestQValue.value_or(std::numeric_limits<double>::infinity());
      if (qa != qb) return qa < qb;
      return a.modifiedSequence < b.modifiedSequence;
    });
    return result;
  }

  std::vector<OswPeakGroup> OswStore::peakGroups(std::int64_t precursorId) const
  {
    QString sql = QStringLiteral(
      "SELECT F.ID, F.RUN_ID, F.EXP_RT, F.DELTA_RT, F.LEFT_WIDTH, F.RIGHT_WIDTH");
    int scoreBase = -1;
    int featBase = -1;
    int column = 6;
    QString joins;
    if (hasScoreMs2_)
    {
      sql += QStringLiteral(", S.SCORE, S.RANK, S.QVALUE, S.PEP");
      scoreBase = column;
      column += 4;
      joins += QStringLiteral(" LEFT JOIN SCORE_MS2 S ON S.FEATURE_ID = F.ID");
    }
    if (hasFeatureMs2_)
    {
      sql += QStringLiteral(", M2.AREA_INTENSITY, M2.APEX_INTENSITY");
      featBase = column;
      column += 2;
      joins += QStringLiteral(" LEFT JOIN FEATURE_MS2 M2 ON M2.FEATURE_ID = F.ID");
    }
    sql += QStringLiteral(" FROM FEATURE F") + joins
         + QStringLiteral(" WHERE F.PRECURSOR_ID = ?1");
    sql += hasScoreMs2_ ? QStringLiteral(" ORDER BY S.RANK IS NULL, S.RANK, S.QVALUE")
                        : QStringLiteral(" ORDER BY F.EXP_RT");

    std::vector<OswPeakGroup> result;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK)
      return result;
    sqlite3_bind_int64(stmt, 1, precursorId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      OswPeakGroup group;
      group.featureId = sqlite3_column_int64(stmt, 0);
      group.runId = sqlite3_column_int64(stmt, 1);
      group.precursorId = precursorId;
      group.apexRt = sqlite3_column_double(stmt, 2);
      group.deltaRt = sqlite3_column_double(stmt, 3);
      group.leftWidth = sqlite3_column_double(stmt, 4);
      group.rightWidth = sqlite3_column_double(stmt, 5);
      if (scoreBase >= 0)
      {
        group.score = columnDouble(stmt, scoreBase);
        group.rank = columnInt(stmt, scoreBase + 1);
        group.qValue = columnDouble(stmt, scoreBase + 2);
        group.pep = columnDouble(stmt, scoreBase + 3);
      }
      if (featBase >= 0)
      {
        group.areaIntensity = columnDouble(stmt, featBase);
        group.apexIntensity = columnDouble(stmt, featBase + 1);
      }
      result.push_back(std::move(group));
    }
    sqlite3_finalize(stmt);
    return result;
  }

  std::vector<OswTransition> OswStore::transitions(std::int64_t precursorId) const
  {
    std::vector<OswTransition> result;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->db,
          "SELECT T.ID, T.PRODUCT_MZ, T.CHARGE, T.TYPE, T.ANNOTATION, T.ORDINAL,"
          " T.DETECTING, T.DECOY"
          " FROM TRANSITION T JOIN TRANSITION_PRECURSOR_MAPPING M ON M.TRANSITION_ID = T.ID"
          " WHERE M.PRECURSOR_ID = ?1 ORDER BY T.ORDINAL",
          -1, &stmt, nullptr) != SQLITE_OK)
      return result;
    sqlite3_bind_int64(stmt, 1, precursorId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      OswTransition transition;
      transition.id = sqlite3_column_int64(stmt, 0);
      transition.productMz = sqlite3_column_double(stmt, 1);
      transition.charge = sqlite3_column_int(stmt, 2);
      transition.type = columnText(stmt, 3);
      transition.annotation = columnText(stmt, 4);
      transition.ordinal = sqlite3_column_int(stmt, 5);
      transition.detecting = sqlite3_column_int(stmt, 6) != 0;
      transition.decoy = sqlite3_column_int(stmt, 7) != 0;
      result.push_back(std::move(transition));
    }
    sqlite3_finalize(stmt);
    return result;
  }

  std::vector<OswSubScore> OswStore::subScores(std::int64_t featureId) const
  {
    std::vector<OswSubScore> result;
    if (ms2SubScoreColumns_.empty()) return result;
    QStringList columns;
    for (const QString& column : ms2SubScoreColumns_) columns.push_back(quoteIdentifier(column));
    const QString sql = QStringLiteral("SELECT ") + columns.join(QLatin1Char(','))
      + QStringLiteral(" FROM FEATURE_MS2 WHERE FEATURE_ID = ?1 LIMIT 1");
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK)
      return result;
    sqlite3_bind_int64(stmt, 1, featureId);
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      const int count = static_cast<int>(ms2SubScoreColumns_.size());
      for (int index = 0; index < count; ++index)
        if (const auto value = columnDouble(stmt, index))
          result.push_back({prettySubScore(ms2SubScoreColumns_[static_cast<std::size_t>(index)]), *value});
    }
    sqlite3_finalize(stmt);
    return result;
  }
}
