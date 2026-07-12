#include "model/ChromatogramSource.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// The .xic reader itself is exercised end-to-end against a real fixture in the
// preview harness; here we lock in graceful failure for the bad-input paths.
class ChromatogramSourceTest final : public QObject
{
  Q_OBJECT

private slots:
  void reportsMissingAndInvalidInputs()
  {
    QString error;
    QVERIFY(OpenMSViewer::XicChromatogramSource::open({}, error) == nullptr);
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(OpenMSViewer::XicChromatogramSource::open(
              {QStringLiteral("/definitely/not/a/real/file.xic")}, error) == nullptr);
    QVERIFY(error.contains(QStringLiteral("not found")));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bogus = dir.filePath(QStringLiteral("bogus.xic"));
    QFile file(bogus);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not a parquet file");
    file.close();
    error.clear();
    QVERIFY(OpenMSViewer::XicChromatogramSource::open({bogus}, error) == nullptr);
    QVERIFY(!error.isEmpty());
  }
};

int runChromatogramSourceTests(int argc, char** argv)
{
  ChromatogramSourceTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "ChromatogramSourceTest.moc"
