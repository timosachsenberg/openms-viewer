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
#include "widgets/RowStackWidget.h"
#include <QMenu>
#include <QSettings>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <array>
#include <tuple>

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
    auto* diaWindows = panel.findChild<QCheckBox*>(QStringLiteral("ionMobilityDiaWindows"));
    auto* display = panel.findChild<QToolButton*>(QStringLiteral("ionMobilityDisplayOptions"));
    auto* overlays = panel.findChild<QToolButton*>(QStringLiteral("ionMobilityOverlayOptions"));
    QVERIFY(smooth != nullptr);
    QVERIFY(diaWindows != nullptr);
    QVERIFY(display != nullptr && display->menu() != nullptr);
    QVERIFY(overlays != nullptr && overlays->menu() != nullptr);
    QVERIFY(display->menu()->isAncestorOf(smooth));
    QVERIFY(overlays->menu()->isAncestorOf(diaWindows));
    smooth->setChecked(true);
    QVERIFY(!panel.grab().isNull());
  }

  // diaPASEF window groups: distinct MS2 isolation windows (m/z × ion mobility) are
  // reconstructed from the frames and deduplicated across acquisition cycles.
  void diaWindowsReconstructAcrossCycles()
  {
    std::vector<OpenMSViewer::IonMobilityFrameRecord> frames;
    OpenMSViewer::IonMobilityFrameRecord ms1;  // survey frame spanning the full space
    ms1.spectrumIndex = 0;
    ms1.rt = 0.0;
    ms1.msLevel = 1;
    ms1.mobilityMin = 0.6;
    ms1.mobilityMax = 1.4;
    ms1.mzMin = 400.0;
    ms1.mzMax = 900.0;
    frames.push_back(ms1);

    // Two cycles of two window groups; each group has two windows at one frame RT.
    std::size_t index = 1;
    for (int cycle = 0; cycle < 2; ++cycle)
    {
      const double rtA = 0.1 + cycle * 1.0;  // group A frame RT
      const double rtB = 0.2 + cycle * 1.0;  // group B frame RT
      // Cycle 2 has slightly different OBSERVED ion-mobility extrema (peak jitter);
      // the reconstruction must still collapse them to the same windows.
      const double jitter = cycle * 0.008;
      const std::array<std::tuple<double, double, double, double, double>, 4> windows{{
        {rtA, 425.0, 475.0, 0.6, 0.9}, {rtA, 525.0, 575.0, 0.9, 1.2},
        {rtB, 625.0, 675.0, 0.6, 0.9}, {rtB, 725.0, 775.0, 0.9, 1.2}}};
      for (const auto& [rt, mzLo, mzHi, imLo, imHi] : windows)
      {
        OpenMSViewer::IonMobilityFrameRecord frame;
        frame.spectrumIndex = index++;
        frame.rt = rt;
        frame.msLevel = 2;
        frame.mobilityMin = imLo + jitter;
        frame.mobilityMax = imHi + jitter;
        frame.mzMin = 400.0;
        frame.mzMax = 900.0;
        frame.precursorMz = (mzLo + mzHi) / 2.0;
        frame.isolationWindowLower = mzLo;
        frame.isolationWindowUpper = mzHi;
        frames.push_back(frame);
      }
    }

    OpenMSViewer::IonMobilityPlotWidget plot;
    plot.resize(600, 400);
    plot.setData(std::make_shared<OpenMS::MSExperiment>(), frames);
    QVERIFY(plot.hasDiaWindows());
    QCOMPARE(plot.diaWindowCount(), std::size_t{4});  // 8 MS2 frames → 4 distinct windows
    plot.setShowDiaWindows(true);
    plot.setFramePosition(0);  // MS1 frame → view spans the whole scheme
    QVERIFY(!plot.grab().isNull());
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
    auto* dock = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("ionMobility"));
    auto* spectrum = window.findChild<OpenMSViewer::SpectrumWidget*>();
    QVERIFY(panel != nullptr);
    QVERIFY(dock != nullptr);
    QVERIFY(spectrum != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(panel->frameCount(), std::size_t{2}, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(dock->isShown(), 3000);
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
