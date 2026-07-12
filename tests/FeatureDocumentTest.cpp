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

  void manualEditAddUpdateRemove()
  {
    OpenMSViewer::ViewerDocument document;
    QSignalSpy changed(&document, &OpenMSViewer::ViewerDocument::featuresChanged);

    // Add creates the feature map on a bare document.
    const std::size_t index = document.addFeature(1234.5, 567.89, 4200.0, 3);
    QCOMPARE(index, std::size_t{0});
    QCOMPARE(changed.count(), 1);
    QVERIFY(document.hasFeatures());
    QCOMPARE(document.features().size(), std::size_t{1});
    const OpenMSViewer::FeatureRecord* added = document.feature(0);
    QVERIFY(added != nullptr);
    QCOMPARE(added->rt, 1234.5);
    QCOMPARE(added->mz, 567.89);
    QCOMPARE(added->charge, 3);
    // No convex hull → a default box around the centroid.
    QVERIFY(added->bounds.rtMin < 1234.5 && added->bounds.rtMax > 1234.5);

    document.addFeature(2000.0, 800.0, 100.0, 1);
    QCOMPARE(document.features().size(), std::size_t{2});

    // Update moves/edits the first feature.
    document.updateFeature(0, 1300.0, 570.0, 9000.0, 2);
    QCOMPARE(document.feature(0)->rt, 1300.0);
    QCOMPARE(document.feature(0)->mz, 570.0);
    QCOMPARE(document.feature(0)->intensity, 9000.0);
    QCOMPARE(document.feature(0)->charge, 2);

    // Remove drops it; indices are rebuilt.
    document.removeFeature(0);
    QCOMPARE(document.features().size(), std::size_t{1});
    QCOMPARE(document.feature(0)->mz, 800.0);  // the second feature, now index 0

    // Out-of-range edits are no-ops.
    const int before = changed.count();
    document.updateFeature(99, 0.0, 0.0, 0.0, 0);
    document.removeFeature(99);
    QCOMPARE(changed.count(), before);

    // Round-trips through featureXML (the manual features are real Features).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.filePath(QStringLiteral("edited.featureXML"));
    QString error;
    QVERIFY2(document.saveFeatures(out, error), qPrintable(error));
    OpenMS::FeatureMap reloaded;
    OpenMS::FeatureXMLFile().load(out.toStdString(), reloaded);
    QCOMPARE(reloaded.size(), std::size_t{1});
  }

  void editKeepsHullUnlessMoved()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("hulls.featureXML"));
    OpenMS::FeatureXMLFile().store(path.toStdString(), OpenMSViewer::TestData::featureMap());

    OpenMSViewer::ViewerDocument document;
    QVERIFY(document.adoptFeatures(OpenMSViewer::ViewerDocument::readFeatureXML(path)));
    QVERIFY(!document.feature(0)->hulls.empty());  // loaded with a convex hull

    // Editing only intensity/charge (same RT/m·z) keeps the hull.
    const double rt = document.feature(0)->rt;
    const double mz = document.feature(0)->mz;
    document.updateFeature(0, rt, mz, 12345.0, 4);
    QVERIFY(!document.feature(0)->hulls.empty());
    QCOMPARE(document.feature(0)->intensity, 12345.0);

    // Moving the centroid drops the now-mismatched hull (falls back to a box).
    document.updateFeature(0, rt + 10.0, mz, 12345.0, 4);
    QVERIFY(document.feature(0)->hulls.empty());
  }
};

int runFeatureDocumentTests(int argc, char** argv)
{
  FeatureDocumentTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "FeatureDocumentTest.moc"

