#pragma once

#include <QWidget>

class QVBoxLayout;

namespace OpenMSViewer
{
  // Severity of a transient notification; drives the toast's accent colour.
  enum class ToastLevel
  {
    Info,
    Success,
    Warning,
    Error
  };

  // A lightweight, non-modal notification overlay. Toasts stack in the
  // bottom-right corner of the parent widget, colour-coded by severity, and
  // auto-dismiss after a timeout with a short fade. The overlay and its cards
  // are transparent to mouse events, so they never block the plot underneath.
  class ToastOverlay final : public QWidget
  {
    Q_OBJECT

  public:
    explicit ToastOverlay(QWidget* parent = nullptr);

    void showToast(const QString& message, ToastLevel level = ToastLevel::Info,
                   int durationMs = 4500);
    void clearToasts();
    [[nodiscard]] int activeToastCount() const;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void reposition();
    void dismiss(QWidget* card);

    QVBoxLayout* stack_{nullptr};
  };
}
