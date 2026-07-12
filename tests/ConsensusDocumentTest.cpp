#include "model/ConsensusDocument.h"

#include <OpenMS/FORMAT/FileHandler.h>
#include <OpenMS/KERNEL/ConsensusFeature.h>
#include <OpenMS/KERNEL/ConsensusMap.h>
#include <OpenMS/KERNEL/FeatureHandle.h>

#include <QTemporaryDir>
#include <QTest>

class ConsensusDocumentTest final : public QObject
{
  Q_OBJECT

private:
  static OpenMS::FeatureHandle handle(OpenMS::UInt64 mapIndex, double rt, double mz,
                                      double intensity, int charge)
  {
    OpenMS::Peak2D point;
    point.setRT(rt);
    point.setMZ(mz);
    point.setIntensity(static_cast<OpenMS::Peak2D::IntensityType>(intensity));
    OpenMS::FeatureHandle result(mapIndex, point, 0);
    result.setCharge(charge);
    return result;
  }

private slots:
  void loadsSummariesColumnsAndHandles()
  {
    OpenMS::ConsensusMap map;
    OpenMS::ConsensusMap::ColumnHeader light;
    light.filename = "light.mzML";
    light.label = "light";
    light.size = 1;
    OpenMS::ConsensusMap::ColumnHeader heavy;
    heavy.filename = "heavy.mzML";
    heavy.label = "heavy";
    heavy.size = 1;
    map.getColumnHeaders()[0] = light;
    map.getColumnHeaders()[1] = heavy;
    map.setExperimentType("labeled_MS1");

    OpenMS::ConsensusFeature paired;
    paired.setRT(100.0);
    paired.setMZ(500.25);
    paired.setCharge(2);
    paired.setIntensity(3000.0);
    paired.setQuality(0.9F);
    paired.insert(handle(0, 99.0, 500.24, 1000.0, 2));
    paired.insert(handle(1, 101.0, 500.26, 2000.0, 2));
    map.push_back(paired);

    OpenMS::ConsensusFeature single;  // covers only map 0 → one missing map
    single.setRT(200.0);
    single.setMZ(650.0);
    single.setCharge(3);
    single.setIntensity(500.0);
    single.insert(handle(0, 200.0, 650.0, 500.0, 3));
    map.push_back(single);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("test.consensusXML"));
    OpenMS::FileHandler().storeConsensusFeatures(path.toStdString(), map);

    const auto result = OpenMSViewer::ConsensusDocument::read(path);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.columns.size(), std::size_t{2});
    QCOMPARE(result.columns.front().label, QStringLiteral("light"));
    QCOMPARE(result.experimentType, QStringLiteral("labeled_MS1"));
    QCOMPARE(result.features.size(), std::size_t{2});

    int covered2 = 0;
    int covered1 = 0;
    const OpenMSViewer::ConsensusFeatureRecord* paired2 = nullptr;
    for (const auto& record : result.features)
    {
      QCOMPARE(record.totalMaps, 2);
      if (record.coveredMaps == 2) { ++covered2; paired2 = &record; }
      if (record.coveredMaps == 1) ++covered1;
    }
    QCOMPARE(covered2, 1);
    QCOMPARE(covered1, 1);
    QVERIFY(paired2 != nullptr);
    QCOMPARE(paired2->missingMaps(), 0);
    QVERIFY(qFuzzyCompare(paired2->sumIntensity, 3000.0));
    QVERIFY(qFuzzyCompare(paired2->meanIntensity, 1500.0));

    const auto handles = OpenMSViewer::ConsensusDocument::handlesFor(*result.map, paired2->index);
    QCOMPARE(handles.size(), std::size_t{2});

    QVERIFY(!OpenMSViewer::ConsensusDocument::read(dir.filePath(QStringLiteral("nope.consensusXML")))
              .succeeded());
  }
};

int runConsensusDocumentTests(int argc, char** argv)
{
  ConsensusDocumentTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "ConsensusDocumentTest.moc"
