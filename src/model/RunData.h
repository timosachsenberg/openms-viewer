#pragma once

#include <QString>

#include <cstddef>
#include <optional>
#include <vector>

namespace OpenMSViewer
{
  struct SpectrumRecord
  {
    std::size_t index{0};
    double rt{0.0};
    unsigned int msLevel{0};
    QString nativeId;
    std::size_t peakCount{0};
    double tic{0.0};
    double basePeakIntensity{0.0};
    double mzMin{0.0};
    double mzMax{0.0};
    std::optional<double> compensationVoltage;
    std::optional<double> precursorMz;
    int precursorCharge{0};
    double isolationLowerOffset{0.0};  ///< precursorMz - offset = isolation window low m/z
    double isolationUpperOffset{0.0};  ///< precursorMz + offset = isolation window high m/z
    std::optional<std::size_t> identificationIndex;
  };

  struct ChromatogramPoint
  {
    double rt{0.0};
    double intensity{0.0};
  };

  struct ChromatogramRecord
  {
    std::size_t index{0};
    QString nativeId;
    QString type;
    bool isTic{false};
    std::optional<double> precursorMz;
    int precursorCharge{0};
    std::optional<double> productMz;
    double rtMin{0.0};
    double rtMax{0.0};
    double maximumIntensity{0.0};
    double totalIntensity{0.0};
    std::vector<ChromatogramPoint> points;
  };

  struct IonMobilityFrameRecord
  {
    std::size_t spectrumIndex{0};
    double rt{0.0};
    unsigned int msLevel{0};
    QString unit;
    double mobilityMin{0.0};
    double mobilityMax{0.0};
    double mzMin{0.0};
    double mzMax{0.0};
    std::optional<double> precursorMz;
    std::optional<double> isolationWindowLower;
    std::optional<double> isolationWindowUpper;
  };

  struct FaimsTracePoint
  {
    double rt{0.0};
    double intensity{0.0};
    std::size_t spectrumIndex{0};
  };

  struct FaimsChannelRecord
  {
    double compensationVoltage{0.0};
    std::vector<FaimsTracePoint> tic;
    double totalIntensity{0.0};
  };
}
