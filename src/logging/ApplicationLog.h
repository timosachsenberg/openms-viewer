#pragma once

#include <QObject>
#include <QString>

namespace OpenMSViewer
{
  class ApplicationLog final : public QObject
  {
    Q_OBJECT

  public:
    static ApplicationLog& instance();
    static void install();

  signals:
    void messageLogged(int severity, const QString& message);

  private:
    ApplicationLog() = default;
  };
}
