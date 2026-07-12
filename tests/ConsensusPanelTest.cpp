#include "model/ConsensusDocument.h"
#include "widgets/ConsensusPanel.h"

#include <OpenMS/FORMAT/FileHandler.h>
#include <OpenMS/KERNEL/ConsensusFeature.h>
#include <OpenMS/KERNEL/ConsensusMap.h>
#include <OpenMS/KERNEL/FeatureHandle.h>

#include <QAbstractItemModel>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

class ConsensusPanelTest final : public QObject
{
  Q_OBJECT

private:
  static OpenMS::FeatureHandle handle(OpenMS::UInt64 mapIndex, double intensity)
  {
    OpenMS::Peak2D point;
    point.setRT(1000.0);
    point.setMZ(500.0);
    point.setIntensity(static_cast<OpenMS::Peak2D::IntensityType>(intensity));
    return OpenMS::FeatureHandle(mapIndex, point, 0);
  }

  static QString buildConsensus(const QTemporaryDir& dir)
  {
    OpenMS::ConsensusMap map;
    for (int m = 0; m < 3; ++m)
    {
      OpenMS::ConsensusMap::ColumnHeader header;
      header.label = "s" + std::to_string(m);
      header.size = 1;
      map.getColumnHeaders()[m] = header;
    }
    OpenMS::ConsensusFeature identified;
    identified.setRT(1000.0);
    identified.setMZ(500.0);
    identified.setCharge(2);
    identified.setIntensity(3000.0);
    identified.insert(handle(0, 1000.0));
    identified.insert(handle(1, 2000.0));
    OpenMS::PeptideIdentification pid;
    OpenMS::PeptideHit hit;
    hit.setSequence(OpenMS::AASequence::fromString("ELVISK"));
    pid.insertHit(hit);
    identified.getPeptideIdentifications().push_back(pid);
    map.push_back(identified);

    OpenMS::ConsensusFeature sparse;
    sparse.setRT(1500.0);
    sparse.setMZ(650.0);
    sparse.setCharge(3);
    sparse.setIntensity(400.0);
    sparse.insert(handle(0, 400.0));
    map.push_back(sparse);

    const QString path = dir.filePath(QStringLiteral("panel.consensusXML"));
    OpenMS::FileHandler().storeConsensusFeatures(path.toStdString(), map);
    return path;
  }

private slots:
  void populatesTableChartAndFilters()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto result = OpenMSViewer::ConsensusDocument::read(buildConsensus(dir));
    QVERIFY2(result.succeeded(), qPrintable(result.error));

    OpenMSViewer::ConsensusPanel panel;
    panel.resize(820, 520);
    OpenMSViewer::ConsensusLoadResult copy = result;  // setData consumes its args
    panel.setData(copy.map, copy.features, copy.columns, copy.experimentType);

    auto* table = panel.findChild<QTableView*>(QStringLiteral("consensusTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->model()->rowCount(), 2);
    QVERIFY(!panel.grab().isNull());  // table + per-map chart paint (selection auto-set)

    // The min-maps (coverage) filter keeps only the 2-map feature; the 1-map one drops.
    auto* minMaps = panel.findChild<QSpinBox*>(QStringLiteral("consensusMinMaps"));
    QVERIFY(minMaps != nullptr);
    minMaps->setValue(2);
    QCOMPARE(table->model()->rowCount(), 1);
    minMaps->setValue(0);
    QCOMPARE(table->model()->rowCount(), 2);
  }
};

int runConsensusPanelTests(int argc, char** argv)
{
  ConsensusPanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "ConsensusPanelTest.moc"
