#pragma once

#include "model/ImagingDocument.h"

#include <QColor>
#include <QFutureWatcher>
#include <QImage>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QMouseEvent;
class QPushButton;
class QWheelEvent;

namespace OpenMSViewer
{
  struct ImagingImageResult
  {
    std::vector<double> intensities;
    std::vector<bool> mask;
    std::uint32_t width{0};
    std::uint32_t height{0};
    double mz{0.0};
    double tolerancePpm{0.0};
    QString error;
  };

  class ImagingImageWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit ImagingImageWidget(QWidget* parent = nullptr);

    void setGeometry(const ImagingSummary& summary);
    void setImage(const std::vector<double>& intensities,
                  const std::vector<bool>& mask,
                  const QString& title);
    void setCompositeImage(QImage image,
                           const std::vector<double>& intensities,
                           const std::vector<bool>& mask,
                           const QString& title,
                           std::vector<std::pair<QColor, QString>> legend);
    void clear();
    void setSelectedSpectrum(std::optional<std::size_t> spectrumIndex);
    [[nodiscard]] const QImage& renderedImage() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedSpectrum() const noexcept;

  signals:
    void pixelActivated(std::size_t spectrumIndex, std::uint32_t x, std::uint32_t y);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

  private:
    [[nodiscard]] QRect imageRect() const;
    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> pixelAt(
      const QPointF& position) const;
    void rebuildImage();

    ImagingSummary summary_;
    std::vector<double> intensities_;
    std::vector<bool> mask_;
    std::vector<std::optional<std::size_t>> spectrumByPixel_;
    QString title_;
    QImage image_;
    bool composite_{false};
    double displayMax_{0.0};   // robust (99th-percentile) intensity for the colorbar
    std::vector<std::pair<QColor, QString>> legend_;   // overlay channel colour + m/z
    std::optional<std::size_t> selectedSpectrum_;
  };

  // Whole-image mean/skyline spectrum with click-to-browse: clicking a peak sets
  // the ion-image m/z and extracts it, which is how MSI data is explored.
  class AggregateSpectrumWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit AggregateSpectrumWidget(QWidget* parent = nullptr);

    void setSpectrum(std::vector<double> mz, std::vector<double> intensity, QString title,
                     bool keepView = false);
    void setMarkerMz(std::optional<double> mz);
    void clear();

  signals:
    void peakSelected(double mz);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    [[nodiscard]] QRect plotRect() const;
    [[nodiscard]] std::pair<double, double> viewRange() const;
    [[nodiscard]] std::optional<std::size_t> peakAt(double x) const;

    std::vector<double> mz_;          // occupied-bin centres, ascending
    std::vector<double> intensity_;
    QString title_;
    double intensityMax_{0.0};
    std::optional<std::pair<double, double>> view_;   // zoomed m/z range
    std::optional<double> markerMz_;                  // the currently-imaged m/z
    std::optional<QPoint> hoverPos_;
  };

  class ImagingPanelWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit ImagingPanelWidget(QWidget* parent = nullptr);
    ~ImagingPanelWidget() override;

    void setData(std::shared_ptr<ImagingStore> store, const ImagingSummary& summary);
    void clear();
    void setSelectedSpectrum(std::optional<std::size_t> spectrumIndex);
    [[nodiscard]] bool hasData() const noexcept;
    [[nodiscard]] const ImagingSummary& summary() const noexcept;
    [[nodiscard]] ImagingImageWidget* imageWidget() const noexcept;
    [[nodiscard]] std::size_t overlayCount() const noexcept;

  signals:
    void spectrumActivated(std::size_t spectrumIndex);

  private slots:
    void extractIonImage();
    void finishExtraction();
    void finishAggregate();
    void addOverlay();
    void clearOverlay();

  private:
    void showTicImage();
    void showOverlay();
    void updateControls();
    void launchAggregate();
    void updateAggregateDisplay(bool keepView);
    void browseToPeak(double mz);

    struct OverlayEntry
    {
      std::vector<double> intensities;
      std::vector<bool> mask;
      double mz{0.0};
      double tolerancePpm{0.0};
    };

    std::shared_ptr<ImagingStore> store_;
    ImagingSummary summary_;
    std::vector<double> ticImage_;
    std::vector<bool> mask_;
    std::optional<ImagingImageResult> currentIonImage_;
    std::vector<OverlayEntry> overlays_;
    QComboBox* displayMode_{nullptr};
    QDoubleSpinBox* mz_{nullptr};
    QDoubleSpinBox* tolerance_{nullptr};
    QPushButton* extract_{nullptr};
    QPushButton* addOverlay_{nullptr};
    QPushButton* clearOverlay_{nullptr};
    QLabel* info_{nullptr};
    ImagingImageWidget* image_{nullptr};
    AggregateSpectrumWidget* aggregate_{nullptr};
    QComboBox* aggregateMode_{nullptr};    // Mean vs Max (skyline)
    AggregateSpectrum aggregateData_;
    QFutureWatcher<ImagingImageResult> extractionWatcher_;
    QFutureWatcher<AggregateSpectrum> aggregateWatcher_;
    std::uint64_t dataGeneration_{0};      // bumped whenever the dataset changes
    std::uint64_t activeExtraction_{0};    // dataGeneration_ the in-flight extraction was launched for
    std::uint64_t activeAggregate_{0};     // dataGeneration_ the in-flight aggregate was launched for
    bool extractionPending_{false};        // a click arrived while an extraction was running
  };
}
