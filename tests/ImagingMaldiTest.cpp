#include "model/ImagingDocument.h"

#include <OpenMS/FORMAT/SqliteConnector.h>
#include <OpenMS/config.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <string>

// Validates the Bruker MALDI .d routing (ImagingDocument::readBrukerMaldi) at the
// levels the environment allows: detection of imaging vs non-imaging vs .tsf-only
// datasets, and graceful (non-crashing) failure. The full spectrum-decode + render
// path needs a real analysis.tdf_bin, which no fixture here provides, so it is not
// exercised — see docs/comparison notes.
namespace
{
  // Builds a fake .d directory containing only an analysis.tdf SQLite (GlobalMetadata
  // + MaldiFrameInfo), mirroring OpenMS's own BrukerTimsImagingFile test.
  QString makeImagingD(const QString& parent, const QString& name, const QString& appType)
  {
    const QString d = QDir(parent).filePath(name + QStringLiteral(".d"));
    QDir().mkpath(d);
    const std::string tdf = QDir(d).filePath(QStringLiteral("analysis.tdf")).toStdString();
    OpenMS::SqliteConnector conn(tdf, OpenMS::SqliteConnector::SqlOpenMode::READWRITE_OR_CREATE);
    conn.executeStatement("CREATE TABLE GlobalMetadata (Key TEXT PRIMARY KEY, Value TEXT);");
    if (!appType.isEmpty())
      conn.executeStatement(("INSERT INTO GlobalMetadata (Key, Value) VALUES ('MaldiApplicationType', '"
                             + appType.toStdString() + "');").c_str());
    conn.executeStatement("CREATE TABLE MaldiFrameInfo (Frame INTEGER, XIndexPos INTEGER, YIndexPos INTEGER);");
    conn.executeStatement("INSERT INTO MaldiFrameInfo VALUES (1,0,0);");
    conn.executeStatement("INSERT INTO MaldiFrameInfo VALUES (2,1,0);");
    conn.executeStatement("INSERT INTO MaldiFrameInfo VALUES (3,0,1);");
    conn.executeStatement("INSERT INTO MaldiFrameInfo VALUES (4,1,1);");
    return d;
  }
}

class ImagingMaldiTest final : public QObject
{
  Q_OBJECT

private slots:
  void rejectsMissingPath()
  {
    const auto result = OpenMSViewer::ImagingDocument::readBrukerMaldi(
      QStringLiteral("/definitely/not/a/dataset.d"));
    QVERIFY(!result.succeeded());
    QVERIFY(result.store == nullptr);
    QVERIFY(!result.error.isEmpty());
  }

  void rejectsNonImagingDataset()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString d = makeImagingD(dir.path(), QStringLiteral("droplet"), QStringLiteral("SingleSpectra"));
    const auto result = OpenMSViewer::ImagingDocument::readBrukerMaldi(d);
    QVERIFY(!result.succeeded());
    QVERIFY(result.store == nullptr);
    QVERIFY(!result.error.isEmpty());
#ifdef WITH_OPENTIMS
    QVERIFY2(result.error.contains(QStringLiteral("MALDI imaging"), Qt::CaseInsensitive),
             qPrintable(result.error));
#endif
  }

  void rejectsTsfOnlyDataset()
  {
    // A single-quad MALDI .d carries analysis.tsf but no analysis.tdf, so detection
    // must fail cleanly rather than misfire.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString d = QDir(dir.path()).filePath(QStringLiteral("tsf_only.d"));
    QDir().mkpath(d);
    QFile tsf(QDir(d).filePath(QStringLiteral("analysis.tsf")));
    QVERIFY(tsf.open(QIODevice::WriteOnly));
    tsf.close();
    const auto result = OpenMSViewer::ImagingDocument::readBrukerMaldi(d);
    QVERIFY(!result.succeeded());
    QVERIFY(result.store == nullptr);
    QVERIFY(!result.error.isEmpty());
  }

  void imagingDatasetWithoutBinariesFailsGracefully()
  {
    // Detection passes (MaldiApplicationType == 'Imaging'), but there is no
    // analysis.tdf_bin, so spectrum decoding must fail cleanly — never crash, and
    // never return a half-built store.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString d = makeImagingD(dir.path(), QStringLiteral("imaging"), QStringLiteral("Imaging"));
    const auto result = OpenMSViewer::ImagingDocument::readBrukerMaldi(d);
    QVERIFY(!result.succeeded());
    QVERIFY(result.store == nullptr);
    QVERIFY(!result.error.isEmpty());
  }
};

int runImagingMaldiTests(int argc, char** argv)
{
  ImagingMaldiTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "ImagingMaldiTest.moc"
