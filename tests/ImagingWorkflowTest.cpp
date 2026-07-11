#include "MainWindow.h"
#include "model/ImagingDocument.h"
#include "widgets/ImagingPanelWidget.h"
#include "widgets/SpectrumWidget.h"

#include <OpenMS/FORMAT/ImzMLFile.h>
#include <OpenMS/IMAGING/MSImagingExperiment.h>
#include <OpenMS/IMAGING/MSImagingGeometry.h>
#include <OpenMS/KERNEL/Peak1D.h>

#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

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
    QVERIFY(mz != nullptr);
    QVERIFY(extract != nullptr);
    QVERIFY(mode != nullptr);
    mz->setValue(100.0);
    extract->click();
    QTRY_COMPARE_WITH_TIMEOUT(mode->currentIndex(), 1, 3000);
    QVERIFY(!panel.imageWidget()->renderedImage().isNull());

    QPushButton* addOverlay = nullptr;
    for (QPushButton* button : panel.findChildren<QPushButton*>())
      if (button->text() == QStringLiteral("Add to overlay")) addOverlay = button;
    QVERIFY(addOverlay != nullptr);
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
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("imagingDock"));
    auto* spectrum = window.findChild<OpenMSViewer::SpectrumWidget*>();
    QVERIFY(panel != nullptr);
    QVERIFY(dock != nullptr);
    QVERIFY(spectrum != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(panel->hasData(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(dock->isVisible(), 3000);
    QCOMPARE(panel->summary().pixels.size(), std::size_t{4});
    QCOMPARE(spectrum->spectrumIndex(), std::size_t{0});
    dock->raise();
    QTest::qWait(30);
    const QPoint lowerRight = panel->imageWidget()->rect().center()
      + QPoint(panel->imageWidget()->width() / 8, panel->imageWidget()->height() / 8);
    QTest::mouseClick(panel->imageWidget(), Qt::LeftButton, Qt::NoModifier, lowerRight);
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
