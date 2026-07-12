#pragma once

#include "model/ChromatogramSource.h"
#include "model/OswStore.h"

#include <QWidget>

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
    void clear();

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    [[nodiscard]] QRectF plotRect() const;
    // Fragment traces sorted by peak intensity (top-N unless "show all"), with all
    // MS1/precursor traces always kept.
    [[nodiscard]] std::vector<const TransitionChromatogram*> visibleTransitions() const;

    std::vector<TransitionChromatogram> transitions_;
    std::vector<OswPeakGroup> peakGroups_;
    int selectedPeakGroup_{-1};
    double libraryRt_{0.0};
    bool showAll_{false};
    int topFragments_{6};
  };
}
