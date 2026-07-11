#include "TestData.h"

#include "model/ViewerDocument.h"

#include <OpenMS/FORMAT/IdXMLFile.h>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class IdentificationDocumentTest final : public QObject
{
  Q_OBJECT

private slots:
  void reportsInvalidInput()
  {
    const auto missing = OpenMSViewer::ViewerDocument::readIdXML(
      QStringLiteral("/definitely/not/a/real/file.idXML"));
    QVERIFY(!missing.succeeded());
    QVERIFY(missing.error.contains(QStringLiteral("does not exist")));
  }

  void loadsAllHitsMetadataAnnotationsAndLinksAfterMzML()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ids.idXML"));
    const auto [proteins, peptides] = OpenMSViewer::TestData::identifications();
    OpenMS::IdXMLFile().store(path.toStdString(), proteins, peptides);

    auto result = OpenMSViewer::ViewerDocument::readIdXML(path);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.identifications.size(), std::size_t{2});
    const auto& linked = result.identifications.front();
    QCOMPARE(linked.rt, 11.2);
    QCOMPARE(linked.mz, 500.25);
    QCOMPARE(linked.scoreType, QStringLiteral("hyperscore"));
    QCOMPARE(linked.hits.size(), std::size_t{2});
    QCOMPARE(linked.hits[0].sequence, QStringLiteral("PEPTIDE"));
    QCOMPARE(linked.hits[1].sequence, QStringLiteral("PEPTIDER"));
    QCOMPARE(linked.hits[0].peakAnnotations.size(), std::size_t{1});
    QCOMPARE(linked.hits[0].peakAnnotations[0].annotation, QStringLiteral("y4+"));
    QVERIFY(!linked.metaValues.empty());
    QVERIFY(!linked.hits[0].metaValues.empty());

    auto secondLinked = result.identifications.front();
    secondLinked.index = result.identifications.size();
    secondLinked.rt = 11.4;
    secondLinked.mz = 500.3;
    secondLinked.hits.resize(1);
    secondLinked.hits.front().sequence = QStringLiteral("ALTERNATE");
    result.identifications.push_back(std::move(secondLinked));

    OpenMSViewer::ViewerDocument document;
    QSignalSpy changed(&document, &OpenMSViewer::ViewerDocument::identificationsChanged);
    QVERIFY(document.adoptIdentifications(std::move(result)));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!document.identification(0)->spectrumIndex.has_value());

    auto mzml = OpenMSViewer::ViewerDocument::LoadResult{};
    mzml.experiment = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::experiment());
    mzml.sourcePath = QStringLiteral("synthetic.mzML");
    mzml.bounds = {10.0, 20.0, 200.0, 700.0};
    mzml.statistics.spectrumCount = mzml.experiment->size();
    QVERIFY(document.adopt(std::move(mzml)));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(document.identification(0)->spectrumIndex.value(), std::size_t{1});
    QVERIFY(!document.identification(1)->spectrumIndex.has_value());
    QCOMPARE(document.identification(2)->spectrumIndex.value(), std::size_t{1});
    QCOMPARE(document.identificationsForSpectrum(1).size(), std::size_t{2});
    QCOMPARE(document.identificationsForSpectrum(1)[0], std::size_t{0});
    QCOMPARE(document.identificationsForSpectrum(1)[1], std::size_t{2});
    QCOMPARE(document.identificationForSpectrum(1)->index, std::size_t{0});
    QVERIFY(document.identificationForSpectrum(0) == nullptr);
    QVERIFY(qAbs(document.identification(0)->linkRtError - 0.2) < 1e-9);
    QVERIFY(qAbs(document.identification(0)->linkMzError - 0.05) < 1e-9);

    document.clearIdentifications();
    QCOMPARE(changed.count(), 3);
    QVERIFY(!document.hasIdentifications());
  }
};

int runIdentificationDocumentTests(int argc, char** argv)
{
  IdentificationDocumentTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "IdentificationDocumentTest.moc"
