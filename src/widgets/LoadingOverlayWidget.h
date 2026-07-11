#pragma once

#include <QFrame>

class QLabel;
class QProgressBar;
class QPushButton;

namespace OpenMSViewer
{
  class LoadingOverlayWidget final : public QFrame
  {
    Q_OBJECT

  public:
    explicit LoadingOverlayWidget(QWidget* parent);

    void begin(const QString& title, const QString& detail, bool cancellable = true);
    void setProgress(const QString& phase, int percent);
    void setIndeterminate(const QString& phase);
    void setElapsed(qint64 milliseconds);
    void finish();

  signals:
    void cancelRequested();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void reposition();

    QLabel* title_{nullptr};
    QLabel* detail_{nullptr};
    QLabel* elapsed_{nullptr};
    QProgressBar* progress_{nullptr};
    QPushButton* cancel_{nullptr};
  };
}
