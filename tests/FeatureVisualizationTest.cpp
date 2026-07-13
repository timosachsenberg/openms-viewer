// Feature-visualization tests driven by REAL vendored sample data
// (tests/data/BSA1_F1.*): a BSA tryptic-digest run with 256 detected features
// and 48 peptide IDs. Exercises annotated feature visualization and the
// feature-table <-> peak-map interaction the same way pyopenms-viewer does.

#include "MainWindow.h"
#include "model/ViewerDocument.h"
#include "widgets/FeatureTableWidget.h"
#include "widgets/IdentificationTableWidget.h"
#include "widgets/PeakMapWidget.h"

#include <OpenMS/FORMAT/FeatureXMLFile.h>
#include <OpenMS/KERNEL/FeatureMap.h>

#include <QDoubleSpinBox>
#include <QTableView>
#include <QTest>

#ifndef OPENMS_VIEWER_TEST_DATA_DIR
#error "OPENMS_VIEWER_TEST_DATA_DIR must be defined by the build"
#endif

namespace
{
  QString dataPath(const char* name)
  {
    return QStringLiteral("%1/%2").arg(QString::fromUtf8(OPENMS_VIEWER_TEST_DATA_DIR),
                                       QString::fromUtf8(name));
  }
}

class FeatureVisualizationTest final : public QObject
{
  Q_OBJECT

private:
  // Load the real BSA trio into a MainWindow and wait for the async loads to land.
  // Returns the populated window's key panels via out-params.
  static void loadBsa(OpenMSViewer::MainWindow& window,
                      OpenMSViewer::FeatureTableWidget*& features,
                      QTableView*& featureTable,
                      OpenMSViewer::PeakMapWidget*& peakMap)
  {
    window.resize(1200, 820);
    window.show();
    window.loadFiles({dataPath("BSA1_F1.mzML"), dataPath("BSA1_F1.featureXML"),
                      dataPath("BSA1_F1.idXML")});

    features = window.findChild<OpenMSViewer::FeatureTableWidget*>();
    peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    QVERIFY(features != nullptr);
    QVERIFY(peakMap != nullptr);
    featureTable = features->findChild<QTableView*>(QStringLiteral("featureTable"));
    QVERIFY(featureTable != nullptr);

    // The mzML is ~5.5 MB / 767 spectra; loads run on a worker thread, so wait.
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->hasExperiment(), 20000);
    QTRY_COMPARE_WITH_TIMEOUT(featureTable->model()->rowCount(), 256, 20000);
  }

