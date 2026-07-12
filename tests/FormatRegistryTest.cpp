#include "TestData.h"

#include "model/FormatRegistry.h"
#include "model/ViewerDocument.h"

#include <OpenMS/FORMAT/FeatureXMLFile.h>
#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/FORMAT/MzMLFile.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class FormatRegistryTest final : public QObject
{
  Q_OBJECT

private slots:
  void detectsCategoriesAndShapes()
  {
    using namespace OpenMSViewer::FormatRegistry;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString mzml = dir.filePath(QStringLiteral("s.mzML"));
    OpenMS::MzMLFile().store(mzml.toStdString(), OpenMSViewer::TestData::experiment());
    const QString featureXml = dir.filePath(QStringLiteral("f.featureXML"));
    OpenMS::FeatureXMLFile().store(featureXml.toStdString(), OpenMSViewer::TestData::featureMap());
    const auto [proteins, peptides] = OpenMSViewer::TestData::identifications();
    const QString idXml = dir.filePath(QStringLiteral("i.idXML"));
    OpenMS::IdXMLFile().store(idXml.toStdString(), proteins, peptides);

    QCOMPARE(detect(mzml).category, Category::Experiment);
    QVERIFY(detect(mzml).supported);
    QCOMPARE(detect(mzml).shape, Shape::File);
    QCOMPARE(detect(featureXml).category, Category::Features);
    QCOMPARE(detect(idXml).category, Category::Identifications);

    // Nonexistent path → unsupported.
    QVERIFY(!detect(dir.filePath(QStringLiteral("nope.mzML"))).supported);

    // A directory-shaped type must actually be a directory: a plain FILE named
    // *.featureparquet is rejected; a DIRECTORY of that name is accepted.
    const QString bundleFile = dir.filePath(QStringLiteral("x.featureparquet"));
    QFile file(bundleFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a bundle");
    file.close();
    QVERIFY(!detect(bundleFile).supported);

    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("y.featureparquet")));
    const auto bundle = detect(dir.filePath(QStringLiteral("y.featureparquet")));
    QCOMPARE(bundle.category, Category::Features);
    QCOMPARE(bundle.shape, Shape::Directory);
    QVERIFY(bundle.supported);
  }

  void categoryLoadersRoundTripExistingFormats()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString mzml = dir.filePath(QStringLiteral("s.mzML"));
    OpenMS::MzMLFile().store(mzml.toStdString(), OpenMSViewer::TestData::experiment());
    const auto experiment = OpenMSViewer::ViewerDocument::readExperiment(mzml);
    QVERIFY2(experiment.succeeded(), qPrintable(experiment.error));
    QCOMPARE(experiment.statistics.spectrumCount, OpenMSViewer::TestData::experiment().size());

    const QString featureXml = dir.filePath(QStringLiteral("f.featureXML"));
    OpenMS::FeatureXMLFile().store(featureXml.toStdString(), OpenMSViewer::TestData::featureMap());
    const auto features = OpenMSViewer::ViewerDocument::readFeatures(featureXml);
    QVERIFY2(features.succeeded(), qPrintable(features.error));
    QCOMPARE(features.features.size(), OpenMSViewer::TestData::featureMap().size());

    const auto [proteins, peptides] = OpenMSViewer::TestData::identifications();
    const QString idXml = dir.filePath(QStringLiteral("i.idXML"));
    OpenMS::IdXMLFile().store(idXml.toStdString(), proteins, peptides);
    const auto identifications = OpenMSViewer::ViewerDocument::readIdentifications(idXml);
    QVERIFY2(identifications.succeeded(), qPrintable(identifications.error));
    QVERIFY(!identifications.identifications.empty());

    // Wrong-category and missing inputs fail gracefully (never a crash).
    QVERIFY(!OpenMSViewer::ViewerDocument::readFeatures(mzml).succeeded());
    QVERIFY(!OpenMSViewer::ViewerDocument::readExperiment(featureXml).succeeded());
    QVERIFY(OpenMSViewer::ViewerDocument::readFeatures(dir.filePath(QStringLiteral("no.featureXML")))
              .error.contains(QStringLiteral("does not exist")));
  }
};

int runFormatRegistryTests(int argc, char** argv)
{
  FormatRegistryTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "FormatRegistryTest.moc"
