#pragma once

#include "annotation/SpectrumAnnotation.h"

#include <OpenMS/KERNEL/MSExperiment.h>

#include <QWidget>

#include <cstddef>
#include <memory>
#include <optional>
#include <map>
#include <vector>

class QKeyEvent;

namespace OpenMSViewer
{
  struct SpectrumMeasurement
  {
    double firstMz{0.0};
    double firstIntensity{0.0};
    double secondMz{0.0};
    double secondIntensity{0.0};
  };

  struct PeakLabel
  {
    double mz{0.0};
    double intensity{0.0};
    QString text;
  };

  class SpectrumWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit SpectrumWidget(QWidget* parent = nullptr);

    void setExperiment(std::shared_ptr<const OpenMS::MSExperiment> experiment);
    void setSpectrumIndex(std::size_t index);
    void setStandaloneSpectrum(OpenMS::MSSpectrum spectrum, std::size_t index,
                               std::size_t totalSpectra);
    void clear();
    void setRelativeIntensity(bool relative);
    void setAnnotation(std::optional<SpectrumAnnotation> annotation);
    void setAnnotationEnabled(bool enabled);
    void setMirrorMode(bool enabled);
    void setShowUnmatchedTheoretical(bool show);
    void setMeasurementMode(bool enabled);
    void setLabelMode(bool enabled);
    void setShowMzLabels(bool show);
    void setAutoYScale(bool enabled);
    void clearMeasurements();
    void clearLabels();
    void resetMzView();

    [[nodiscard]] std::size_t spectrumIndex() const noexcept;
    [[nodiscard]] const std::optional<SpectrumAnnotation>& annotation() const noexcept;
    [[nodiscard]] bool measurementMode() const noexcept;
    [[nodiscard]] bool labelMode() const noexcept;
    [[nodiscard]] std::optional<std::pair<double, double>> mzView() const noexcept;
    [[nodiscard]] const std::vector<SpectrumMeasurement>& measurements() const noexcept;
    [[nodiscard]] const std::vector<PeakLabel>& labels() const noexcept;

  signals:
    void mzViewChanged(double minimumMz, double maximumMz, bool reset);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    [[nodiscard]] const OpenMS::MSSpectrum* currentSpectrum() const noexcept;
    [[nodiscard]] QRect plotRect() const;
    [[nodiscard]] std::pair<double, double> fullMzRange() const;
    [[nodiscard]] std::optional<std::pair<double, double>> peakAt(const QPointF& position) const;
    [[nodiscard]] std::optional<std::size_t> measurementAt(const QPointF& position) const;
    void editLabelAt(const std::pair<double, double>& peak);
    void applyMzView(double minimumMz, double maximumMz, bool reset = false);

    std::shared_ptr<const OpenMS::MSExperiment> experiment_;
    std::optional<OpenMS::MSSpectrum> standaloneSpectrum_;
    std::size_t spectrumIndex_{0};
    std::size_t totalSpectra_{0};
    bool relativeIntensity_{true};
    std::optional<SpectrumAnnotation> annotation_;
    bool annotationEnabled_{true};
    bool mirrorMode_{false};
    bool showUnmatchedTheoretical_{true};
    bool measurementMode_{false};
    bool labelMode_{false};
    bool showMzLabels_{false};
    bool autoYScale_{true};
    // Vertical scaling captured by the most recent paint so peakAt() can hit-test
    // against exactly what is on screen without rescanning the spectrum per move.
    double plotBaseline_{0.0};
    double plotPositiveHeight_{1.0};
    double plotIntensityMax_{1.0};
    std::optional<std::pair<double, double>> mzView_;
    std::optional<std::pair<double, double>> hoveredPeak_;
    std::optional<std::pair<double, double>> measurementStart_;
    std::map<std::size_t, std::vector<SpectrumMeasurement>> measurements_;
    std::map<std::size_t, std::vector<PeakLabel>> peakLabels_;
    std::optional<std::size_t> selectedMeasurement_;
    bool draggingZoom_{false};
    QPoint dragStart_;
    QPoint dragCurrent_;
  };
}
