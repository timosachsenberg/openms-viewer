#include "TestData.h"

#include "MainWindow.h"
#include "widgets/IdentificationTableWidget.h"
#include "widgets/PeakMapWidget.h"
#include "widgets/SpectrumTableWidget.h"
#include "widgets/SpectrumWidget.h"

#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/FORMAT/MzMLFile.h>

#include <QAction>
#include <QCheckBox>
#include <QLineEdit>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

class MainWindowIdentificationWorkflowTest final : public QObject
{
  Q_OBJECT

private slots:
  void loadsLinksFiltersSelectsAndAnnotates()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mzmlPath = directory.filePath(QStringLiteral("small.mzML"));
    const QString idPath = directory.filePath(QStringLiteral("small.idXML"));
    OpenMS::MzMLFile().store(mzmlPath.toStdString(), OpenMSViewer::TestData::experiment());
    auto [proteins, peptides] = OpenMSViewer::TestData::identifications();
    OpenMS::PeptideIdentification alternate = peptides.front();
    alternate.setRT(11.4);
    alternate.setMZ(500.3);
    OpenMS::PeptideHit alternatePeptideHit;
    alternatePeptideHit.setSequence(OpenMS::AASequence::fromString("ALTERNATE"));
    alternatePeptideHit.setScore(35.0);
    alternatePeptideHit.setCharge(2);
    alternate.setHits({alternatePeptideHit});
    peptides.push_back(std::move(alternate));
    OpenMS::IdXMLFile().store(idPath.toStdString(), proteins, peptides);

    OpenMSViewer::MainWindow window;
    window.resize(1200, 800);
    window.show();
    window.loadFiles({mzmlPath, idPath});

    auto* identificationWidget = window.findChild<OpenMSViewer::IdentificationTableWidget*>();
    auto* table = identificationWidget ? identificationWidget->findChild<QTableView*>() : nullptr;
    auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    auto* spectrum = window.findChild<OpenMSViewer::SpectrumWidget*>();
    auto* spectrumTableWidget = window.findChild<OpenMSViewer::SpectrumTableWidget*>();
    auto* spectrumTable = spectrumTableWidget
      ? spectrumTableWidget->findChild<QTableView*>(QStringLiteral("spectraTable")) : nullptr;
    auto* spectrumAllHits = spectrumTableWidget
      ? spectrumTableWidget->findChild<QCheckBox*>(QStringLiteral("spectrumAllHits")) : nullptr;
    auto* resetFilters = identificationWidget
      ? identificationWidget->findChild<QAction*>(
          QStringLiteral("identificationResetFilters")) : nullptr;
    QVERIFY(identificationWidget != nullptr);
    QVERIFY(table != nullptr);
    QVERIFY(peakMap != nullptr);
    QVERIFY(spectrum != nullptr);
    QVERIFY(spectrumTable != nullptr);
    QVERIFY(spectrumAllHits != nullptr);
    QVERIFY(resetFilters != nullptr);
    QCOMPARE(resetFilters->text(), QStringLiteral("Reset filters"));
    QVERIFY(!resetFilters->icon().isNull());

    QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 3, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->hasExperiment(), 5000);

    const QModelIndex firstRow = table->model()->index(0, 0);
    table->scrollTo(firstRow);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                      table->visualRect(firstRow).center());
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{1});
    QTRY_VERIFY_WITH_TIMEOUT(spectrum->annotation().has_value(), 3000);
    QCOMPARE(spectrum->annotation()->sequence, QStringLiteral("PEPTIDE"));
    QCOMPARE(spectrum->annotation()->matched.size(), std::size_t{1});
    QVERIFY(spectrum->annotation()->matched.front().external);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->selectedIdentification().has_value(), 2000);
    QCOMPARE(*peakMap->selectedIdentification(), std::size_t{0});

    table->setFocus();
    QTest::keyClick(table, Qt::Key_Down);
    QTRY_COMPARE(*peakMap->selectedIdentification(), std::size_t{1});
    QTest::keyClick(table, Qt::Key_Up);
    QTRY_COMPARE(*peakMap->selectedIdentification(), std::size_t{0});

    auto* allHits = identificationWidget->findChild<QCheckBox*>();
    QVERIFY(allHits != nullptr);
    allHits->setChecked(true);
    QTRY_COMPARE(table->model()->rowCount(), 4);

    QLineEdit* sequenceFilter = nullptr;
    for (QLineEdit* input : identificationWidget->findChildren<QLineEdit*>())
      if (input->placeholderText().contains(QStringLiteral("Sequence"))) sequenceFilter = input;
    QVERIFY(sequenceFilter != nullptr);
    sequenceFilter->setText(QStringLiteral("PEPTIDER"));
    QTRY_COMPARE(table->model()->rowCount(), 1);
    const QModelIndex alternateHit = table->model()->index(0, 0);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                      table->visualRect(alternateHit).center());
    QTRY_VERIFY(spectrum->annotation().has_value());
    QTRY_COMPARE(spectrum->annotation()->sequence, QStringLiteral("PEPTIDER"));
    sequenceFilter->clear();
    QTRY_COMPARE(table->model()->rowCount(), 4);

    spectrumAllHits->setChecked(true);
    QTRY_COMPARE(spectrumTable->model()->rowCount(), 5);
    QModelIndex alternateSpectrumRow;
    for (int row = 0; row < spectrumTable->model()->rowCount(); ++row)
    {
      for (int column = 0; column < spectrumTable->model()->columnCount(); ++column)
      {
        if (spectrumTable->model()->index(row, column).data().toString()
            == QStringLiteral("ALTERNATE"))
        {
          alternateSpectrumRow = spectrumTable->model()->index(row, 0);
          break;
        }
      }
      if (alternateSpectrumRow.isValid()) break;
    }
    QVERIFY(alternateSpectrumRow.isValid());
    spectrumTable->scrollTo(alternateSpectrumRow);
    QTest::mouseClick(spectrumTable->viewport(), Qt::LeftButton, Qt::NoModifier,
                      spectrumTable->visualRect(alternateSpectrumRow).center());
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{1});
    QTRY_VERIFY(spectrum->annotation().has_value());
    QTRY_COMPARE(spectrum->annotation()->sequence, QStringLiteral("ALTERNATE"));
    QTRY_COMPARE(*peakMap->selectedIdentification(), std::size_t{2});

    peakMap->resetView();
    const QPoint marker = peakMap->mapDataToWidget(11.2, 500.25).toPoint();
    QTest::mouseMove(peakMap, marker);
    QTest::mouseClick(peakMap, Qt::LeftButton, Qt::NoModifier, marker);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->selectedIdentification().has_value(), 2000);
    QTRY_COMPARE(*peakMap->selectedIdentification(), std::size_t{0});
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{1});
    QTRY_VERIFY(table->currentIndex().isValid());
    QCOMPARE(table->currentIndex().data(Qt::UserRole).toULongLong(), qulonglong{0});
    QTRY_VERIFY(spectrum->annotation().has_value());
    QTRY_COMPARE(spectrum->annotation()->sequence, QStringLiteral("PEPTIDE"));
  }
};

int runMainWindowIdentificationWorkflowTests(int argc, char** argv)
{
  MainWindowIdentificationWorkflowTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "MainWindowIdentificationWorkflowTest.moc"
