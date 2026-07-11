#include "TestData.h"

#include "model/ViewerDocument.h"
#include "model/SelectionController.h"

#include <OpenMS/FORMAT/MzMLFile.h>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <vector>

class ViewerDocumentTest final : public QObject
{
  Q_OBJECT

private slots:
  void centralSelectionStateEmitsOnlyForChanges()
  {
    OpenMSViewer::SelectionController selection;
    QSignalSpy spectra(&selection, &OpenMSViewer::SelectionController::spectrumChanged);
    QSignalSpy identifications(&selection,
      &OpenMSViewer::SelectionController::identificationChanged);
    QSignalSpy features(&selection, &OpenMSViewer::SelectionController::featureChanged);

    selection.setSpectrum(4);
    selection.setSpectrum(4);
    selection.setFeature(7);
    selection.setIdentification(2, 1);
    QCOMPARE(selection.spectrum().value(), std::size_t{4});
    QCOMPARE(selection.feature().value(), std::size_t{7});
    QCOMPARE(selection.identification().value(), std::size_t{2});
    QCOMPARE(selection.hit().value(), std::size_t{1});
    QCOMPARE(spectra.count(), 1);
    QCOMPARE(features.count(), 1);
    QCOMPARE(identifications.count(), 1);

    selection.clear();
    QVERIFY(!selection.spectrum().has_value());
    QVERIFY(!selection.feature().has_value());
    QVERIFY(!selection.identification().has_value());
    QVERIFY(!selection.hit().has_value());
    QCOMPARE(spectra.count(), 2);
    QCOMPARE(features.count(), 2);
    QCOMPARE(identifications.count(), 2);
  }

  void reportsMissingFiles()
  {
    const auto result = OpenMSViewer::ViewerDocument::readMzML(
      QStringLiteral("/definitely/not/a/real/file.mzML"));
    QVERIFY(!result.succeeded());
    QVERIFY(result.error.contains(QStringLiteral("does not exist")));
  }

  void loadsSummarizesAndNavigates()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("small.mzML"));
    const auto source = OpenMSViewer::TestData::experimentWithChromatograms();
    OpenMS::MzMLFile().store(path.toStdString(), source);

    std::vector<int> progressValues;
    QStringList progressLabels;
    auto result = OpenMSViewer::ViewerDocument::readMzML(
      path, [&progressValues, &progressLabels](const QString& label, int percent)
      {
        progressLabels.push_back(label);
        progressValues.push_back(percent);
      });
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QVERIFY(!progressValues.empty());
    QVERIFY(std::find(progressValues.cbegin(), progressValues.cend(), 100)
            != progressValues.cend());
    QVERIFY(std::any_of(progressLabels.cbegin(), progressLabels.cend(),
                        [](const QString& label) { return !label.isEmpty(); }));
    QCOMPARE(result.statistics.spectrumCount, std::size_t{3});
    QCOMPARE(result.statistics.ms1SpectrumCount, std::size_t{2});
    QCOMPARE(result.statistics.peakCount, std::uint64_t{7});
    QCOMPARE(result.statistics.ms1PeakCount, std::uint64_t{4});
    QCOMPARE(result.tic.size(), std::size_t{2});
    QCOMPARE(result.ticLabel, QStringLiteral("MS1 TIC"));
    QCOMPARE(result.bounds.rtMin, 10.0);
    QCOMPARE(result.bounds.rtMax, 20.0);
    QCOMPARE(result.bounds.mzMin, 400.0);
    QCOMPARE(result.bounds.mzMax, 600.0);
    QCOMPARE(result.spectra.size(), std::size_t{3});
    QCOMPARE(result.spectra[0].tic, 110.0);
    QCOMPARE(result.spectra[0].basePeakIntensity, 100.0);
    QCOMPARE(result.spectra[0].mzMin, 400.0);
    QCOMPARE(result.spectra[0].mzMax, 500.0);
    QVERIFY(!result.spectra[0].precursorMz.has_value());
    QCOMPARE(result.spectra[1].precursorMz.value(), 500.2);
    QCOMPARE(result.spectra[1].precursorCharge, 2);
    QCOMPARE(result.chromatograms.size(), std::size_t{2});
    QCOMPARE(result.chromatograms[0].nativeId, QStringLiteral("TIC"));
    QVERIFY(result.chromatograms[0].isTic);
    QCOMPARE(result.chromatograms[0].points.size(), std::size_t{3});
    QCOMPARE(result.chromatograms[0].maximumIntensity, 80.0);
    QCOMPARE(result.chromatograms[0].totalIntensity, 110.0);
    QCOMPARE(result.chromatograms[1].precursorMz.value(), 500.2);
    QCOMPARE(result.chromatograms[1].productMz.value(), 200.0);

    OpenMSViewer::ViewerDocument document;
    QSignalSpy changed(&document, &OpenMSViewer::ViewerDocument::dataChanged);
    QVERIFY(document.adopt(std::move(result)));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!document.isEmpty());
    QCOMPARE(document.spectra().size(), std::size_t{3});
    QVERIFY(document.hasChromatograms());
    QCOMPARE(document.chromatograms().size(), std::size_t{2});
    QCOMPARE(document.nearestSpectrumIndex(10.9).value(), std::size_t{1});
    QCOMPARE(document.nearestSpectrumIndex(10.9, 1).value(), std::size_t{0});
    QCOMPARE(document.adjacentSpectrumIndex(0, 1, 2).value(), std::size_t{1});
    QCOMPARE(document.adjacentSpectrumIndex(1, 1, 1).value(), std::size_t{2});
    QCOMPARE(document.edgeSpectrumIndex(false, 1).value(), std::size_t{0});
    QCOMPARE(document.edgeSpectrumIndex(true, 1).value(), std::size_t{2});

    document.clear();
    QCOMPARE(changed.count(), 2);
    QVERIFY(document.isEmpty());
  }

  void loadsChromatogramOnlyMzML()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("transitions.chrom.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(),
                             OpenMSViewer::TestData::chromatogramOnlyExperiment());

    auto result = OpenMSViewer::ViewerDocument::readMzML(path);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.statistics.spectrumCount, std::size_t{0});
    QCOMPARE(result.spectra.size(), std::size_t{0});
    QCOMPARE(result.chromatograms.size(), std::size_t{2});
    QCOMPARE(result.ticLabel, QStringLiteral("No spectral TIC"));
    QCOMPARE(result.bounds.rtMin, 9.0);
    QCOMPARE(result.bounds.rtMax, 21.0);
    QCOMPARE(result.bounds.mzMin, 200.0);
    QCOMPARE(result.bounds.mzMax, 500.2);

    OpenMSViewer::ViewerDocument document;
    QVERIFY(document.adopt(std::move(result)));
    QVERIFY(!document.isEmpty());
    QVERIFY(document.hasChromatograms());
    QVERIFY(!document.edgeSpectrumIndex(false).has_value());
  }

  void cancelsMzMLLoadingWithoutPublishingPartialData()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("cancel.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), OpenMSViewer::TestData::experiment());

    const auto result = OpenMSViewer::ViewerDocument::readMzML(
      path, {}, [] { return true; });
    QVERIFY(!result.succeeded());
    QVERIFY(result.error.contains(QStringLiteral("cancel"), Qt::CaseInsensitive));
    QVERIFY(result.experiment == nullptr);
  }
};

int runViewerDocumentTests(int argc, char** argv)
{
  ViewerDocumentTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "ViewerDocumentTest.moc"