private slots:
  // Loading the trio populates the peak map, the 256-row feature table, and the
  // identification table, and links IDs to spectra.
  void loadsRealBsaTrioAndPopulatesPanels()
  {
    OpenMSViewer::MainWindow window;
    OpenMSViewer::FeatureTableWidget* features = nullptr;
    QTableView* featureTable = nullptr;
    OpenMSViewer::PeakMapWidget* peakMap = nullptr;
    loadBsa(window, features, featureTable, peakMap);

    // loadBsa already asserted the 256-row feature table and a loaded experiment.
    // The identification table populates too (peptide IDs from the idXML).
    auto* idWidget = window.findChild<OpenMSViewer::IdentificationTableWidget*>();
    QVERIFY(idWidget != nullptr);
    auto* idTable = idWidget->findChild<QTableView*>(QStringLiteral("identificationTable"));
    QVERIFY(idTable != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(idTable->model()->rowCount() > 0, 20000);
  }

  // Selecting a feature in the table drives the peak-map selection, and selecting
  // one in the plot drives the table selection — the cross-panel sync stays
  // consistent in both directions.
  void featureTableAndPlotStayInSync()
  {
    OpenMSViewer::MainWindow window;
    OpenMSViewer::FeatureTableWidget* features = nullptr;
    QTableView* featureTable = nullptr;
    OpenMSViewer::PeakMapWidget* peakMap = nullptr;
    loadBsa(window, features, featureTable, peakMap);

    // Table -> plot: clicking the first row selects that feature on the peak map.
    const QModelIndex firstRow = featureTable->model()->index(0, 0);
    QVERIFY(firstRow.isValid());
    const auto firstIndex = firstRow.data(Qt::UserRole).toULongLong();
    featureTable->scrollTo(firstRow);
    QTest::mouseClick(featureTable->viewport(), Qt::LeftButton, Qt::NoModifier,
                      featureTable->visualRect(firstRow).center());
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->selectedFeature().has_value(), 3000);
    QCOMPARE(qulonglong{*peakMap->selectedFeature()}, firstIndex);

    // Keyboard navigation in the table keeps the plot selection in step.
    featureTable->setFocus();
    const auto secondIndex = featureTable->model()->index(1, 0).data(Qt::UserRole).toULongLong();
    QTest::keyClick(featureTable, Qt::Key_Down);
    QTRY_COMPARE(qulonglong{*peakMap->selectedFeature()}, secondIndex);

    // Plot -> table: click on the peak map where a known feature sits (coordinates
    // read from the FeatureXML; FeatureRecord.index == FeatureMap position). The
    // resulting selection must agree across both panels — whichever feature the hit
    // test picks, the table highlights the same one.
    OpenMS::FeatureMap map;
    OpenMS::FeatureXMLFile().load(dataPath("BSA1_F1.featureXML").toStdString(), map);
    QVERIFY(map.size() == 256);
    const auto& target = map[100];

    peakMap->resetView();
    peakMap->setShowFeatureCentroids(true);
    const QPoint spot = peakMap->mapDataToWidget(target.getRT(), target.getMZ()).toPoint();
    QTest::mouseMove(peakMap, spot);
    QTest::mouseClick(peakMap, Qt::LeftButton, Qt::NoModifier, spot);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->selectedFeature().has_value(), 3000);

    QTRY_COMPARE(featureTable->selectionModel()->selectedRows().size(), 1);
    const auto tableSelected =
      featureTable->selectionModel()->selectedRows().front().data(Qt::UserRole).toULongLong();
    QCOMPARE(tableSelected, qulonglong{*peakMap->selectedFeature()});
  }

  // The feature overlays (centroids, bounding boxes, convex hulls) render without
  // error over the real data, and zooming to a feature keeps the widget paintable.
  void featureOverlaysRenderWithoutError()
  {
    OpenMSViewer::MainWindow window;
    OpenMSViewer::FeatureTableWidget* features = nullptr;
    QTableView* featureTable = nullptr;
    OpenMSViewer::PeakMapWidget* peakMap = nullptr;
    loadBsa(window, features, featureTable, peakMap);

    peakMap->setShowFeatureCentroids(true);
    peakMap->setShowFeatureBounds(true);
    peakMap->setShowFeatureHulls(true);
    peakMap->resetView();

    const QPixmap full = peakMap->grab();
    QVERIFY(!full.isNull());
    QCOMPARE(full.size(), peakMap->size());

    // Zoom to a single feature's neighbourhood and re-grab: the overlays reproject
    // and the widget still paints a frame of the expected size.
    OpenMS::FeatureMap map;
    OpenMS::FeatureXMLFile().load(dataPath("BSA1_F1.featureXML").toStdString(), map);
    const auto& f = map[50];
    peakMap->setRtRange(f.getRT() - 20.0, f.getRT() + 20.0);
    const QPixmap zoomed = peakMap->grab();
    QVERIFY(!zoomed.isNull());
    QCOMPARE(zoomed.size(), peakMap->size());
  }

  // The feature table's intensity filter narrows the visible set, and clearing it
  // restores all 256 — the table<->filter plumbing works on real data.
  void intensityFilterNarrowsFeatureSet()
  {
    OpenMSViewer::MainWindow window;
    OpenMSViewer::FeatureTableWidget* features = nullptr;
    QTableView* featureTable = nullptr;
    OpenMSViewer::PeakMapWidget* peakMap = nullptr;
    loadBsa(window, features, featureTable, peakMap);

    const auto spins = features->findChildren<QDoubleSpinBox*>();
    QVERIFY(!spins.isEmpty());
    QDoubleSpinBox* minIntensity = spins.front();

    const int before = featureTable->model()->rowCount();
    QCOMPARE(before, 256);
    minIntensity->setValue(minIntensity->maximum());  // impossibly high -> hides all
    QTRY_VERIFY(featureTable->model()->rowCount() < before);
    minIntensity->setValue(0.0);                       // clear -> everything is back
    QTRY_COMPARE(featureTable->model()->rowCount(), 256);
  }
};

int runFeatureVisualizationTests(int argc, char** argv)
{
  FeatureVisualizationTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "FeatureVisualizationTest.moc"
