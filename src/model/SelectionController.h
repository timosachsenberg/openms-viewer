#pragma once

#include <QObject>

#include <cstddef>
#include <optional>

namespace OpenMSViewer
{
  class SelectionController final : public QObject
  {
    Q_OBJECT

  public:
    explicit SelectionController(QObject* parent = nullptr);

    [[nodiscard]] const std::optional<std::size_t>& spectrum() const noexcept;
    [[nodiscard]] const std::optional<std::size_t>& feature() const noexcept;
    [[nodiscard]] const std::optional<std::size_t>& identification() const noexcept;
    [[nodiscard]] const std::optional<std::size_t>& hit() const noexcept;
    [[nodiscard]] int faimsChannel() const noexcept;
    // A cross-panel m/z reference (a pinned peak pick), independent of the
    // selected spectrum. Pure coordinate — carries no intensity — so it stays
    // truthful when the selected scan changes. Producers: peak map + spectrum
    // widget picks. Consumers: both plots draw a line at this m/z.
    [[nodiscard]] const std::optional<double>& mz() const noexcept;

    void setSpectrum(std::optional<std::size_t> index);
    void setFeature(std::optional<std::size_t> index);
    void setIdentification(std::optional<std::size_t> index,
                           std::optional<std::size_t> hitIndex = std::nullopt);
    void setFaimsChannel(int channelIndex);
    void setMz(std::optional<double> value);
    void clear();

  signals:
    void spectrumChanged(qint64 index);
    void featureChanged(qint64 index);
    void identificationChanged(qint64 identificationIndex, qint64 hitIndex);
    void faimsChannelChanged(int channelIndex);
    // `valid` is false when the m/z was cleared; `mz` is then unspecified. A
    // bare double cannot express "none", so validity is signalled explicitly.
    void mzChanged(double mz, bool valid);

  private:
    std::optional<std::size_t> spectrum_;
    std::optional<std::size_t> feature_;
    std::optional<std::size_t> identification_;
    std::optional<std::size_t> hit_;
    int faimsChannel_{-1};
    std::optional<double> mz_;
  };
}
