#include "MainWindow.h"
#include "TestData.h"
#include "model/ImagingDocument.h"
#include "widgets/ImagingPanelWidget.h"
#include "widgets/SpectrumWidget.h"

#include <OpenMS/FORMAT/ImzMLFile.h>
#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/IMAGING/MSImagingExperiment.h>
#include <OpenMS/IMAGING/MSImagingGeometry.h>
#include <OpenMS/KERNEL/Peak1D.h>

#include <QComboBox>
#include "widgets/RowStackWidget.h"
#include <QDoubleSpinBox>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <optional>

namespace
{
  QString writeImagingFixture(const QString& directory)
  {
    OpenMS::MSExperiment spectra;
    for (std::size_t index = 0; index < 4; ++index)
    {
      OpenMS::MSSpectrum spectrum;
      OpenMS::Peak1D first;
      first.setMZ(100.0);
      first.setIntensity(static_cast<float>(10.0 * (index + 1)));
      spectrum.push_back(first);
      OpenMS::Peak1D second;
      second.setMZ(200.0);
      second.setIntensity(static_cast<float>(5.0 * (4 - index)));
      spectrum.push_back(second);
      spectra.addSpectrum(spectrum);
    }
    OpenMS::MSImagingGeometry geometry;
    geometry.setDimensions(2, 2);
    geometry.setPixelSize(25.0, 30.0, "micrometer");
    geometry.addPixel(0, 0, 0);
    geometry.addPixel(1, 0, 1);
    geometry.addPixel(0, 1, 2);
    geometry.addPixel(1, 1, 3);
    OpenMS::MSImagingExperiment imaging;
    imaging.setMSExperiment(spectra);
    imaging.setGeometry(geometry);
    const QString path = directory + QStringLiteral("/small.imzML");
    OpenMS::ImzMLFile().store(path.toStdString(), imaging);
    return path;
  }
}

class ImagingWorkflowTest final : public QObject
{
  Q_OBJECT

private slots:
  void reportsMissingCompanionFile()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("broken.imzML"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<mzML/>");
    file.close();
    const auto result = OpenMSViewer::ImagingDocument::readImzML(path);
    QVERIFY(!result.succeeded());
    QVERIFY(result.error.contains(QStringLiteral("IBD")));
  }

