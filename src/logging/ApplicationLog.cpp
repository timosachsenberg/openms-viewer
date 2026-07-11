#include "logging/ApplicationLog.h"

#include <QMetaObject>
#include <QtGlobal>

namespace OpenMSViewer
{
  namespace
  {
    QtMessageHandler previousHandler = nullptr;

    void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
      if (previousHandler) previousHandler(type, context, message);
      QMetaObject::invokeMethod(&ApplicationLog::instance(),
        [type, message] { emit ApplicationLog::instance().messageLogged(static_cast<int>(type), message); },
        Qt::QueuedConnection);
    }
  }

  ApplicationLog& ApplicationLog::instance()
  {
    static ApplicationLog log;
    return log;
  }

  void ApplicationLog::install()
  {
    const QtMessageHandler previous = qInstallMessageHandler(messageHandler);
    if (previous != messageHandler) previousHandler = previous;
  }
}
