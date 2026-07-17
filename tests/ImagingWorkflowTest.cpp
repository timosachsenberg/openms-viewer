#include "MainWindow.h"
#include "model/ImagingDocument.h"
#include "widgets/ImagingPanelWidget.h"
#include "widgets/SpectrumWidget.h"

#include <OpenMS/FORMAT/ImzMLFile.h>
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
    const auto aggregate =
      result.store->aggregateSpectrum(result.summary.mzMin, result.summary.mzMax, 2000);
    QCOMPARE(aggregate.mz.size(), std::size_t{2});
    QCOMPARE(aggregate.mean.size(), std::size_t{2});
    QCOMPARE(aggregate.maxIntensity.size(), std::size_t{2});
    // Representative m/z is the intensity-weighted peak location (exactly 100/200),
    // NOT the bin centre (~100.025) — a ppm extraction around it must hit the peak.
    QVERIFY(qAbs(aggregate.mz.front() - 100.0) < 1e-3);
    QVERIFY(qAbs(aggregate.mean.front() - 25.0) < 1e-6);        // (10+20+30+40)/4
    QVERIFY(qAbs(aggregate.maxIntensity.front() - 40.0) < 1e-6);
    QVERIFY(qAbs(aggregate.mz.back() - 200.0) < 1e-3);
    QVERIFY(qAbs(aggregate.mean.back() - 12.5) < 1e-6);         // (20+15+10+5)/4
    QVERIFY(qAbs(aggregate.maxIntensity.back() - 20.0) < 1e-6);

    // End-to-end: the reported m/z round-trips through a 10 ppm extraction (a bin
    // centre would sit outside the ppm window and extract an empty image).
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
    // Click the exact centre of the bottom-right pixel (x=1, y=1 → spectrum 3),
    // computed from the on-screen image rect so it is robust to the widget size /
    // panel layout rather than guessing a fraction of the widget.
    const QRect image = panel->imageWidget()->imageRect();
    const QPoint bottomRightPixel = image.topLeft()
      + QPoint(image.width() * 3 / 4, image.height() * 3 / 4);
    QTest::mouseClick(panel->imageWidget(), Qt::LeftButton, Qt::NoModifier, bottomRightPixel);
    QTRY_COMPARE(spectrum->spectrumIndex(), std::size_t{3});
    QCOMPARE(panel->imageWidget()->selectedSpectrum().value(), std::size_t{3});
  }
};

int runImagingWorkflowTests(int argc, char** argv)
{
  ImagingWorkflowTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "ImagingWorkflowTest.moc"
