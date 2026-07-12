#pragma once

#include <QString>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace OpenMSViewer
{
  using MetaValues = std::vector<std::pair<QString, QString>>;

  struct PeakAnnotationRecord
  {
    double mz{-1.0};
    double intensity{0.0};
    int charge{0};
    QString annotation;
  };

  struct PeptideHitRecord
  {
    std::size_t index{0};
    QString sequence;
    double score{0.0};
    int charge{0};
    MetaValues metaValues;
    std::vector<PeakAnnotationRecord> peakAnnotations;
  };

  // How an identification was tied to its spectrum. Native-ID (spectrum_reference)
  // links are exact; RtMz links are an approximate ±5 s / ±0.5 Da window match and
  // are labelled as such in the UI so the distinction is never hidden.
  enum class LinkMode { None, NativeId, RtMz };

  struct IdentificationRecord
  {
    std::size_t index{0};
    double rt{0.0};
    double mz{0.0};
    QString scoreType;
    bool higherScoreBetter{true};
    QString identifier;
    QString spectrumReference;  // native-ID string of the source spectrum, if any
    MetaValues metaValues;
    std::vector<PeptideHitRecord> hits;
    std::optional<std::size_t> spectrumIndex;
    LinkMode linkMode{LinkMode::None};
    double linkRtError{0.0};
    double linkMzError{0.0};

    [[nodiscard]] const PeptideHitRecord* bestHit() const noexcept
    {
      return hits.empty() ? nullptr : &hits.front();
    }
  };
}

