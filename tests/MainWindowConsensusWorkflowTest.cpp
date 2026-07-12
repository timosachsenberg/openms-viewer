#include "TestData.h"

#include "MainWindow.h"
#include "widgets/PeakMapWidget.h"

#include <OpenMS/FORMAT/FileHandler.h>
#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/KERNEL/ConsensusFeature.h>
#include <OpenMS/KERNEL/ConsensusMap.h>
#include <OpenMS/KERNEL/FeatureHandle.h>
#include <OpenMS/KERNEL/MSExperiment.h>

#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

class MainWindowConsensusWorkflowTest final : public QObject
{
  Q_OBJECT

private:
  static void storeConsensus(const QString& path, const QString& runPath)
  {
    OpenMS::ConsensusMap map;
    OpenMS::ConsensusMap::ColumnHeader header;
    header.filename = runPath.toStdString();
    header.label = "run0";
    header.size = 1;
    map.getColumnHeaders()[0] = header;

    OpenMS::ConsensusFeature feature;
    feature.setRT(11.0);
    feature.setMZ(500.2);
    feature.setCharge(2);
    feature.setIntensity(1000.0);
    OpenMS::Peak2D point;
    point.setRT(11.0);
    point.setMZ(500.2);
    point.setIntensity(1000.0);
    feature.insert(OpenMS::FeatureHandle(0, point, 0));
    feature.ensureUniqueId();
    map.push_back(feature);

    OpenMS::FileHandler().storeConsensusFeatures(path.toStdString(), map);
  }

private slots:
  // The consensus overlay lands on the peak map and the selected feature is
  // highlighted through the panel → MainWindow → peak-map wiring.
  void overlayRendersAndHighlightsSelection()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mzmlPath = directory.filePath(QStringLiteral("drill.mzML"));
    const QString consensusPath = directory.filePath(QStringLiteral("drill.consensusXML"));
    OpenMS::MzMLFile().store(mzmlPath.toStdString(), OpenMSViewer::TestData::experiment());
    storeConsensus(consensusPath, mzmlPath);

    OpenMSViewer::MainWindow window;
    window.resize(1200, 820);
    window.show();
    window.loadFiles({mzmlPath, consensusPath});

    auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    auto* table = window.findChild<QTableView*>(QStringLiteral("consensusTable"));
    QVERIFY(peakMap != nullptr);
    QVERIFY(table != nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(peakMap->hasExperiment(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->hasConsensusFeatures(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 1, 5000);
    QVERIFY(!peakMap->grab().isNull());  // the diamond + dashed envelope paint

    // The panel auto-selects its first row on load, which fans out through
    // onConsensusFeatureActivated to highlight the feature on the peak map.
    QTRY_VERIFY_WITH_TIMEOUT(peakMap->selectedConsensus().has_value(), 3000);
    QCOMPARE(*peakMap->selectedConsensus(), std::size_t{0});
  }
};

int runMainWindowConsensusWorkflowTests(int argc, char** argv)
{
  MainWindowConsensusWorkflowTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "MainWindowConsensusWorkflowTest.moc"
