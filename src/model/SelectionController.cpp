#include "model/SelectionController.h"

namespace OpenMSViewer
{
  namespace
  {
    qint64 signalIndex(const std::optional<std::size_t>& index)
    {
      return index ? static_cast<qint64>(*index) : qint64{-1};
    }
  }

  SelectionController::SelectionController(QObject* parent) : QObject(parent) {}

  const std::optional<std::size_t>& SelectionController::spectrum() const noexcept
  {
    return spectrum_;
  }

  const std::optional<std::size_t>& SelectionController::feature() const noexcept
  {
    return feature_;
  }

  const std::optional<std::size_t>& SelectionController::identification() const noexcept
  {
    return identification_;
  }

  const std::optional<std::size_t>& SelectionController::hit() const noexcept
  {
    return hit_;
  }

  int SelectionController::faimsChannel() const noexcept { return faimsChannel_; }

  const std::optional<double>& SelectionController::mz() const noexcept { return mz_; }

  void SelectionController::setSpectrum(std::optional<std::size_t> index)
  {
    if (spectrum_ == index) return;
    spectrum_ = index;
    emit spectrumChanged(signalIndex(spectrum_));
  }

  void SelectionController::setFeature(std::optional<std::size_t> index)
  {
    if (feature_ == index) return;
    feature_ = index;
    emit featureChanged(signalIndex(feature_));
  }

  void SelectionController::setIdentification(std::optional<std::size_t> index,
                                               std::optional<std::size_t> hitIndex)
  {
    if (!index) hitIndex.reset();
    if (identification_ == index && hit_ == hitIndex) return;
    identification_ = index;
    hit_ = hitIndex;
    emit identificationChanged(signalIndex(identification_), signalIndex(hit_));
  }

  void SelectionController::setFaimsChannel(int channelIndex)
  {
    if (faimsChannel_ == channelIndex) return;
    faimsChannel_ = channelIndex;
    emit faimsChannelChanged(faimsChannel_);
  }

  void SelectionController::setMz(std::optional<double> value)
  {
    if (mz_ == value) return;
    mz_ = value;
    emit mzChanged(mz_.value_or(0.0), mz_.has_value());
  }

  void SelectionController::clear()
  {
    setSpectrum(std::nullopt);
    setFeature(std::nullopt);
    setIdentification(std::nullopt);
    setFaimsChannel(-1);
    setMz(std::nullopt);
  }
}
