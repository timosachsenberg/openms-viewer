#include "TestData.h"

#include "model/ViewerDocument.h"

#include <OpenMS/FORMAT/IdXMLFile.h>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

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

  // Exact spectrum_reference (native-ID) matches win over any RT/m-z proximity,
  // and an unresolvable reference falls back to the RT/m-z window search.
  void nativeIdLinkOverridesRtMzProximity()
  {
    OpenMS::MSExperiment experiment;
    for (int i = 0; i < 3; ++i)
    {
      OpenMS::MSSpectrum spectrum;
      spectrum.setRT(100.0 + i);
      spectrum.setMSLevel(2);
      spectrum.setNativeID("scan=" + std::to_string(i));
      OpenMS::Precursor precursor;
      precursor.setMZ(500.0);
      spectrum.setPrecursors({precursor});
      experiment.addSpectrum(spectrum);
    }

    std::vector<OpenMSViewer::IdentificationRecord> ids(3);
    // (a) spectrum_reference "scan=2" is exact even though its RT/m-z sits closest
    //     to spectrum #0 — the native-ID link must still resolve to #2.
    ids[0].index = 0;
    ids[0].rt = 100.0;
    ids[0].mz = 500.0;
    ids[0].spectrumReference = QStringLiteral("scan=2");
    ids[0].hits.push_back({});
    // (b) a reference that matches no spectrum falls back to RT/m-z → spectrum #1.
    ids[1].index = 1;
    ids[1].rt = 101.0;
    ids[1].mz = 500.0;
    ids[1].spectrumReference = QStringLiteral("scan=does-not-exist");
    ids[1].hits.push_back({});
    // (c) no reference at all → RT/m-z fallback → spectrum #0.
    ids[2].index = 2;
    ids[2].rt = 100.0;
    ids[2].mz = 500.0;
    ids[2].hits.push_back({});

    const auto links = OpenMSViewer::ViewerDocument::linkIdentifications(experiment, ids);

    QCOMPARE(ids[0].spectrumIndex.value(), std::size_t{2});
    QCOMPARE(ids[0].linkMode, OpenMSViewer::LinkMode::NativeId);
    QCOMPARE(ids[1].spectrumIndex.value(), std::size_t{1});
    QCOMPARE(ids[1].linkMode, OpenMSViewer::LinkMode::RtMz);
    QCOMPARE(ids[2].spectrumIndex.value(), std::size_t{0});
    QCOMPARE(ids[2].linkMode, OpenMSViewer::LinkMode::RtMz);

    QVERIFY(links.bestBySpectrum[2].has_value());
    QCOMPARE(links.bestBySpectrum[2].value(), std::size_t{0});  // id position 0
    QCOMPARE(links.allBySpectrum[0].size(), std::size_t{1});
    QCOMPARE(links.allBySpectrum[0][0], std::size_t{2});
  }

  // On a full RT/m-z-error tie the lowest spectrum index must win, matching the
  // former full-scan order — even though the candidate list is RT-sorted and the
  // equidistant spectra sit on opposite sides of the identification.
  void rtMzTieBreaksToLowestSpectrumIndex()
  {
    OpenMS::MSExperiment experiment;
    for (const double rt : {101.0, 99.0})  // spectrum 0 above, spectrum 1 below
    {
      OpenMS::MSSpectrum spectrum;
      spectrum.setRT(rt);
      spectrum.setMSLevel(2);
      OpenMS::Precursor precursor;
      precursor.setMZ(500.0);
      spectrum.setPrecursors({precursor});
      experiment.addSpectrum(spectrum);
    }

    std::vector<OpenMSViewer::IdentificationRecord> ids(1);
    ids[0].rt = 100.0;  // equidistant (ΔRT 1.0) from both spectra
    ids[0].mz = 500.0;
    ids[0].hits.push_back({});

    OpenMSViewer::ViewerDocument::linkIdentifications(experiment, ids);
    QCOMPARE(ids[0].spectrumIndex.value(), std::size_t{0});
    QCOMPARE(ids[0].linkMode, OpenMSViewer::LinkMode::RtMz);
  }

  // Two ids share a scan reference; the one carrying real coordinates is preferred
  // over a reference-only id whose missing coordinates must rank last, not as 0.
  void coordinateBearingNativeLinkWinsOverReferenceOnly()
  {
    OpenMS::MSExperiment experiment;
    OpenMS::MSSpectrum spectrum;
    spectrum.setRT(100.0);
    spectrum.setMSLevel(2);
    spectrum.setNativeID("scan=7");
    OpenMS::Precursor precursor;
    precursor.setMZ(500.0);
    spectrum.setPrecursors({precursor});
    experiment.addSpectrum(spectrum);

    std::vector<OpenMSViewer::IdentificationRecord> ids(2);
    // id 0: reference-only, no coordinates (contest error must be +inf, not 0).
    ids[0].rt = std::numeric_limits<double>::quiet_NaN();
    ids[0].mz = std::numeric_limits<double>::quiet_NaN();
    ids[0].spectrumReference = QStringLiteral("scan=7");
    ids[0].hits.push_back({});
    // id 1: same reference, with a real (small-error) coordinate.
    ids[1].rt = 100.2;
    ids[1].mz = 500.05;
    ids[1].spectrumReference = QStringLiteral("scan=7");
    ids[1].hits.push_back({});

    const auto links = OpenMSViewer::ViewerDocument::linkIdentifications(experiment, ids);
    QCOMPARE(ids[0].spectrumIndex.value(), std::size_t{0});
    QCOMPARE(ids[1].spectrumIndex.value(), std::size_t{0});
    QVERIFY(links.bestBySpectrum[0].has_value());
    QCOMPARE(links.bestBySpectrum[0].value(), std::size_t{1});  // coordinate-bearing id
    QCOMPARE(links.allBySpectrum[0].size(), std::size_t{2});
  }
};

int runIdentificationDocumentTests(int argc, char** argv)
{
  IdentificationDocumentTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "IdentificationDocumentTest.moc"
