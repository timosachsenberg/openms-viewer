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

    void setSpectrum(std::optional<std::size_t> index);
    void setFeature(std::optional<std::size_t> index);
    void setIdentification(std::optional<std::size_t> index,
                           std::optional<std::size_t> hitIndex = std::nullopt);
    void setFaimsChannel(int channelIndex);
    void clear();

  signals:
    void spectrumChanged(qint64 index);
    void featureChanged(qint64 index);
    void identificationChanged(qint64 identificationIndex, qint64 hitIndex);
    void faimsChannelChanged(int channelIndex);

  private:
    std::optional<std::size_t> spectrum_;
    std::optional<std::size_t> feature_;
    std::optional<std::size_t> identification_;
    std::optional<std::size_t> hit_;
    int faimsChannel_{-1};
  };
}
