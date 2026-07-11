#pragma once

#include "plot/PeakMapRasterizer.h"

#include <OpenMS/KERNEL/MSSpectrum.h>

#include <QImage>
#include <QSize>

#include <vector>

namespace OpenMSViewer
{
  struct IonMobilityRange
  {
    double mzMin{0.0};
    double mzMax{1.0};
    double mobilityMin{0.0};
    double mobilityMax{1.0};

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] double mzSpan() const noexcept;
    [[nodiscard]] double mobilitySpan() const noexcept;
    [[nodiscard]] IonMobilityRange normalized() const noexcept;
    [[nodiscard]] IonMobilityRange clampedTo(const IonMobilityRange& bounds) const noexcept;
  };

  struct IonMobilityRaster
  {
    QImage image;
    std::vector<float> mobilogram;
    float maximumIntensity{0.0F};
  };

  class IonMobilityRasterizer
  {
  public:
    [[nodiscard]] static IonMobilityRaster render(
      const OpenMS::MSSpectrum& spectrum,
      const IonMobilityRange& range,
      QSize size,
      PeakMapColorMap colorMap = PeakMapColorMap::Viridis);
  };
}
