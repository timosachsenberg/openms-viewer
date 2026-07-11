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

  struct IdentificationRecord
  {
    std::size_t index{0};
    double rt{0.0};
    double mz{0.0};
    QString scoreType;
    bool higherScoreBetter{true};
    QString identifier;
    MetaValues metaValues;
    std::vector<PeptideHitRecord> hits;
    std::optional<std::size_t> spectrumIndex;
    double linkRtError{0.0};
    double linkMzError{0.0};

    [[nodiscard]] const PeptideHitRecord* bestHit() const noexcept
    {
      return hits.empty() ? nullptr : &hits.front();
    }
  };
}

