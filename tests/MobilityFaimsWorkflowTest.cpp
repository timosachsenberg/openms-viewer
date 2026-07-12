#include "TestData.h"

#include "MainWindow.h"
#include "model/ViewerDocument.h"
#include "plot/IonMobilityRasterizer.h"
#include "widgets/FaimsPanelWidget.h"
#include "widgets/IonMobilityPanelWidget.h"
#include "widgets/SpectrumTableWidget.h"
#include "widgets/SpectrumWidget.h"

#include <OpenMS/FORMAT/MzMLFile.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QSettings>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

class MobilityFaimsWorkflowTest final : public QObject
{
  Q_OBJECT

private slots:
  void extractsAndRasterizesIonMobilityFrames()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("mobility.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), OpenMSViewer::TestData::ionMobilityExperiment());

    auto result = OpenMSViewer::ViewerDocument::readMzML(path);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.ionMobilityFrames.size(), std::size_t{2});
    QCOMPARE(result.ionMobilityFrames[0].spectrumIndex, std::size_t{0});
    QCOMPARE(result.ionMobilityFrames[0].msLevel, 1U);
    QVERIFY(result.ionMobilityFrames[0].mobilityMin < result.ionMobilityFrames[0].mobilityMax);
    QCOMPARE(result.ionMobilityFrames[1].msLevel, 2U);
    QCOMPARE(result.ionMobilityFrames[1].precursorMz.value(), 500.0);
    QCOMPARE(result.ionMobilityFrames[1].isolationWindowLower.value(), 498.0);
    QCOMPARE(result.ionMobilityFrames[1].isolationWindowUpper.value(), 503.0);

    const auto& source = (*result.experiment)[0];
    const OpenMSViewer::IonMobilityRange range{
      result.ionMobilityFrames[0].mzMin, result.ionMobilityFrames[0].mzMax,
      result.ionMobilityFrames[0].mobilityMin, result.ionMobilityFrames[0].mobilityMax};
    const auto raster = OpenMSViewer::IonMobilityRasterizer::render(source, range, {240, 160});
    QVERIFY(!raster.image.isNull());
    QCOMPARE(raster.image.size(), QSize(240, 160));
    QCOMPARE(raster.mobilogram.size(), std::size_t{160});
    QVERIFY(raster.maximumIntensity > 0.0F);

    OpenMSViewer::ViewerDocument document;
    QVERIFY(document.adopt(std::move(result)));
    QVERIFY(document.hasIonMobility());
    QCOMPARE(document.ionMobilityFrameForSpectrum(1)->msLevel, 2U);
    QVERIFY(document.ionMobilityFrameForSpectrum(2) == nullptr);

    OpenMSViewer::IonMobilityPanelWidget panel;
    panel.resize(920, 620);
    panel.show();
    panel.setData(document.experimentHandle(), document.ionMobilityFrames());
    QCOMPARE(panel.frameCount(), std::size_t{2});
    QCOMPARE(panel.selectedSpectrumIndex().value(), std::size_t{0});
    QTRY_VERIFY_WITH_TIMEOUT(!panel.plot()->raster().image.isNull(), 3000);
    panel.setSpectrumIndex(1);
    QCOMPARE(panel.selectedSpectrumIndex().value(), std::size_t{1});
    QTRY_VERIFY_WITH_TIMEOUT(!panel.plot()->raster().image.isNull(), 3000);

    std::optional<std::size_t> activated;
    connect(&panel, &OpenMSViewer::IonMobilityPanelWidget::spectrumActivated,
            &panel, [&](std::size_t index) { activated = index; });
    auto* selector = panel.findChild<QComboBox*>(QStringLiteral("ionMobilityFrameSelector"));
    QVERIFY(selector != nullptr);
    selector->setCurrentIndex(0);
    QCOMPARE(activated.value(), std::size_t{0});

