#pragma once

#include "model/IdentificationData.h"

#include <OpenMS/KERNEL/MSSpectrum.h>

#include <QString>

#include <cstddef>
#include <vector>

namespace OpenMSViewer
{
  enum class IonType { A, B, C, X, Y, Z, Precursor, Unknown };

  struct TheoreticalIon
  {
    double mz{0.0};
    double intensity{1.0};
    QString name;
    IonType type{IonType::Unknown};
  };

  struct MatchedIon
  {
    std::size_t experimentalPeakIndex{0};
    double experimentalMz{0.0};
    double experimentalIntensity{0.0};
    double theoreticalMz{0.0};
    double theoreticalIntensity{1.0};
    double mzError{0.0};
    QString name;
    IonType type{IonType::Unknown};
    bool external{false};
  };

  struct SpectrumAnnotation
  {
    QString sequence;
    int charge{0};
    double toleranceDa{0.05};
    std::vector<MatchedIon> matched;
    std::vector<TheoreticalIon> unmatched;
    std::size_t theoreticalCount{0};
    double coverage{0.0};
    QString error;
  };

  [[nodiscard]] IonType classifyIon(const QString& name) noexcept;
  [[nodiscard]] QString formatIonLabel(const QString& name);
  [[nodiscard]] SpectrumAnnotation computeSpectrumAnnotation(
    const OpenMS::MSSpectrum& experimental,
    const PeptideHitRecord& hit,
    double toleranceDa = 0.05);
}
