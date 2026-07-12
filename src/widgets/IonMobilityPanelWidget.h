#pragma once

#include "model/RunData.h"
#include "plot/IonMobilityRasterizer.h"

#include <OpenMS/KERNEL/MSExperiment.h>

#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QResizeEvent;
class QWheelEvent;

namespace OpenMSViewer
{
  // One diaPASEF isolation window: a box in m/z × ion-mobility space. `group` is the
  // acquisition window group (MS2 frames sharing an RT in the first cycle), used to
  // colour windows acquired together.
  struct DiaWindow
  {
    double mzLow{0.0};
    double mzHigh{0.0};
    double mobilityLow{0.0};
    double mobilityHigh{0.0};
    int group{0};
  };

  class IonMobilityPlotWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit IonMobilityPlotWidget(QWidget* parent = nullptr);
    ~IonMobilityPlotWidget() override;

    void setData(std::shared_ptr<const OpenMS::MSExperiment> experiment,
                 const std::vector<IonMobilityFrameRecord>& frames);
    void setFramePosition(std::optional<std::size_t> position);
    void setShowMobilogram(bool show);
    void setSmoothMobilogram(bool smooth);
    void setShowDiaWindows(bool show);
    [[nodiscard]] bool hasDiaWindows() const noexcept { return !diaWindows_.empty(); }
    [[nodiscard]] std::size_t diaWindowCount() const noexcept { return diaWindows_.size(); }
    void setColorMap(PeakMapColorMap colorMap);
    void resetView();
    void setMzRange(double minimumMz, double maximumMz, bool reset = false);
    [[nodiscard]] std::optional<std::size_t> framePosition() const noexcept;
    [[nodiscard]] const IonMobilityRange& viewRange() const noexcept;
    [[nodiscard]] const IonMobilityRaster& raster() const noexcept;

  signals:
    void renderCompleted();
    void viewRangeChanged(const IonMobilityRange& range);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

  private:
    [[nodiscard]] QRect mainPlotRect() const;
    [[nodiscard]] QRect mobilogramRect() const;
    void scheduleRender();
    void startRender();
    void applyRange(const IonMobilityRange& range);
    void drawAxes(QPainter& painter, const QRect& area);
    void drawMobilogram(QPainter& painter, const QRect& area);
    void drawDiaWindows(QPainter& painter, const QRect& area) const;
    void rebuildDiaWindows();

    std::shared_ptr<const OpenMS::MSExperiment> experiment_;
    std::vector<IonMobilityFrameRecord> frames_;
    std::optional<std::size_t> framePosition_;
    IonMobilityRange bounds_;
    IonMobilityRange view_;
    IonMobilityRaster raster_;
    PeakMapColorMap colorMap_{PeakMapColorMap::Viridis};
    bool showMobilogram_{true};
    bool smoothMobilogram_{false};
    std::vector<DiaWindow> diaWindows_;  // distinct MS2 isolation windows (m/z × IM)
    bool showDiaWindows_{false};
    bool dragging_{false};
    bool draggingMobilogram_{false};
    QPoint dragStart_;
    QPoint dragCurrent_;
    std::size_t desiredGeneration_{0};
    std::size_t activeGeneration_{0};
    QTimer renderTimer_;
    QFutureWatcher<IonMobilityRaster> renderWatcher_;
  };

  class IonMobilityPanelWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit IonMobilityPanelWidget(QWidget* parent = nullptr);

    void setData(std::shared_ptr<const OpenMS::MSExperiment> experiment,
                 const std::vector<IonMobilityFrameRecord>& frames);
    void clear();
    void setSpectrumIndex(std::size_t spectrumIndex);
    void setSpectrumMzRange(double minimumMz, double maximumMz, bool reset);
    void setPeakMapMzRange(double minimumMz, double maximumMz);
    void setColorMap(int colorMapIndex);
    [[nodiscard]] std::size_t frameCount() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedSpectrumIndex() const noexcept;
    [[nodiscard]] IonMobilityPlotWidget* plot() const noexcept;

  signals:
    void spectrumActivated(std::size_t spectrumIndex);

  private:
    void selectPosition(int position, bool activateSpectrum);
    void updateInfo();

    std::shared_ptr<const OpenMS::MSExperiment> experiment_;
    std::vector<IonMobilityFrameRecord> frames_;
    QComboBox* frameSelector_{nullptr};
    QLabel* info_{nullptr};
    QLabel* range_{nullptr};
    QCheckBox* mobilogram_{nullptr};
    QCheckBox* linkMz_{nullptr};
    QPushButton* previous_{nullptr};
    QPushButton* next_{nullptr};
    IonMobilityPlotWidget* plot_{nullptr};
    std::optional<std::pair<double, double>> spectrumMzRange_;
  };
}
