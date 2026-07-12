#include "TestData.h"

#include "MainWindow.h"
#include "widgets/FeatureTableWidget.h"
#include "widgets/PeakMapWidget.h"

#include <OpenMS/FORMAT/FeatureXMLFile.h>
#include <OpenMS/FORMAT/MzMLFile.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

class MainWindowFeatureWorkflowTest final : public QObject
{
  Q_OBJECT

private slots:
  void loadsFiltersAndSynchronizesSelection()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mzmlPath = directory.filePath(QStringLiteral("small.mzML"));
    const QString featurePath = directory.filePath(QStringLiteral("small.featureXML"));
    OpenMS::MzMLFile().store(mzmlPath.toStdString(), OpenMSViewer::TestData::experiment());
    auto featureMap = OpenMSViewer::TestData::featureMap();
    featureMap[0].setRT(12.0);
    featureMap[0].setMZ(500.0);
    featureMap[0].getConvexHulls().clear();
    featureMap[1].setRT(18.0);
    featureMap[1].setMZ(550.0);
    featureMap.updateRanges();
    OpenMS::FeatureXMLFile().store(featurePath.toStdString(), featureMap);

    OpenMSViewer::MainWindow window;
    window.resize(1100, 760);
    window.show();
    window.loadFiles({mzmlPath, featurePath});

    auto* featureWidget = window.findChild<OpenMSViewer::FeatureTableWidget*>();
    auto* table = featureWidget ? featureWidget->findChild<QTableView*>() : nullptr;
    auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    QVERIFY(featureWidget != nullptr);
    QVERIFY(table != nullptr);
    QVERIFY(peakMap != nullptr);

    QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->hasExperiment(), 5000);

    const QModelIndex firstRow = table->model()->index(0, 0);
    QVERIFY(firstRow.isValid());
    table->scrollTo(firstRow);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                      table->visualRect(firstRow).center());
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->selectedFeature().has_value(), 2000);
    QCOMPARE(*peakMap->selectedFeature(), std::size_t{0});

    table->setFocus();
    QTest::keyClick(table, Qt::Key_Down);
    QTRY_COMPARE(*peakMap->selectedFeature(), std::size_t{1});
    QTest::keyClick(table, Qt::Key_Up);
    QTRY_COMPARE(*peakMap->selectedFeature(), std::size_t{0});

    peakMap->resetView();
    const QPoint secondFeature = peakMap->mapDataToWidget(18.0, 550.0).toPoint();
    QTest::mouseMove(peakMap, secondFeature);
    QTest::mouseClick(peakMap, Qt::LeftButton, Qt::NoModifier, secondFeature);
    QTRY_COMPARE(*peakMap->selectedFeature(), std::size_t{1});
    QTRY_COMPARE(table->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(table->selectionModel()->selectedRows().front().data(Qt::UserRole).toULongLong(), qulonglong{1});

    const auto intensityInputs = featureWidget->findChildren<QDoubleSpinBox*>();
    QCOMPARE(intensityInputs.size(), 2);
    intensityInputs[0]->setValue(500.0);
    QTRY_COMPARE(table->model()->rowCount(), 1);
    intensityInputs[0]->setValue(2000.0);
    QTRY_COMPARE(table->model()->rowCount(), 0);

    intensityInputs[0]->setValue(0.0);
    auto* charge = featureWidget->findChild<QComboBox*>();
    QVERIFY(charge != nullptr);
    charge->setCurrentIndex(2);
    QTRY_COMPARE(table->model()->rowCount(), 1);
  }

  // Edit mode: clicking empty space creates a feature; Delete removes the selected one.
  void editModeCreatesAndDeletesFeatures()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mzmlPath = directory.filePath(QStringLiteral("edit.mzML"));
    OpenMS::MzMLFile().store(mzmlPath.toStdString(), OpenMSViewer::TestData::experiment());

    OpenMSViewer::MainWindow window;
    window.resize(1100, 760);
    window.show();
    window.loadFiles({mzmlPath});
    auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    QVERIFY(peakMap != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->hasExperiment(), 5000);

    peakMap->setInteractionMode(3);  // Edit
    peakMap->resetView();
    const QPoint spot = peakMap->mapDataToWidget(15.0, 500.0).toPoint();
    QTest::mouseClick(peakMap, Qt::LeftButton, Qt::NoModifier, spot);  // empty → create

    auto* featureWidget = window.findChild<OpenMSViewer::FeatureTableWidget*>();
    QVERIFY(featureWidget != nullptr);
    auto* table = featureWidget->findChild<QTableView*>();
    QVERIFY(table != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->selectedFeature().has_value(), 2000);

    peakMap->setFocus();
    QTest::keyClick(peakMap, Qt::Key_Delete);  // selected feature → delete
    QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 0, 3000);
  }
};

int runMainWindowFeatureWorkflowTests(int argc, char** argv)
{
  MainWindowFeatureWorkflowTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "MainWindowFeatureWorkflowTest.moc"
