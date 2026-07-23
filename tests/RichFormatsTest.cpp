#include "TestData.h"

#include "model/ViewerDocument.h"
#include "model/ConsensusDocument.h"

#include <OpenMS/FORMAT/FileHandler.h>
#include <OpenMS/KERNEL/ConsensusFeature.h>
#include <OpenMS/KERNEL/ConsensusMap.h>

#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#ifndef OPENMS_VIEWER_TEST_DATA_DIR
#error "OPENMS_VIEWER_TEST_DATA_DIR must be defined by the build"
#endif

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

  // The consensusparquet bundle directory is a first-class folder input: it must
  // round-trip through the viewer's consensus loader like consensusXML.
  void loadsConsensusParquet()
  {
    OpenMS::ConsensusMap map;
    OpenMS::ConsensusMap::ColumnHeader light;
    light.filename = "light.mzML";
    light.label = "light";
    light.size = 1;
    map.getColumnHeaders()[0] = light;
    map.setExperimentType("labeled_MS1");

    OpenMS::ConsensusFeature feature;
    feature.setRT(100.0);
    feature.setMZ(500.25);
    feature.setCharge(2);
    feature.setIntensity(3000.0);
    OpenMS::FeatureHandle handle;
    handle.setMapIndex(0);
    handle.setRT(100.0);
    handle.setMZ(500.25);
    handle.setIntensity(3000.0);
    handle.setCharge(2);
    feature.insert(handle);
    map.push_back(feature);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bundle = dir.filePath(QStringLiteral("consensus.consensusparquet"));
    OpenMS::FileHandler().storeConsensusFeatures(bundle.toStdString(), map);

    const auto loaded = OpenMSViewer::ConsensusDocument::read(bundle);
    QVERIFY2(loaded.succeeded(), qPrintable(loaded.error));
    QCOMPARE(loaded.features.size(), std::size_t{1});
    QCOMPARE(loaded.columns.size(), std::size_t{1});
  }

  // A standalone OpenSWATH .xic (CHROMPARQUET, a single Parquet file) decodes to a
  // chromatogram-only LoadResult: every transition XIC becomes an MSChromatogram.
  void loadsStandaloneXicChromatograms()
  {
    const QString xic = QStringLiteral("%1/openswath_transitions.xic")
                          .arg(QString::fromUtf8(OPENMS_VIEWER_TEST_DATA_DIR));
    const auto loaded = OpenMSViewer::ViewerDocument::readXic(xic);
    QVERIFY2(loaded.succeeded(), qPrintable(loaded.error));
    QCOMPARE(loaded.chromatograms.size(), std::size_t{18});
    // Chromatogram-only: no spectra, but each trace carries its RT/intensity points.
    QVERIFY(loaded.experiment->getSpectra().empty());
    QVERIFY(!loaded.chromatograms.front().points.empty());
    QVERIFY(!loaded.chromatograms.front().nativeId.isEmpty());
  }

  // A non-.xic / corrupt input fails softly (no throw, no crash) with an error.
  void readXicRejectsBadInput()
  {
    QVERIFY(!OpenMSViewer::ViewerDocument::readXic(
              QStringLiteral("/definitely/not/a/real/file.xic")).succeeded());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bogus = dir.filePath(QStringLiteral("bogus.xic"));
    QFile file(bogus);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not a parquet file");
    file.close();
    const auto loaded = OpenMSViewer::ViewerDocument::readXic(bogus);
    QVERIFY(!loaded.succeeded());
    QVERIFY(!loaded.error.isEmpty());
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
