#include "annotation/SpectrumAnnotation.h"

#include <OpenMS/CHEMISTRY/AASequence.h>
#include <OpenMS/CHEMISTRY/TheoreticalSpectrumGenerator.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenMSViewer
{
  namespace
  {
    std::optional<std::size_t> nearestPeak(const OpenMS::MSSpectrum& spectrum,
                                           double mz, double tolerance)
    {
      if (spectrum.empty() || !std::isfinite(mz)) return std::nullopt;
      const auto upper = std::lower_bound(spectrum.begin(), spectrum.end(), mz,
        [](const OpenMS::Peak1D& peak, double value) { return peak.getMZ() < value; });
      auto best = spectrum.end();
      if (upper != spectrum.end()) best = upper;
      if (upper != spectrum.begin())
      {
        const auto previous = upper - 1;
        if (best == spectrum.end() || std::abs(previous->getMZ() - mz) < std::abs(best->getMZ() - mz))
          best = previous;
      }
      if (best == spectrum.end() || std::abs(best->getMZ() - mz) > tolerance) return std::nullopt;
      return static_cast<std::size_t>(std::distance(spectrum.begin(), best));
    }

    std::vector<TheoreticalIon> generateTheoreticalIons(const PeptideHitRecord& hit)
    {
      OpenMS::TheoreticalSpectrumGenerator generator;
      OpenMS::Param parameters = generator.getParameters();
      parameters.setValue("add_a_ions", "true");
      parameters.setValue("add_b_ions", "true");
      parameters.setValue("add_y_ions", "true");
      parameters.setValue("add_c_ions", "false");
      parameters.setValue("add_x_ions", "false");
      parameters.setValue("add_z_ions", "false");
      parameters.setValue("add_metainfo", "true");
      generator.setParameters(parameters);

      OpenMS::MSSpectrum spectrum;
      const OpenMS::AASequence sequence = OpenMS::AASequence::fromString(hit.sequence.toStdString());
      const int maximumCharge = std::clamp(std::abs(hit.charge), 1, 2);
      generator.getSpectrum(spectrum, sequence, 1, maximumCharge);

      const OpenMS::DataArrays::StringDataArray* names = nullptr;
      for (const auto& array : spectrum.getStringDataArrays())
      {
        if (array.getName() == "IonNames")
        {
          names = &array;
          break;
        }
      }
      if (!names && !spectrum.getStringDataArrays().empty()) names = &spectrum.getStringDataArrays().front();

      std::vector<TheoreticalIon> result;
      result.reserve(spectrum.size());
      for (std::size_t index = 0; index < spectrum.size(); ++index)
      {
        const QString name = names && index < names->size()
                               ? QString::fromStdString((*names)[index]) : QString{};
        if (name.isEmpty()) continue;
        result.push_back({spectrum[index].getMZ(),
                          std::max(1.0, static_cast<double>(spectrum[index].getIntensity())),
                          name, classifyIon(name)});
      }
      return result;
    }
  }

  IonType classifyIon(const QString& name) noexcept
  {
    const QString lower = name.trimmed().toLower();
    if (lower.startsWith(QLatin1Char('a')) || lower.contains(QStringLiteral("$a"))) return IonType::A;
    if (lower.startsWith(QLatin1Char('b')) || lower.contains(QStringLiteral("$b"))) return IonType::B;
    if (lower.startsWith(QLatin1Char('c')) || lower.contains(QStringLiteral("$c"))) return IonType::C;
    if (lower.startsWith(QLatin1Char('x')) || lower.contains(QStringLiteral("$x"))) return IonType::X;
    if (lower.startsWith(QLatin1Char('y')) || lower.contains(QStringLiteral("$y"))) return IonType::Y;
    if (lower.startsWith(QLatin1Char('z')) || lower.contains(QStringLiteral("$z"))) return IonType::Z;
    if (lower.contains(QStringLiteral("precursor")) || lower.contains(QStringLiteral("[m")))
      return IonType::Precursor;
    return IonType::Unknown;
  }

  QString formatIonLabel(const QString& name)
  {
    if (name.isEmpty()) return name;
    static const QString subscriptDigits = QStringLiteral("₀₁₂₃₄₅₆₇₈₉");
    static const QString superscriptDigits = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");
    auto subscript = [&](QChar digit) { return subscriptDigits.at(digit.digitValue()); };
    auto superscript = [&](QChar digit) { return superscriptDigits.at(digit.digitValue()); };

    // Precursor / adduct labels (e.g. "[M+2H]2+") keep their bracketed core verbatim
    // — the digits inside are stoichiometry — and only superscript the trailing charge.
    if (name.startsWith(QLatin1Char('[')))
    {
      const int close = name.lastIndexOf(QLatin1Char(']'));
      if (close < 0) return name;
      // The trailing charge may be written as "2+", "+2", "+", or "++"; parse the
      // sign and magnitude order-independently so it renders like the fragment-ion
      // path (superscript magnitude then sign, magnitude 1 hidden).
      const QString suffix = name.mid(close + 1);
      QChar sign;
      QString digits;
      int signCount = 0;
      bool onlyChargeChars = !suffix.isEmpty();
      for (const QChar character : suffix)
      {
        if (character == QLatin1Char('+') || character == QLatin1Char('-'))
        {
          sign = character;
          ++signCount;
        }
        else if (character.isDigit())
          digits += character;
        else
          onlyChargeChars = false;
      }
      // Not a clean charge token (e.g. "[alpha|ci$y3]") — keep the label verbatim.
      if (!onlyChargeChars || signCount == 0) return name;
      const QString magnitude = !digits.isEmpty() ? digits
        : (signCount > 1 ? QString::number(signCount) : QString());
      QString charge;
      for (const QChar digit : magnitude) charge += superscript(digit);
      charge += sign == QLatin1Char('+') ? QStringLiteral("⁺") : QStringLiteral("⁻");
      return name.left(close + 1) + charge;
    }

    QString core = name;
    QString charge;
    int suffixStart = core.size();
    while (suffixStart > 0 && (core.at(suffixStart - 1) == QLatin1Char('+')
                               || core.at(suffixStart - 1) == QLatin1Char('-')))
      --suffixStart;
    if (suffixStart < core.size())
    {
      const QString signs = core.mid(suffixStart);
      core.truncate(suffixStart);
      if (signs.size() > 1)
        for (QChar digit : QString::number(signs.size())) charge += superscript(digit);
      charge += signs.front() == QLatin1Char('+') ? QStringLiteral("⁺") : QStringLiteral("⁻");
    }
    else
    {
      int digitStart = core.size();
      while (digitStart > 0 && core.at(digitStart - 1).isDigit()) --digitStart;
      if (digitStart > 0 && digitStart < core.size()
          && (core.at(digitStart - 1) == QLatin1Char('+') || core.at(digitStart - 1) == QLatin1Char('-')))
      {
        const QChar sign = core.at(digitStart - 1);
        const QString digits = core.mid(digitStart);
        core.truncate(digitStart - 1);
        for (QChar digit : digits) charge += superscript(digit);
        charge += sign == QLatin1Char('+') ? QStringLiteral("⁺") : QStringLiteral("⁻");
      }
    }

    QString formatted;
    for (int index = 0; index < core.size(); ++index)
    {
      const QChar character = core.at(index);
      if (character.isDigit())
      {
        // Fragment ordinals and digits in neutral-loss formulae are both
        // conventionally typeset as subscripts.
        formatted += subscript(character);
      }
      else
      {
        formatted += character;
      }
    }
    return formatted + charge;
  }

  SpectrumAnnotation computeSpectrumAnnotation(const OpenMS::MSSpectrum& experimental,
                                               const PeptideHitRecord& hit,
                                               double toleranceDa)
  {
    SpectrumAnnotation result;
    result.sequence = hit.sequence;
    result.charge = hit.charge;
    result.toleranceDa = toleranceDa;
    if (experimental.empty() || hit.sequence.isEmpty() || toleranceDa <= 0.0) return result;

    try
    {
      std::vector<TheoreticalIon> theoretical = generateTheoreticalIons(hit);
      result.theoreticalCount = theoretical.size();
      std::vector<bool> theoreticalMatched(theoretical.size(), false);

      if (!hit.peakAnnotations.empty())
      {
        for (const PeakAnnotationRecord& annotation : hit.peakAnnotations)
        {
          const auto peakIndex = nearestPeak(experimental, annotation.mz, toleranceDa);
          if (!peakIndex) continue;
          const auto& peak = experimental[*peakIndex];
          double theoreticalMz = annotation.mz;
          double theoreticalIntensity = std::max(1.0, annotation.intensity);
          std::size_t bestTheoretical = theoretical.size();
          double bestDistance = toleranceDa;
          for (std::size_t index = 0; index < theoretical.size(); ++index)
          {
            const double distance = std::abs(theoretical[index].mz - peak.getMZ());
            if (distance <= bestDistance)
            {
              bestDistance = distance;
              bestTheoretical = index;
            }
          }
          if (bestTheoretical < theoretical.size())
          {
            theoreticalMatched[bestTheoretical] = true;
            theoreticalMz = theoretical[bestTheoretical].mz;
            theoreticalIntensity = theoretical[bestTheoretical].intensity;
          }
          result.matched.push_back({*peakIndex, peak.getMZ(), peak.getIntensity(), theoreticalMz,
                                    theoreticalIntensity, peak.getMZ() - theoreticalMz,
                                    annotation.annotation, classifyIon(annotation.annotation), true});
        }
      }
      else
      {
        for (std::size_t theoreticalIndex = 0; theoreticalIndex < theoretical.size(); ++theoreticalIndex)
        {
          const TheoreticalIon& ion = theoretical[theoreticalIndex];
          const auto peakIndex = nearestPeak(experimental, ion.mz, toleranceDa);
          if (!peakIndex) continue;
          const auto& peak = experimental[*peakIndex];
          theoreticalMatched[theoreticalIndex] = true;
          result.matched.push_back({*peakIndex, peak.getMZ(), peak.getIntensity(), ion.mz,
                                    ion.intensity, peak.getMZ() - ion.mz, ion.name, ion.type, false});
        }
      }

      for (std::size_t index = 0; index < theoretical.size(); ++index)
        if (!theoreticalMatched[index]) result.unmatched.push_back(theoretical[index]);

      std::sort(result.matched.begin(), result.matched.end(), [](const MatchedIon& left, const MatchedIon& right)
      {
        return left.experimentalMz < right.experimentalMz;
      });
      result.coverage = result.theoreticalCount == 0 ? 0.0
        : std::min(1.0, static_cast<double>(result.matched.size()) / result.theoreticalCount);
    }
    catch (const std::exception& error)
    {
      result.error = QString::fromLocal8Bit(error.what());
    }
    catch (...)
    {
      result.error = QStringLiteral("Unknown theoretical-spectrum error");
    }
    return result;
  }
}
