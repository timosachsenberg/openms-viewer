#include "logging/ApplicationLog.h"
#include "widgets/LogWidget.h"

#include <QComboBox>
#include <QLineEdit>
#include <QTest>
#include <QToolButton>

class LogWidgetTest final : public QObject
{
  Q_OBJECT

private slots:
  void capturesFiltersAndClearsMessages()
  {
    OpenMSViewer::ApplicationLog::install();
    OpenMSViewer::LogWidget widget;
    widget.resize(700, 300);
    widget.show();
    qInfo().noquote() << "log-widget-info-unique";
    qWarning().noquote() << "log-widget-warning-unique";
    QTRY_VERIFY_WITH_TIMEOUT(widget.text().contains(QStringLiteral("log-widget-info-unique")), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(widget.text().contains(QStringLiteral("log-widget-warning-unique")), 2000);
    QVERIFY(widget.messageCount() >= 2);

    auto* severity = widget.findChild<QComboBox*>(QStringLiteral("logSeverity"));
    auto* search = widget.findChild<QLineEdit*>(QStringLiteral("logSearch"));
    auto* clear = widget.findChild<QToolButton*>(QStringLiteral("logClear"));
    auto* copy = widget.findChild<QToolButton*>(QStringLiteral("logCopyAll"));
    auto* save = widget.findChild<QToolButton*>(QStringLiteral("logSave"));
    QVERIFY(severity != nullptr);
    QVERIFY(search != nullptr);
    for (QToolButton* button : {clear, copy, save})
    {
      QVERIFY(button != nullptr);
      QVERIFY(!button->icon().isNull());
      QVERIFY(!button->toolTip().isEmpty());
    }
    severity->setCurrentIndex(2);
    QVERIFY(!widget.text().contains(QStringLiteral("log-widget-info-unique")));
    QVERIFY(widget.text().contains(QStringLiteral("log-widget-warning-unique")));
    search->setText(QStringLiteral("does-not-match"));
    QCOMPARE(widget.text(), QString());
  }
};

int runLogWidgetTests(int argc, char** argv)
{
  LogWidgetTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "LogWidgetTest.moc"
