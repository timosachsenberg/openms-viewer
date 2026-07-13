#pragma once

#include "model/RunData.h"
#include "plot/PeakMapRasterizer.h"
#include "plot/PlotRange.h"

#include <OpenMS/KERNEL/MSExperiment.h>

#include <QFutureWatcher>
#include <QImage>
#include <QTimer>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <vector>

class QComboBox;
class QMouseEvent;
class QPaintEvent;
class QTableWidget;

namespace OpenMSViewer
{
  class FaimsPeakMapsWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit FaimsPeakMapsWidget(QWidget* parent = nullptr);
    ~FaimsPeakMapsWidget() override;

    void setData(const std::vector<std::shared_ptr<const OpenMS::MSExperiment>>& experiments,
                 const std::vector<FaimsChannelRecord>& channels,
                 const PlotRange& bounds);
    void setViewRange(const PlotRange& range);
    void setSelectedChannel(int channelIndex);
    void setColorMap(PeakMapColorMap colorMap);
    [[nodiscard]] const std::vector<QImage>& images() const noexcept;

  signals:
    void channelActivated(int channelIndex);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

  private:
    [[nodiscard]] QRect cellRect(std::size_t index) const;
    void scheduleRender();
    void startRender();

    std::vector<std::shared_ptr<const OpenMS::MSExperiment>> experiments_;
    std::vector<FaimsChannelRecord> channels_;
    PlotRange bounds_;
    PlotRange view_;
    std::vector<QImage> images_;
    PeakMapColorMap colorMap_{PeakMapColorMap::Viridis};
    int selectedChannel_{-1};
    std::uint64_t desiredGeneration_{0};
    std::uint64_t activeGeneration_{0};
    QTimer renderTimer_;
    QFutureWatcher<std::vector<QImage>> renderWatcher_;
  };

  class FaimsTracePlotWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit FaimsTracePlotWidget(QWidget* parent = nullptr);

    void setChannels(const std::vector<FaimsChannelRecord>& channels);
    void setSelectedChannel(int channelIndex);
    void setPeakMapRange(const PlotRange& range);
    void setRtInMinutes(bool minutes);
    [[nodiscard]] int selectedChannel() const noexcept;

  signals:
    void spectrumActivated(std::size_t spectrumIndex);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

  private:
    [[nodiscard]] QRect plotRect() const;

    std::vector<FaimsChannelRecord> channels_;
    int selectedChannel_{-1};
    PlotRange peakMapRange_;
    bool hasPeakMapRange_{false};
    bool rtInMinutes_{false};
  };

  class FaimsPanelWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit FaimsPanelWidget(QWidget* parent = nullptr);

    void setChannels(const std::vector<FaimsChannelRecord>& channels);
    void setExperiments(const std::vector<std::shared_ptr<const OpenMS::MSExperiment>>& experiments,
                        const PlotRange& bounds);
    void clear();
    void setPeakMapRange(const PlotRange& range);
    void setSelectedChannel(int channelIndex);
    void setColorMap(int colorMapIndex);
    void setRtInMinutes(bool minutes);
    [[nodiscard]] std::size_t channelCount() const noexcept;
    [[nodiscard]] int selectedChannel() const noexcept;
    [[nodiscard]] FaimsTracePlotWidget* plot() const noexcept;
    [[nodiscard]] FaimsPeakMapsWidget* peakMaps() const noexcept;

  signals:
    void channelSelected(int channelIndex);
    void spectrumActivated(std::size_t spectrumIndex);

  private:
    void applySelection(int selectorIndex, bool emitSignal);
    [[nodiscard]] QString rtRangeText(const FaimsChannelRecord& channel) const;

    std::vector<FaimsChannelRecord> channels_;
    QComboBox* selector_{nullptr};
    QTableWidget* table_{nullptr};
    FaimsTracePlotWidget* plot_{nullptr};
    FaimsPeakMapsWidget* peakMaps_{nullptr};
    bool rtInMinutes_{false};
  };
}
