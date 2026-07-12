#pragma once

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace OpenMSViewer
{
  // A run (input file) in an OpenSWATH results database.
  struct OswRun
  {
    std::int64_t id{0};
    QString filename;
  };

  // A precursor / analyte — the quantitation unit the user navigates by.
  struct OswPrecursor
  {
    std::int64_t id{0};
    QString modifiedSequence;
    int charge{0};
    double precursorMz{0.0};
    double libraryRt{0.0};
    bool decoy{false};
    int peakGroupCount{0};
    std::optional<double> bestQValue;  ///< min q-value across its features (if scored)
  };

  // A detected peak group (one FEATURE row) for a precursor in one run.
  struct OswPeakGroup
  {
    std::int64_t featureId{0};
    std::int64_t runId{0};
    std::int64_t precursorId{0};
    double apexRt{0.0};          ///< FEATURE.EXP_RT
    double leftWidth{0.0};       ///< FEATURE.LEFT_WIDTH (RT boundary)
    double rightWidth{0.0};      ///< FEATURE.RIGHT_WIDTH (RT boundary)
    double deltaRt{0.0};         ///< FEATURE.DELTA_RT (Δ to library RT)
    std::optional<double> areaIntensity;   ///< FEATURE_MS2.AREA_INTENSITY
    std::optional<double> apexIntensity;   ///< FEATURE_MS2.APEX_INTENSITY
    std::optional<int> rank;               ///< SCORE_MS2.RANK
    std::optional<double> score;           ///< SCORE_MS2.SCORE (d-score)
    std::optional<double> qValue;          ///< SCORE_MS2.QVALUE
    std::optional<double> pep;             ///< SCORE_MS2.PEP
  };

  // A library transition for a precursor (product ion → its XIC).
  struct OswTransition
  {
    std::int64_t id{0};
    double productMz{0.0};
    int charge{0};
    QString type;         ///< b / y / ...
    QString annotation;
    int ordinal{0};
    bool detecting{false};
    bool decoy{false};
  };

  struct OswSubScore
  {
    QString name;         ///< e.g. "xcorr shape" (from FEATURE_MS2 VAR_* column)
    double value{0.0};
  };

  // Read-only accessor over an OpenSWATH .osw SQLite database. Opened
  // SQLITE_OPEN_READONLY so the input is never mutated. Worker-thread-affine:
  // create and query on the same (background) thread; not internally synchronized.
  // Optional score/subscore tables are capability-detected and degrade gracefully.
  class OswStore
  {
  public:
    ~OswStore();
    OswStore(const OswStore&) = delete;
    OswStore& operator=(const OswStore&) = delete;

    // Opens the database read-only. Returns nullptr and fills @p error on failure.
    [[nodiscard]] static std::shared_ptr<OswStore> open(const QString& path, QString& error);

    [[nodiscard]] const QString& sourcePath() const noexcept { return sourcePath_; }
    [[nodiscard]] const std::vector<OswRun>& runs() const noexcept { return runs_; }
    [[nodiscard]] bool hasScores() const noexcept { return hasScoreMs2_; }

    // All precursors (with peak-group count + best q-value where scored), sorted
    // by best q-value then modified sequence.
    [[nodiscard]] std::vector<OswPrecursor> precursors() const;
    // Detected peak groups for a precursor, ranked (best first).
    [[nodiscard]] std::vector<OswPeakGroup> peakGroups(std::int64_t precursorId) const;
    // Library transitions for a precursor.
    [[nodiscard]] std::vector<OswTransition> transitions(std::int64_t precursorId) const;
    // Non-null FEATURE_MS2 VAR_* subscores for one feature (empty if none).
    [[nodiscard]] std::vector<OswSubScore> subScores(std::int64_t featureId) const;

  private:
    OswStore() = default;
    bool tableExists(const QString& name) const;
    bool columnExists(const QString& table, const QString& column) const;

    struct Sqlite;                         // pimpl to keep <sqlite3.h> out of the header
    std::unique_ptr<Sqlite> db_;
    QString sourcePath_;
    std::vector<OswRun> runs_;
    bool hasScoreMs2_{false};
    bool hasFeatureMs2_{false};
    std::vector<QString> ms2SubScoreColumns_;  ///< VAR_* columns present in FEATURE_MS2
  };
}
