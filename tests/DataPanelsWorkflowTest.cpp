#include "TestData.h"

#include "MainWindow.h"
#include "model/ViewerDocument.h"
#include "widgets/ChromatogramPanelWidget.h"
#include "widgets/IdentificationTableWidget.h"
#include "widgets/PeakMapWidget.h"
#include "widgets/SpectrumTableWidget.h"
#include "widgets/SpectrumWidget.h"
#include "widgets/TicWidget.h"

#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/FORMAT/MzMLFile.h>

#include <QCheckBox>
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

class DataPanelsWorkflowTest final : public QObject
{
  Q_OBJECT

private slots:
  void filtersAllSpectrumHitsAndSelectsNativeChromatograms()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mzmlPath = directory.filePath(QStringLiteral("chromatograms.mzML"));
    const QString idPath = directory.filePath(QStringLiteral("ids.idXML"));
    OpenMS::MzMLFile().store(mzmlPath.toStdString(),
                             OpenMSViewer::TestData::experimentWithChromatograms());
    const auto [proteins, peptides] = OpenMSViewer::TestData::identifications();
    OpenMS::IdXMLFile().store(idPath.toStdString(), proteins, peptides);

    OpenMSViewer::ViewerDocument document;
    auto mzml = OpenMSViewer::ViewerDocument::readMzML(mzmlPath);
    auto ids = OpenMSViewer::ViewerDocument::readIdXML(idPath);
    QVERIFY2(mzml.succeeded(), qPrintable(mzml.error));
    QVERIFY2(ids.succeeded(), qPrintable(ids.error));
    QVERIFY(document.adopt(std::move(mzml)));
    QVERIFY(document.adoptIdentifications(std::move(ids)));
    QCOMPARE(document.spectrumRecord(1)->identificationIndex.value(), std::size_t{0});

    OpenMSViewer::SpectrumTableWidget spectra;
    spectra.resize(1100, 460);
    spectra.show();
    spectra.setData(document.spectra(), document.identifications());
    auto* spectraTable = spectra.findChild<QTableView*>(QStringLiteral("spectraTable"));
    auto* mode = spectra.findChild<QComboBox*>(QStringLiteral("spectrumModeFilter"));
    auto* sequence = spectra.findChild<QLineEdit*>(QStringLiteral("spectrumSequenceFilter"));
    auto* allHits = spectra.findChild<QCheckBox*>(QStringLiteral("spectrumAllHits"));
    auto* minimumRt = spectra.findChild<QLineEdit*>(QStringLiteral("spectrumMinimumRt"));
    auto* score = spectra.findChild<QLineEdit*>(QStringLiteral("spectrumMinimumScore"));
    auto* scoreLabel = spectra.findChild<QLabel*>(QStringLiteral("spectrumScoreThresholdLabel"));
    QVERIFY(spectraTable != nullptr);
    QVERIFY(mode != nullptr);
    QVERIFY(sequence != nullptr);
    QVERIFY(allHits != nullptr);
    QVERIFY(minimumRt != nullptr);
    QVERIFY(score != nullptr);
    QVERIFY(scoreLabel != nullptr);
    QCOMPARE(spectraTable->model()->rowCount(), 3);
    mode->setCurrentIndex(1);
    QCOMPARE(spectraTable->model()->rowCount(), 1);
    mode->setCurrentIndex(2);
    QCOMPARE(spectraTable->model()->rowCount(), 1);
    allHits->setChecked(true);
    QCOMPARE(spectraTable->model()->rowCount(), 2);
    sequence->setText(QStringLiteral("PEPTIDER"));
    QCOMPARE(spectraTable->model()->rowCount(), 1);
    sequence->clear();
    mode->setCurrentIndex(0);
    QCOMPARE(spectraTable->model()->rowCount(), 4);
    QVERIFY(scoreLabel->text().contains(QStringLiteral("≥")));
    score->setText(QStringLiteral("35"));
    QCOMPARE(spectraTable->model()->rowCount(), 1);  // hyperscore: higher is better
    score->setText(QStringLiteral("not-a-number"));
    QVERIFY(score->property("invalidInput").toBool());
    QCOMPARE(spectraTable->model()->rowCount(), 4);  // invalid input is visible, not silently applied
    score->clear();
    spectra.setRtInMinutes(true);
    minimumRt->setText(QStringLiteral("0.3"));  // 18 seconds
    QCOMPARE(spectraTable->model()->rowCount(), 1);
    minimumRt->clear();

    std::optional<std::size_t> activatedSpectrum;
    connect(&spectra, &OpenMSViewer::SpectrumTableWidget::spectrumActivated,
            &spectra, [&](std::size_t index) { activatedSpectrum = index; });
    const QModelIndex ms2Row = spectraTable->model()->index(1, 0);
    QTest::mouseClick(spectraTable->viewport(), Qt::LeftButton, Qt::NoModifier,
                      spectraTable->visualRect(ms2Row).center());
    QCOMPARE(activatedSpectrum.value(), std::size_t{1});