    // The mobilogram smoothing toggle is present and drives the plot without error.
    auto* smooth = panel.findChild<QCheckBox*>(QStringLiteral("ionMobilitySmooth"));
    QVERIFY(smooth != nullptr);
    smooth->setChecked(true);
    QVERIFY(!panel.grab().isNull());
  }

  void detectsFiltersAndNavigatesFaimsChannels()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("faims.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), OpenMSViewer::TestData::faimsExperiment());

    auto result = OpenMSViewer::ViewerDocument::readMzML(path);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.faimsChannels.size(), std::size_t{2});
    QCOMPARE(result.faimsChannels[0].compensationVoltage, -50.0);
    QCOMPARE(result.faimsChannels[1].compensationVoltage, -40.0);
    QCOMPARE(result.faimsChannels[0].tic.size(), std::size_t{2});
    QCOMPARE(result.faimsChannels[1].tic.size(), std::size_t{2});
    QCOMPARE(result.faimsExperiments.size(), std::size_t{2});
    QCOMPARE(result.faimsExperiments[0]->size(), std::size_t{2});

    OpenMSViewer::ViewerDocument document;
    QVERIFY(document.adopt(std::move(result)));
    QVERIFY(document.hasFaims());
    QCOMPARE(document.faimsExperiment(1)->size(), std::size_t{2});

    OpenMSViewer::FaimsPanelWidget panel;
    panel.resize(850, 520);
    panel.show();
    panel.setChannels(document.faimsChannels());
    panel.setExperiments({document.faimsExperiment(0), document.faimsExperiment(1)},
                         document.bounds());
    panel.setPeakMapRange(document.bounds());
    QCOMPARE(panel.channelCount(), std::size_t{2});
    QCOMPARE(panel.selectedChannel(), -1);
    auto* selector = panel.findChild<QComboBox*>(QStringLiteral("faimsChannelSelector"));
    auto* table = panel.findChild<QTableWidget*>(QStringLiteral("faimsChannelTable"));
    QVERIFY(selector != nullptr);
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 2);
    int selected = -2;
    connect(&panel, &OpenMSViewer::FaimsPanelWidget::channelSelected,
            &panel, [&](int index) { selected = index; });
    selector->setCurrentIndex(2);
    QCOMPARE(selected, 1);
    QCOMPARE(panel.selectedChannel(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(panel.peakMaps()->images().size(), std::size_t{2}, 3000);
    QVERIFY(!panel.peakMaps()->images()[0].isNull());
    QVERIFY(!panel.peakMaps()->images()[1].isNull());
    QTest::mouseClick(panel.peakMaps(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(panel.peakMaps()->width() / 4, panel.peakMaps()->height() / 2));
    QCOMPARE(panel.selectedChannel(), 0);

    std::optional<std::size_t> spectrum;
    connect(&panel, &OpenMSViewer::FaimsPanelWidget::spectrumActivated,
            &panel, [&](std::size_t index) { spectrum = index; });
    QTest::mouseClick(panel.plot(), Qt::LeftButton, Qt::NoModifier,
                      panel.plot()->rect().center());
    QVERIFY(spectrum.has_value());
    QVERIFY(*spectrum == 0 || *spectrum == 2);
  }

  void synchronizesMobilityFrameInMainWindow()
  {
    QSettings().clear();  // start from the default layout, not persisted state
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("mobility.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), OpenMSViewer::TestData::ionMobilityExperiment());

    OpenMSViewer::MainWindow window;
    window.resize(1300, 860);
    window.show();
    window.loadFile(path);
    auto* panel = window.findChild<OpenMSViewer::IonMobilityPanelWidget*>();
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("ionMobilityDock"));
    auto* spectrum = window.findChild<OpenMSViewer::SpectrumWidget*>();
    QVERIFY(panel != nullptr);
    QVERIFY(dock != nullptr);
    QVERIFY(spectrum != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(panel->frameCount(), std::size_t{2}, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(dock->isVisible(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!panel->plot()->raster().image.isNull(), 3000);
    auto* selector = panel->findChild<QComboBox*>(QStringLiteral("ionMobilityFrameSelector"));
    QVERIFY(selector != nullptr);
    selector->setCurrentIndex(1);
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{1});
    QCOMPARE(panel->selectedSpectrumIndex().value(), std::size_t{1});
  }

  void keepsFaimsSelectionConsistentAcrossTablesAndNavigation()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("faims-main.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), OpenMSViewer::TestData::faimsExperiment());

    OpenMSViewer::MainWindow window;
    window.resize(1300, 860);
    window.show();
    window.loadFile(path);
    auto* panel = window.findChild<OpenMSViewer::FaimsPanelWidget*>();
    auto* spectrum = window.findChild<OpenMSViewer::SpectrumWidget*>();
    auto* spectra = window.findChild<OpenMSViewer::SpectrumTableWidget*>();
    auto* selector = panel
      ? panel->findChild<QComboBox*>(QStringLiteral("faimsChannelSelector")) : nullptr;
    auto* table = spectra
      ? spectra->findChild<QTableView*>(QStringLiteral("spectraTable")) : nullptr;
    QVERIFY(panel != nullptr);
    QVERIFY(spectrum != nullptr);
    QVERIFY(selector != nullptr);
    QVERIFY(table != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 4, 5000);

    selector->setCurrentIndex(1);
    QTRY_COMPARE(panel->selectedChannel(), 0);
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{0});

    const QModelIndex otherCv = table->model()->index(1, 0);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                      table->visualRect(otherCv).center());
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{1});
    QTRY_COMPARE(panel->selectedChannel(), 1);

    QTest::keyClick(&window, Qt::Key_PageDown);
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{3});
    QCOMPARE(panel->selectedChannel(), 1);
  }
};

int runMobilityFaimsWorkflowTests(int argc, char** argv)
{
  MobilityFaimsWorkflowTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "MobilityFaimsWorkflowTest.moc"
