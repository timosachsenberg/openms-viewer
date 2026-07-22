#include "TestData.h"

#include "export/MzMLExportDialog.h"
#include "export/MzMLExporter.h"
#include "export/PlotExporter.h"

#include <OpenMS/FORMAT/MzMLFile.h>

#include <QImage>
#include <QFileInfo>
#include <QPainter>
#include <QPalette>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

class ExportWorkflowTest final : public QObject
{
  Q_OBJECT

private slots:
  void filtersRtMzLevelsAndAlignedArrays()
  {
    const auto source = std::make_shared<OpenMS::MSExperiment>(
      OpenMSViewer::TestData::ionMobilityExperiment());
    OpenMSViewer::MzMLExportFilter filter;
    filter.range = {9.0, 11.5, 450.0, 650.0};
    filter.msLevels = {1, 2};
    const auto output = OpenMSViewer::MzMLExporter::filter(*source, filter);
    QCOMPARE(output->size(), std::size_t{2});
    QCOMPARE((*output)[0].size(), std::size_t{2});
    QCOMPARE((*output)[1].size(), std::size_t{1});
    QVERIFY((*output)[0].containsIMData());
    QVERIFY((*output)[1].containsIMData());
    QCOMPARE((*output)[0].getFloatDataArrays().front().size(), (*output)[0].size());
    QCOMPARE((*output)[1].getFloatDataArrays().front().size(), (*output)[1].size());
    QCOMPARE(source->size(), std::size_t{3});
    QCOMPARE((*source)[0].size(), std::size_t{3});
  }

  void writesReloadableFaimsFilteredMzML()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("filtered.mzML"));
    auto source = std::make_shared<OpenMS::MSExperiment>(OpenMSViewer::TestData::faimsExperiment());
    OpenMSViewer::MzMLExportFilter filter;
    filter.range = {0.0, 50.0, 350.0, 550.0};
    filter.msLevels = {1};
    filter.faimsCompensationVoltage = -50.0;
    const auto result = OpenMSViewer::MzMLExporter::write(source, path, filter);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.spectrumCount, std::size_t{2});
    QCOMPARE(result.peakCount, std::size_t{4});
    QVERIFY(QFileInfo::exists(path));

    OpenMS::MSExperiment reloaded;
    OpenMS::MzMLFile().load(path.toStdString(), reloaded);
    QCOMPARE(reloaded.size(), std::size_t{2});
    QCOMPARE(reloaded[0].getRT(), 10.0);
    QCOMPARE(reloaded[1].getRT(), 30.0);

    OpenMSViewer::MzMLExportFilter invalid = filter;
    invalid.msLevels.clear();
    const auto failed = OpenMSViewer::MzMLExporter::write(source,
      directory.filePath(QStringLiteral("invalid.mzML")), invalid);
    QVERIFY(!failed.succeeded());
    QVERIFY(failed.error.contains(QStringLiteral("MS level")));
  }

  void capturesWidgetAsPngAndBuildsDialogFilter()
  {
    class PaintedWidget final : public QWidget
    {
    protected:
      void paintEvent(QPaintEvent*) override
      {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(20, 40, 80));
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("OpenMS"));
      }
    } widget;
    widget.resize(320, 180);
    widget.show();
    QTest::qWait(10);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("plot.png"));
    QCOMPARE(OpenMSViewer::PlotExporter::writePng(widget, path), QString());
    const QImage image(path);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(320, 180));

    // SVG (vector) export: writes a well-formed, non-empty <svg> document.
    const QString svgPath = directory.filePath(QStringLiteral("plot.svg"));
    QCOMPARE(OpenMSViewer::PlotExporter::writeSvg(widget, svgPath), QString());
    QFile svgFile(svgPath);
    QVERIFY(svgFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray svg = svgFile.readAll();
    QVERIFY(svg.size() > 0);
    QVERIFY(svg.contains("<svg"));
    QVERIFY(svg.contains("</svg>"));

    const auto experiment = OpenMSViewer::TestData::experiment();
    std::vector<OpenMSViewer::SpectrumRecord> spectra;
    for (std::size_t index = 0; index < experiment.size(); ++index)
      spectra.push_back({.index = index, .rt = experiment[index].getRT(),
                         .msLevel = experiment[index].getMSLevel(),
                         .peakCount = experiment[index].size()});
    OpenMSViewer::MzMLExportDialog dialog(
      {10.0, 20.0, 200.0, 700.0}, {11.0, 19.0, 300.0, 600.0},
      {1, 2}, spectra, std::nullopt);
    const auto filter = dialog.filter();
    QCOMPARE(filter.range.rtMin, 11.0);
    QCOMPARE(filter.range.rtMax, 19.0);
    QCOMPARE(filter.range.mzMin, 300.0);
    QCOMPARE(filter.range.mzMax, 600.0);
    QCOMPARE(filter.msLevels.size(), std::size_t{2});
  }

  // A plot whose canvas follows palette() must export on the LIGHT theme even when
  // the widget is currently on a dark palette, and its palette must be restored.
  void exportsOnLightThemeRegardlessOfWidgetPalette()
  {
    class PaletteWidget final : public QWidget
    {
    protected:
      void paintEvent(QPaintEvent*) override
      {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Base));
      }
    } widget;
    widget.resize(120, 80);
    QPalette dark = widget.palette();
    dark.setColor(QPalette::Base, QColor(20, 22, 28));  // dark canvas
    widget.setPalette(dark);
    widget.show();
    QTest::qWait(10);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("dark-plot.png"));
    QCOMPARE(OpenMSViewer::PlotExporter::writePng(widget, path), QString());
    const QImage image(path);
    QVERIFY(!image.isNull());
    // The exported canvas is the light standard palette, not the widget's dark Base.
    QVERIFY(image.pixelColor(2, 2).lightnessF() > 0.7);
    // The widget's own palette is restored afterwards, never left forced light.
    QCOMPARE(widget.palette().color(QPalette::Base), QColor(20, 22, 28));
  }
};

int runExportWorkflowTests(int argc, char** argv)
{
  ExportWorkflowTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "ExportWorkflowTest.moc"