    OpenMSViewer::ChromatogramPanelWidget chromatograms;
    chromatograms.resize(1000, 650);
    chromatograms.show();
    chromatograms.setChromatograms(document.chromatograms());
    chromatograms.setPeakMapRange({11.0, 17.0, 0.0, 1.0});
    auto* chromatogramTable = chromatograms.findChild<QTableView*>(QStringLiteral("chromatogramTable"));
    QVERIFY(chromatogramTable != nullptr);
    QCOMPARE(chromatogramTable->model()->rowCount(), 2);
    chromatogramTable->selectRow(0);
    chromatogramTable->selectionModel()->select(
      chromatogramTable->model()->index(1, 0),
      QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCOMPARE(chromatograms.selectedChromatogramCount(), std::size_t{2});
    QVERIFY(chromatograms.plot()->peakMapRtRange().has_value());
    QCOMPARE(chromatograms.plot()->peakMapRtRange()->first, 11.0);
    QCOMPARE(chromatograms.plot()->peakMapRtRange()->second, 17.0);

    std::optional<double> activatedRt;
    connect(chromatograms.plot(), &OpenMSViewer::ChromatogramPlotWidget::rtActivated,
            &chromatograms, [&](double rt) { activatedRt = rt; });
    QTest::mouseClick(chromatograms.plot(), Qt::LeftButton, Qt::NoModifier,
                      chromatograms.plot()->rect().center());
    QVERIFY(activatedRt.has_value());
    QVERIFY(*activatedRt > 13.0 && *activatedRt < 17.0);
  }

  void lowerIsBetterScoreThresholdIsExplicit()
  {
    OpenMSViewer::IdentificationRecord confident;
    confident.index = 0;
    confident.scoreType = QStringLiteral("q-value");
    confident.higherScoreBetter = false;
    confident.hits.push_back({0, QStringLiteral("PEPTIDE"), 0.01, 2, {}, {}});
    OpenMSViewer::IdentificationRecord weak = confident;
    weak.index = 1;
    weak.hits.front().score = 0.2;

    OpenMSViewer::IdentificationTableWidget identifications;
    identifications.setIdentifications({confident, weak});
    auto* table = identifications.findChild<QTableView*>(QStringLiteral("identificationTable"));
    auto* threshold = identifications.findChild<QLineEdit*>(QStringLiteral("identificationScoreThreshold"));
    auto* label = identifications.findChild<QLabel*>(QStringLiteral("identificationScoreThresholdLabel"));
    QVERIFY(table != nullptr);
    QVERIFY(threshold != nullptr);
    QVERIFY(label != nullptr);
    QVERIFY(label->text().contains(QStringLiteral("q-value")));
    QVERIFY(label->text().contains(QStringLiteral("≤")));
    threshold->setText(QStringLiteral("0.05"));
    QCOMPARE(table->model()->rowCount(), 1);
    threshold->setText(QStringLiteral("."));
    QVERIFY(threshold->property("invalidInput").toBool());
    QCOMPARE(table->model()->rowCount(), 2);
  }

  void synchronizesSpectrumTableWithMainWindow()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mzmlPath = directory.filePath(QStringLiteral("small.mzML"));
    OpenMS::MzMLFile().store(mzmlPath.toStdString(), OpenMSViewer::TestData::experiment());

    OpenMSViewer::MainWindow window;
    window.resize(1300, 850);
    window.show();
    window.loadFile(mzmlPath);
    auto* spectra = window.findChild<OpenMSViewer::SpectrumTableWidget*>();
    auto* spectraDock = window.findChild<QDockWidget*>(QStringLiteral("spectraDock"));
    auto* spectrum = window.findChild<OpenMSViewer::SpectrumWidget*>();
    auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    auto* tic = window.findChild<OpenMSViewer::TicWidget*>();
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("spectrumSearch"));
    auto* spectrumIndex = window.findChild<QSpinBox*>(QStringLiteral("spectrumIndex"));
    QVERIFY(spectra != nullptr);
    QVERIFY(spectraDock != nullptr);
    QVERIFY(spectrum != nullptr);
    QVERIFY(peakMap != nullptr);
    QVERIFY(tic != nullptr);
    QVERIFY(search != nullptr);
    QVERIFY(spectrumIndex != nullptr);
    auto* table = spectra->findChild<QTableView*>(QStringLiteral("spectraTable"));
    QVERIFY(table != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 3, 5000);
    QAction* rtMinutes = nullptr;
    for (QAction* action : window.findChildren<QAction*>())
      if (action->text() == QStringLiteral("RT in minutes")) { rtMinutes = action; break; }
    QVERIFY(rtMinutes != nullptr);
    rtMinutes->setChecked(true);
    search->setText(QStringLiteral("0.3333"));  // approximately 20 seconds
    QTest::keyClick(search, Qt::Key_Return);
    QTRY_COMPARE(spectrumIndex->value(), 3);
    spectraDock->raise();
    QTest::qWait(30);
    const QModelIndex row = table->model()->index(1, 0);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                      table->visualRect(row).center());
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{1});
    table->setFocus();
    QTest::keyClick(table, Qt::Key_Down);
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{2});

    // Reactive fan-out: a spectra-table selection (not just a TIC click)
    // recenters the peak map when the spectrum's RT is outside the view.
    peakMap->setRtRange(0.0, 4.0);
    QTest::keyClick(table, Qt::Key_Up);
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{1});
    QTest::keyClick(table, Qt::Key_Down);
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{2});
    QTRY_VERIFY(peakMap->viewRange().rtMin > 4.0);

    peakMap->setRtRange(10.0, 14.0);
    const QRect ticPlot = tic->rect().adjusted(62, 30, -16, -35);
    QTest::mouseClick(tic, Qt::LeftButton, Qt::NoModifier,
                      QPoint(ticPlot.right() - 1, ticPlot.center().y()));
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{2});
    QTRY_VERIFY(peakMap->viewRange().rtMin > 15.9);
    QTRY_VERIFY(peakMap->viewRange().rtMax > 19.9);
    QVERIFY(qAbs(peakMap->viewRange().rtSpan() - 4.0) < 1e-9);
  }
};

int runDataPanelsWorkflowTests(int argc, char** argv)
{
  DataPanelsWorkflowTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "DataPanelsWorkflowTest.moc"
