#include "TestData.h"

#include "model/ViewerDocument.h"

#include <OpenMS/FORMAT/FeatureXMLFile.h>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class FeatureDocumentTest final : public QObject
{
  Q_OBJECT

private slots:
  void reportsInvalidInput()
  {
    const auto missing = OpenMSViewer::ViewerDocument::readFeatureXML(
      QStringLiteral("/definitely/not/a/real/file.featureXML"));
    QVERIFY(!missing.succeeded());
    QVERIFY(missing.error.contains(QStringLiteral("does not exist")));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString wrongExtension = directory.filePath(QStringLiteral("features.xml"));
    QFile file(wrongExtension);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    const auto wrong = OpenMSViewer::ViewerDocument::readFeatureXML(wrongExtension);
    QVERIFY(!wrong.succeeded());
    QVERIFY(wrong.error.contains(QStringLiteral("Unsupported")));
  }

  void loadsGeometryMetadataAndDocumentState()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("features.featureXML"));
    const auto source = OpenMSViewer::TestData::featureMap();
    OpenMS::FeatureXMLFile().store(path.toStdString(), source);

    auto result = OpenMSViewer::ViewerDocument::readFeatureXML(path);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.features.size(), std::size_t{2});

    const auto& first = result.features[0];
    QCOMPARE(first.index, std::size_t{0});
    QCOMPARE(first.rt, 100.5);
    QCOMPARE(first.mz, 500.25);
    QCOMPARE(first.intensity, 1000.0);
    QCOMPARE(first.charge, 2);
    QVERIFY(qAbs(first.quality - 0.95) < 1e-6);
    QCOMPARE(first.hulls.size(), std::size_t{1});
    QCOMPARE(first.hulls.front().size(), std::size_t{4});
    QCOMPARE(first.bounds.rtMin, 99.0);
    QCOMPARE(first.bounds.rtMax, 102.0);
    QCOMPARE(first.bounds.mzMin, 499.5);
    QCOMPARE(first.bounds.mzMax, 501.0);

    const auto& second = result.features[1];
    QCOMPARE(second.bounds.rtMin, 149.0);
    QCOMPARE(second.bounds.rtMax, 151.0);
    QCOMPARE(second.bounds.mzMin, 699.5);
    QCOMPARE(second.bounds.mzMax, 700.5);

    OpenMSViewer::ViewerDocument document;
    QSignalSpy changed(&document, &OpenMSViewer::ViewerDocument::featuresChanged);
    QVERIFY(document.adoptFeatures(std::move(result)));
    QCOMPARE(changed.count(), 1);
    QVERIFY(document.hasFeatures());
    QCOMPARE(document.features().size(), std::size_t{2});
    QCOMPARE(document.feature(1)->mz, 700.0);
    QVERIFY(document.feature(2) == nullptr);

    document.clearFeatures();
    QCOMPARE(changed.count(), 2);
    QVERIFY(!document.hasFeatures());
    QVERIFY(document.features().empty());
  }
};

int runFeatureDocumentTests(int argc, char** argv)
{
  FeatureDocumentTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "FeatureDocumentTest.moc"