  void loadsOnDiscExtractsAndInteracts()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeImagingFixture(directory.path());
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("small.ibd"))));
    auto result = OpenMSViewer::ImagingDocument::readImzML(path);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.summary.width, std::uint32_t{2});
    QCOMPARE(result.summary.height, std::uint32_t{2});
    QCOMPARE(result.summary.pixels.size(), std::size_t{4});
    QCOMPARE(result.summary.peakCount, std::size_t{8});
    QCOMPARE(result.summary.pixelSizeX, 25.0);
    QCOMPARE(result.summary.pixelSizeY, 30.0);
    QCOMPARE(result.summary.mzMin, 100.0);
    QCOMPARE(result.summary.mzMax, 200.0);
    QCOMPARE(result.store->spectrum(2).size(), std::size_t{2});
    const OpenMS::IonImage ion = result.store->extractIonImage(100.0, 10.0);
    QCOMPARE(ion.getWidth(), 2U);
    QCOMPARE(ion.getHeight(), 2U);
    QCOMPARE(ion.getIntensity(0, 0), 10.0);
    QCOMPARE(ion.getIntensity(1, 1), 40.0);

    OpenMSViewer::ImagingPanelWidget panel;
    panel.resize(800, 620);
    panel.show();
    panel.setData(result.store, result.summary);
    QVERIFY(panel.hasData());
    QVERIFY(!panel.imageWidget()->renderedImage().isNull());
    QCOMPARE(panel.imageWidget()->renderedImage().size(), QSize(2, 2));
    auto* mz = panel.findChild<QDoubleSpinBox*>(QStringLiteral("imagingMz"));
    auto* extract = panel.findChild<QPushButton*>(QStringLiteral("imagingExtract"));
    auto* mode = panel.findChild<QComboBox*>(QStringLiteral("imagingDisplayMode"));
    auto* display = panel.findChild<QToolButton*>(QStringLiteral("imagingDisplayOptions"));
    auto* overlays = panel.findChild<QToolButton*>(QStringLiteral("imagingOverlayOptions"));
    QVERIFY(mz != nullptr);
    QVERIFY(extract != nullptr);
    QVERIFY(mode != nullptr);
    QVERIFY(display != nullptr && display->menu() != nullptr);
    QVERIFY(overlays != nullptr && overlays->menu() != nullptr);
    QVERIFY(display->menu()->isAncestorOf(mode));
    mz->setValue(100.0);
    extract->click();
    QTRY_COMPARE_WITH_TIMEOUT(mode->currentIndex(), 1, 3000);
    QVERIFY(!panel.imageWidget()->renderedImage().isNull());

    auto* addOverlay = panel.findChild<QPushButton*>(QStringLiteral("imagingAddOverlay"));
    QVERIFY(addOverlay != nullptr);
    QVERIFY(overlays->menu()->isAncestorOf(addOverlay));
    addOverlay->click();
    QCOMPARE(panel.overlayCount(), std::size_t{1});
    QCOMPARE(mode->currentIndex(), 2);

    std::optional<std::size_t> selected;
    connect(&panel, &OpenMSViewer::ImagingPanelWidget::spectrumActivated,
            &panel, [&](std::size_t index) { selected = index; });
    QTest::mouseClick(panel.imageWidget(), Qt::LeftButton, Qt::NoModifier,
                      panel.imageWidget()->rect().center());
    QVERIFY(selected.has_value());
  }

  void computesAggregateSpectrum()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeImagingFixture(directory.path());
    auto result = OpenMSViewer::ImagingDocument::readImzML(path);
    QVERIFY(result.succeeded());

    // Fixture: 4 pixels, m/z 100 intensities {10,20,30,40}, m/z 200 {20,15,10,5}.
    // Log-spaced 5 ppm bins (pyopenms-viewer compute_aggregate); every pixel hits
    // both peaks so mean = sum / hit-count equals sum / pixel-count.
    const auto aggregate =
      result.store->aggregateSpectrum(result.summary.mzMin, result.summary.mzMax, 5.0);
    QCOMPARE(aggregate.mz.size(), std::size_t{2});
    QCOMPARE(aggregate.mean.size(), std::size_t{2});
    QCOMPARE(aggregate.maxIntensity.size(), std::size_t{2});
    // Reported m/z is the log-bin centre (Python centers); with 5 ppm bins around
    // sharp centroid peaks that centre sits within << 1 mDa of the true peak, so
    // a 10 ppm extraction still hits.
    QVERIFY(qAbs(aggregate.mz.front() - 100.0) < 1e-3);
    QVERIFY(qAbs(aggregate.mean.front() - 25.0) < 1e-6);        // (10+20+30+40)/4
    QVERIFY(qAbs(aggregate.maxIntensity.front() - 40.0) < 1e-6);
    QVERIFY(qAbs(aggregate.mz.back() - 200.0) < 1e-3);
    QVERIFY(qAbs(aggregate.mean.back() - 12.5) < 1e-6);         // (20+15+10+5)/4
    QVERIFY(qAbs(aggregate.maxIntensity.back() - 20.0) < 1e-6);

    // End-to-end: bin-centre m/z round-trips through a 10 ppm extraction.
    const OpenMS::IonImage image = result.store->extractIonImage(aggregate.mz.front(), 10.0);
    double total = 0.0;
    for (const double value : image.getData()) total += value;
    QVERIFY(total > 0.0);
  }

  void aggregateSpectrumClickBrowsesToPeak()
  {
    OpenMSViewer::AggregateSpectrumWidget widget;
    widget.resize(600, 200);
    widget.show();
    widget.setSpectrum({150.0, 500.0, 850.0}, {10.0, 90.0, 40.0}, QStringLiteral("agg"));

    std::optional<double> selected;
    connect(&widget, &OpenMSViewer::AggregateSpectrumWidget::peakSelected, &widget,
            [&](double mz) { selected = mz; });

    const QRect plot = widget.rect().adjusted(58, 20, -12, -34);
    const int x = plot.left() + static_cast<int>((500.0 - 150.0) / (850.0 - 150.0) * plot.width());
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(x, plot.center().y()));

    QVERIFY(selected.has_value());   // clicking the middle peak emits its m/z
    QVERIFY(qAbs(*selected - 500.0) < 1.0);
  }

  void loadsAndSynchronizesMainWindow()
  {
    QSettings().clear();  // start from the default layout, not persisted state
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeImagingFixture(directory.path());
    OpenMSViewer::MainWindow window;
    window.resize(1250, 850);
    window.show();
    window.loadFile(path);
    auto* panel = window.findChild<OpenMSViewer::ImagingPanelWidget*>();
    auto* dock = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("imaging"));
    auto* spectrum = window.findChild<OpenMSViewer::SpectrumWidget*>();
    QVERIFY(panel != nullptr);
    QVERIFY(dock != nullptr);
    QVERIFY(spectrum != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(panel->hasData(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(dock->isShown(), 3000);
    QCOMPARE(panel->summary().pixels.size(), std::size_t{4});
    QCOMPARE(spectrum->spectrumIndex(), std::size_t{0});
    dock->raise();
    QTest::qWait(30);
    // Click the centre of geometry pixel (x=1, y=1 → spectrum 3). With
    // origin-lower rendering (pyopenms-viewer / matplotlib), geometry y=1 is at
    // the *top* of the on-screen image.
    const QRect image = panel->imageWidget()->imageRect();
    const QPoint geometryPixel11 = image.topLeft()
      + QPoint(image.width() * 3 / 4, image.height() * 1 / 4);
    QTest::mouseClick(panel->imageWidget(), Qt::LeftButton, Qt::NoModifier, geometryPixel11);
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{3});
    QCOMPARE(panel->imageWidget()->selectedSpectrum().value(), std::size_t{3});

    // Spectrum index spinbox is bidirectional in imaging mode (PR #44 parity).
    auto* spectrumIndex = window.findChild<QSpinBox*>(QStringLiteral("spectrumIndex"));
    QVERIFY(spectrumIndex != nullptr);
    spectrumIndex->setValue(2);   // 1-based UI → spectrum index 1
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{1});
    QCOMPARE(panel->imageWidget()->selectedSpectrum().value(), std::size_t{1});
  }

  void clearsImagingWhenLoadingMzML()
  {
    QSettings().clear();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imzml = writeImagingFixture(directory.path());
    const QString mzml = directory.filePath(QStringLiteral("after.mzML"));
    OpenMS::MzMLFile().store(mzml.toStdString(), OpenMSViewer::TestData::experiment());

    OpenMSViewer::MainWindow window;
    window.resize(1250, 850);
    window.show();
    window.loadFile(imzml);
    auto* panel = window.findChild<OpenMSViewer::ImagingPanelWidget*>();
    auto* dock = window.findChild<OpenMSViewer::PanelHandle*>(QStringLiteral("imaging"));
    QVERIFY(panel != nullptr);
    QVERIFY(dock != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(panel->hasData(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(dock->isShown(), 3000);

    window.loadFile(mzml);
    QTRY_VERIFY_WITH_TIMEOUT(!panel->hasData(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!dock->isShown(), 3000);
  }

  void aggregateBinPpmControlRelaunchesScan()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeImagingFixture(directory.path());
    auto result = OpenMSViewer::ImagingDocument::readImzML(path);
    QVERIFY(result.succeeded());

    OpenMSViewer::ImagingPanelWidget panel;
    panel.resize(800, 620);
    panel.show();
    panel.setData(result.store, result.summary);
    auto* aggregate = panel.findChild<OpenMSViewer::AggregateSpectrumWidget*>(
      QStringLiteral("imagingAggregateSpectrum"));
    auto* binPpm = panel.findChild<QDoubleSpinBox*>(QStringLiteral("imagingBinPpm"));
    auto* colorMap = panel.findChild<QComboBox*>(QStringLiteral("imagingColorMap"));
    QVERIFY(aggregate != nullptr);
    QVERIFY(binPpm != nullptr);
    QVERIFY(colorMap != nullptr);
    QCOMPARE(binPpm->value(), 5.0);
    QCOMPARE(colorMap->count(), 5);   // viridis / plasma / inferno / magma / hot
    // Wait for the initial aggregate sticks, then change bin width and colormap.
    QTRY_VERIFY_WITH_TIMEOUT(aggregate->hasComputedSpectrum(), 3000);
    QVERIFY(aggregate->peakCount() >= 2);
    const auto firstCount = aggregate->peakCount();
    binPpm->setValue(10.0);
    QTRY_COMPARE_WITH_TIMEOUT(binPpm->value(), 10.0, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(aggregate->hasComputedSpectrum(), 3000);
    QVERIFY(aggregate->peakCount() >= 2);
    // Fixture has two sharp peaks; coarser bins still keep both occupied.
    QCOMPARE(aggregate->peakCount(), firstCount);
    colorMap->setCurrentIndex(1);   // plasma — must not drop the imaging session
    QVERIFY(panel.hasData());
    QVERIFY(panel.imageWidget()->renderedImage().width() > 0);
  }
};

int runImagingWorkflowTests(int argc, char** argv)
{
  ImagingWorkflowTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "ImagingWorkflowTest.moc"
