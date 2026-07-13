#pragma once

#include <QString>

// Shared retention-time unit formatting so every plot axis, table column, and
// readout renders RT identically when the "RT in minutes" view option is toggled.
// RT is stored in seconds everywhere; these helpers convert only for display.
namespace OpenMSViewer::RtUnit
{
  /// Divisor to convert seconds → the display unit.
  [[nodiscard]] inline double scale(bool minutes) { return minutes ? 60.0 : 1.0; }

  /// Short unit suffix ("min" / "s").
  [[nodiscard]] inline QString unit(bool minutes)
  {
    return minutes ? QStringLiteral("min") : QStringLiteral("s");
  }

  /// Axis title for RT plots.
  [[nodiscard]] inline QString axisTitle(bool minutes)
  {
    return minutes ? QStringLiteral("Retention time (min)")
                   : QStringLiteral("Retention time (s)");
  }

  /// Table column header for an RT column.
  [[nodiscard]] inline QString columnHeader(bool minutes)
  {
    return minutes ? QStringLiteral("RT (min)") : QStringLiteral("RT (s)");
  }

  /// Format an RT (given in seconds) for display. Minutes get one extra decimal
  /// so the finer scale stays readable.
  [[nodiscard]] inline QString format(double rtSeconds, bool minutes, int secondsDecimals = 2)
  {
    return QString::number(rtSeconds / scale(minutes), 'f', minutes ? secondsDecimals + 1 : secondsDecimals);
  }
}
