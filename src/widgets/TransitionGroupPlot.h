#pragma once

#include "model/ChromatogramSource.h"
#include "model/OswStore.h"

#include <QPoint>
#include <QWidget>

#include <optional>
#include <vector>

namespace OpenMSViewer
{
  // The canonical OpenSWATH peak-group view: N transition XICs on one shared RT
  // axis, with the selected detected peak group's RT boundaries shaded, its apex
  // and the library RT marked, and other candidate peak groups drawn faintly.
  // MS1/precursor traces are styled distinctly from fragment traces.
  class TransitionGroupPlot final : public QWidget
  {
    Q_OBJECT

  public:
    explicit TransitionGroupPlot(QWidget* parent = nullptr);

    void setData(std::vector<TransitionChromatogram> transitions,
                 std::vector<OswPeakGroup> peakGroups,
                 int selectedPeakGroup,
                 double libraryRt);
    void setShowAllTransitions(bool showAll);
    void setSmoothing(bool smooth);
    void setRtInMinutes(bool minutes);
    void clear();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    [[nodiscard]] QRectF plotRect() const;
    // Fragment traces sorted by peak intensity (top-N unless "show all"), with all
    // MS1/precursor traces always kept.
    [[nodiscard]] std::vector<const TransitionChromatogram*> visibleTransitions() const;
    // The intensities to draw for a transition: raw, or the cached Savitzky-Golay
    // smoothing when the smoothing toggle is on.
    [[nodiscard]] const std::vector<double>& displayIntensity(
      const TransitionChromatogram* transition) const;
    void rebuildSmoothing();

    std::vector<TransitionChromatogram> transitions_;
    std::vector<std::vector<double>> smoothed_;  // parallel to transitions_, when smoothing on
    std::vector<OswPeakGroup> peakGroups_;
    int selectedPeakGroup_{-1};
    double libraryRt_{0.0};
    // True once a precursor's transitions have been handed in via setData(), so an
    // empty plot can distinguish "no precursor selected" from "fetch returned no data".
    bool dataRequested_{false};
    std::optional<QPoint> hoverPos_;  // last cursor position, for the RT/intensity chip
    bool showAll_{false};
    bool smooth_{false};
    bool rtInMinutes_{false};
    int topFragments_{6};
  };
}
