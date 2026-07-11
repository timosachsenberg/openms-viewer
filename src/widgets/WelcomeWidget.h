#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

namespace OpenMSViewer
{
  class WelcomeWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit WelcomeWidget(QWidget* parent = nullptr);

    void setRecentFiles(const QStringList& paths);

  signals:
    void openRequested();
    void recentFileRequested(const QString& path);

  private:
    QListWidget* recentFiles_{nullptr};
    QLabel* recentLabel_{nullptr};
  };
}
