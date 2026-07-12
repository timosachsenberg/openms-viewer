#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace OpenMSViewer
{
  class OswStore;

  // One transition's extracted-ion chromatogram (an XIC), with the metadata a
  // peak-group plot needs to label and group it.
  struct TransitionChromatogram
  {
    std::int64_t transitionId{-1};
    std::int64_t precursorId{-1};
    std::int64_t runId{0};
    int msLevel{0};        ///< 1 = precursor/MS1 trace, ≥2 = fragment
    int ordinal{0};
    QString ionType;       ///< b / y / ...
    QString annotation;
    bool detecting{false};
    std::vector<double> rt;
    std::vector<double> intensity;

    [[nodiscard]] bool isMs1() const noexcept { return msLevel == 1; }
  };

  struct ChromatogramRunRef
  {
    std::int64_t runId{0};
    QString sourceFile;
  };

  // Abstract provider of transition XICs for OpenSWATH peak groups. Backends:
  // .xic Parquet (implemented) and, later, sqMass. Worker-thread-affine.
  class ChromatogramSource
  {
  public:
    virtual ~ChromatogramSource() = default;
    [[nodiscard]] virtual std::vector<ChromatogramRunRef> runs() const = 0;
    // All transition chromatograms for a precursor (optionally restricted to one
    // run; runId < 0 = every run).
    [[nodiscard]] virtual std::vector<TransitionChromatogram> fetch(
      std::int64_t precursorId, std::int64_t runId = -1) const = 0;
    [[nodiscard]] virtual QString provenance() const = 0;
  };

  // .xic (OpenSWATH Parquet chromatogram store) backed by OpenMS XICParquetFile,
  // which applies precursor/run filter pushdown before decoding the RT/intensity
  // arrays.
  class XicChromatogramSource final : public ChromatogramSource
  {
  public:
    [[nodiscard]] static std::shared_ptr<XicChromatogramSource> open(
      const QStringList& paths, QString& error);

    [[nodiscard]] std::vector<ChromatogramRunRef> runs() const override { return runs_; }
    [[nodiscard]] std::vector<TransitionChromatogram> fetch(
      std::int64_t precursorId, std::int64_t runId) const override;
    [[nodiscard]] QString provenance() const override { return provenance_; }

  private:
    XicChromatogramSource() = default;
    std::vector<std::string> paths_;
    std::vector<ChromatogramRunRef> runs_;
    QString provenance_;
  };

  // .sqMass (the classic OpenSWATH SQLite chromatogram store) — the common
  // alternative to .xic. sqMass keys chromatograms only by native ID (= the OSW
  // transition ID) and carries no precursor grouping or library annotation, so this
  // source is paired with the OswStore: it builds a native-ID → chromatogram-ID
  // index up-front (metadata only) and decodes just the selected precursor's
  // transitions on demand via OpenMS's indexed reader — never the whole file.
  class SqMassChromatogramSource final : public ChromatogramSource
  {
  public:
    [[nodiscard]] static std::shared_ptr<SqMassChromatogramSource> open(
      const QString& sqMassPath, std::shared_ptr<OswStore> store, QString& error);

    [[nodiscard]] std::vector<ChromatogramRunRef> runs() const override { return runs_; }
    [[nodiscard]] std::vector<TransitionChromatogram> fetch(
      std::int64_t precursorId, std::int64_t runId) const override;
    [[nodiscard]] QString provenance() const override { return provenance_; }

  private:
    SqMassChromatogramSource() = default;
    std::string path_;
    // A PRIVATE OswStore connection (its own sqlite handle), so per-precursor
    // fetches on worker threads never touch the GUI's store — OswStore is
    // worker-thread-affine and not internally synchronized. storeMutex_ serializes
    // the (possibly overlapping) fetches that share this connection.
    std::shared_ptr<OswStore> store_;
    mutable std::mutex storeMutex_;
    std::unordered_map<std::string, int> chromatogramIdByNativeId_;  // NATIVE_ID → CHROMATOGRAM.ID
    std::int64_t runId_{0};
    std::vector<ChromatogramRunRef> runs_;
    QString provenance_;
  };
}
