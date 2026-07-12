#include "TestData.h"

#include "model/ViewerDocument.h"

#include <OpenMS/FORMAT/FileHandler.h>

#include <QString>
#include <QTemporaryDir>
#include <QTest>

// Round-trips the new Phase-2 formats through OpenMS writers, then loads them via
// the viewer's category loaders — self-contained (no external fixtures needed).
class RichFormatsTest final : public QObject
{
  Q_OBJECT

private slots:
  void loadsFeatureParquet()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bundle = dir.filePath(QStringLiteral("features.featureparquet"));
    const auto source = OpenMSViewer::TestData::featureMap();
    OpenMS::FileHandler().storeFeatures(bundle.toStdString(), source);

    const auto loaded = OpenMSViewer::ViewerDocument::readFeatures(bundle);
    QVERIFY2(loaded.succeeded(), qPrintable(loaded.error));
    QCOMPARE(loaded.features.size(), source.size());
  }

  void loadsIdParquetAndMzIdentML()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto [proteins, peptides] = OpenMSViewer::TestData::identifications();

    const QString idParquet = dir.filePath(QStringLiteral("ids.idparquet"));
    OpenMS::FileHandler().storeIdentifications(idParquet.toStdString(), proteins, peptides);
    const auto fromParquet = OpenMSViewer::ViewerDocument::readIdentifications(idParquet);
    QVERIFY2(fromParquet.succeeded(), qPrintable(fromParquet.error));
    QVERIFY(!fromParquet.identifications.empty());

    const QString mzid = dir.filePath(QStringLiteral("ids.mzid"));
    OpenMS::FileHandler().storeIdentifications(mzid.toStdString(), proteins, peptides);
    const auto fromMzid = OpenMSViewer::ViewerDocument::readIdentifications(mzid);
    QVERIFY2(fromMzid.succeeded(), qPrintable(fromMzid.error));
    QVERIFY(!fromMzid.identifications.empty());
  }

  void loadsSecondaryExperimentFormats()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto source = OpenMSViewer::TestData::experiment();

    const QString mzXml = dir.filePath(QStringLiteral("spectra.mzXML"));
    OpenMS::FileHandler().storeExperiment(mzXml.toStdString(), source);
    const auto fromMzXml = OpenMSViewer::ViewerDocument::readExperiment(mzXml);
    QVERIFY2(fromMzXml.succeeded(), qPrintable(fromMzXml.error));
    QCOMPARE(fromMzXml.statistics.spectrumCount, source.size());
  }
};

int runRichFormatsTests(int argc, char** argv)
{
  RichFormatsTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "RichFormatsTest.moc"
