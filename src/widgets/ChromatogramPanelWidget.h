#pragma once

#include "model/RunData.h"
#include "plot/PlotRange.h"

#include <QPoint>
#include <QWidget>

#include <cstddef>
#include <optional>
#include <vector>

class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QTableView;

namespace OpenMSViewer
{
  class ChromatogramTableModel;

  class ChromatogramPlotWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit ChromatogramPlotWidget(QWidget* parent = nullptr);

    void setChromatograms(const std::vector<ChromatogramRecord>& chromatograms);
    void setSelectedIndices(const std::vector<std::size_t>& indices);
    void setPeakMapRange(const PlotRange& range);
    void setRtInMinutes(bool minutes);
    void setSmoothing(bool smooth);
    [[nodiscard]] const std::vector<std::size_t>& selectedIndices() const noexcept;
    [[nodiscard]] std::optional<std::pair<double, double>> peakMapRtRange() const noexcept;

  signals:
    void rtActivated(double rt);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    [[nodiscard]] QRectF plotRect() const;
    [[nodiscard]] std::optional<std::pair<double, double>> selectedRtBounds() const;
    void rebuildSmoothing();

    std::vector<ChromatogramRecord> chromatograms_;
    std::vector<std::vector<double>> smoothed_;  // parallel to chromatograms_, when smoothing on
    std::vector<std::size_t> selectedIndices_;
    std::optional<std::pair<double, double>> peakMapRtRange_;
    std::optional<QPoint> hoverPos_;
    bool rtInMinutes_{false};
    bool smooth_{false};
  };

  class ChromatogramPanelWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit ChromatogramPanelWidget(QWidget* parent = nullptr);

    void setChromatograms(const std::vector<ChromatogramRecord>& chromatograms);
    void clear();
    void setPeakMapRange(const PlotRange& range);
    [[nodiscard]] std::size_t selectedChromatogramCount() const noexcept;
    [[nodiscard]] ChromatogramPlotWidget* plot() const noexcept;

  signals:
    void rtActivated(double rt);

  private slots:
    void updateSelection();
    void clearSelection();
    void exportTsv();

  private:
    void updateCountLabel();

    ChromatogramTableModel* model_{nullptr};
    QSortFilterProxyModel* proxy_{nullptr};
    QTableView* table_{nullptr};
    QLineEdit* search_{nullptr};
    ChromatogramPlotWidget* plot_{nullptr};
    QLabel* countLabel_{nullptr};
  };
}
