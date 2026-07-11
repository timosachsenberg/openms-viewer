#pragma once

#include "model/ViewerDocument.h"
#include "plot/PlotRange.h"

#include <QWidget>

#include <cstddef>
#include <optional>
#include <vector>

class QKeyEvent;

namespace OpenMSViewer
{
  class TicWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit TicWidget(QWidget* parent = nullptr);

    void setTrace(std::vector<TicPoint> points, QString label);
    void setSelectedSpectrum(std::size_t spectrumIndex);
    void setSelectedRt(double rt);
    void setPeakMapRange(const PlotRange& range);
    void clear();
    [[nodiscard]] const std::optional<double>& selectedRt() const noexcept;

  public slots:
    void resetView();

  signals:
    void spectrumActivated(std::size_t spectrumIndex);
    void rtRangeSelected(double minimumRt, double maximumRt);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    [[nodiscard]] QRect plotRect() const;
    [[nodiscard]] std::optional<std::size_t> pointAtX(double x) const;

    std::vector<TicPoint> points_;
    QString label_;
    std::optional<std::size_t> selectedSpectrum_;
    std::optional<double> selectedRt_;
    PlotRange peakMapRange_;
    bool hasPeakMapRange_{false};
    bool dragging_{false};
    QPoint dragStart_;
    QPoint dragCurrent_;
  };
}
