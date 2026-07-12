#include "model/ConsensusDrilldown.h"
#include "model/ViewerDocument.h"

#include <OpenMS/KERNEL/ConsensusFeature.h>
#include <OpenMS/KERNEL/ConsensusMap.h>
#include <OpenMS/KERNEL/FeatureHandle.h>
#include <OpenMS/KERNEL/MSExperiment.h>
#include <OpenMS/METADATA/PeptideIdentification.h>

#include <QTest>

#include <memory>

class ConsensusDrilldownTest final : public QObject
{
  Q_OBJECT

private:
  // A three-scan run with explicit native IDs, adopted so both the native-ID map
  // (spectra records) and the nearest-by-RT search (experiment) are populated.
  static void buildRun(OpenMSViewer::ViewerDocument& document)
  {
    auto experiment = std::make_shared<OpenMS::MSExperiment>();
    const auto add = [&](double rt, unsigned int level, const std::string& nativeId)
    {
      OpenMS::MSSpectrum spectrum;
      spectrum.setRT(rt);
      spectrum.setMSLevel(level);
      spectrum.setNativeID(nativeId);
      experiment->addSpectrum(spectrum);
    };
    add(10.0, 1, "scan=1");
    add(11.0, 2, "scan=2");
    add(20.0, 1, "scan=3");

    OpenMSViewer::ViewerDocument::LoadResult result;
    result.experiment = experiment;
    result.sourcePath = QStringLiteral("/data/run7.mzML");
    result.bounds = {10.0, 20.0, 100.0, 600.0};
    for (std::size_t i = 0; i < experiment->size(); ++i)
    {
      OpenMSViewer::SpectrumRecord record;
      record.index = i;
      record.rt = (*experiment)[i].getRT();
      record.msLevel = (*experiment)[i].getMSLevel();
      record.nativeId = QString::fromStdString((*experiment)[i].getNativeID());
      result.spectra.push_back(record);
    }
    document.adopt(std::move(result));
  }

  static std::shared_ptr<OpenMS::ConsensusMap> buildConsensus(const std::string& runFilename,
                                                              bool withSpectrumReference)
  {
    auto map = std::make_shared<OpenMS::ConsensusMap>();
    OpenMS::ConsensusMap::ColumnHeader header;
    header.filename = runFilename;   // input map 0 == the loaded run (matched by base name)
    header.label = "run7";
    header.size = 1;
    map->getColumnHeaders()[0] = header;

    OpenMS::ConsensusFeature feature;
    feature.setRT(11.0);
    feature.setMZ(500.2);
    feature.setCharge(2);
    OpenMS::Peak2D point;
    point.setRT(11.0);
    point.setMZ(500.2);
    feature.insert(OpenMS::FeatureHandle(0, point, 0));

    OpenMS::PeptideIdentification pid;
    pid.setRT(11.0);
    pid.setMZ(500.2);
    pid.setMetaValue("map_index", 0);
    if (withSpectrumReference) pid.setSpectrumReference("scan=2");
    OpenMS::PeptideHit hit;
    hit.setSequence(OpenMS::AASequence::fromString("PEPTIDE"));
    pid.insertHit(hit);
    feature.getPeptideIdentifications().push_back(pid);
    map->push_back(feature);
    return map;
  }

  static std::vector<OpenMSViewer::ConsensusColumn> columnsFor(const OpenMS::ConsensusMap& map)
  {
    std::vector<OpenMSViewer::ConsensusColumn> columns;
    for (const auto& [mapIndex, header] : map.getColumnHeaders())
      columns.push_back({static_cast<std::int64_t>(mapIndex),
                         QString::fromStdString(header.filename), QString::fromStdString(header.label)});
    return columns;
  }

private slots:
  // An exact scan reference on the matching map resolves the source scan by native ID.
  void exactSpectrumReferenceResolvesNativeId()
  {
    OpenMSViewer::ViewerDocument document;
    buildRun(document);
    // The run is listed under a different extension — matching is by base name.
    auto map = buildConsensus("/elsewhere/run7.featureXML", true);

    const auto target = OpenMSViewer::ConsensusDrilldown::resolve(
      *map, 0, columnsFor(*map), document);
    QVERIFY(target.runIsInputMap);
    QCOMPARE(target.mapIndex.value(), std::int64_t{0});
    QVERIFY(target.spectrumIndex.has_value());
    QCOMPARE(target.spectrumIndex.value(), std::size_t{1});  // "scan=2" is index 1
    QVERIFY(target.exact);
  }

  // With no scan reference, the map's handle RT drives a nearest-scan fallback that
  // prefers the MS1 survey scan (a quant feature is measured from MS1), so despite an
  // MS2 scan sitting exactly at the handle RT it lands on the nearer MS1 scan.
  void missingReferenceFallsBackToNearestMs1ByRt()
  {
    OpenMSViewer::ViewerDocument document;
    buildRun(document);  // MS1@10 (idx0), MS2@11 (idx1), MS1@20 (idx2)
    auto map = buildConsensus("/data/run7.mzML", false);

    const auto target = OpenMSViewer::ConsensusDrilldown::resolve(
      *map, 0, columnsFor(*map), document);
    QVERIFY(target.runIsInputMap);
    QVERIFY(target.spectrumIndex.has_value());
    QCOMPARE(target.spectrumIndex.value(), std::size_t{0});  // nearest MS1 to RT 11.0
    QVERIFY(!target.exact);
  }

  // A run that is not one of the consensus columns yields no navigation target.
  void unrelatedRunIsNotAnInputMap()
  {
    OpenMSViewer::ViewerDocument document;
    buildRun(document);
    auto map = buildConsensus("/data/other_sample.mzML", true);

    const auto target = OpenMSViewer::ConsensusDrilldown::resolve(
      *map, 0, columnsFor(*map), document);
    QVERIFY(!target.runIsInputMap);
    QVERIFY(!target.spectrumIndex.has_value());
  }

  // Two input maps share the loaded run's base name (batchA/run7 vs batchB/run7):
  // without a full-path match, drill-down must refuse to guess.
  void ambiguousBaseNameRefusesToGuess()
  {
    OpenMSViewer::ViewerDocument document;
    buildRun(document);  // run sourcePath is /data/run7.mzML
    auto map = buildConsensus("/batchA/run7.featureXML", true);
    // Add a second column with the same base name but a different path.
    OpenMS::ConsensusMap::ColumnHeader other;
    other.filename = "/batchB/run7.featureXML";
    other.label = "run7b";
    other.size = 1;
    map->getColumnHeaders()[1] = other;

    const auto target = OpenMSViewer::ConsensusDrilldown::resolve(
      *map, 0, columnsFor(*map), document);
    QVERIFY(target.runIsInputMap);
    QVERIFY(target.ambiguousRun);
    QVERIFY(!target.mapIndex.has_value());
    QVERIFY(!target.spectrumIndex.has_value());
  }

  // An exact absolute-path column match wins even when another column shares the
  // base name, so a genuinely-present run still resolves despite the ambiguity.
  void exactPathMatchBeatsAmbiguousBaseName()
  {
    OpenMSViewer::ViewerDocument document;
    buildRun(document);  // run sourcePath is /data/run7.mzML
    auto map = buildConsensus("/data/run7.mzML", true);  // column 0 = exact path
    OpenMS::ConsensusMap::ColumnHeader other;
    other.filename = "/batchB/run7.featureXML";  // shares base name only
    other.label = "run7b";
    other.size = 1;
    map->getColumnHeaders()[1] = other;

    const auto target = OpenMSViewer::ConsensusDrilldown::resolve(
      *map, 0, columnsFor(*map), document);
    QVERIFY(target.runIsInputMap);
    QVERIFY(!target.ambiguousRun);
    QCOMPARE(target.mapIndex.value(), std::int64_t{0});
    QVERIFY(target.exact);
    QCOMPARE(target.spectrumIndex.value(), std::size_t{1});
  }
};

int runConsensusDrilldownTests(int argc, char** argv)
{
  ConsensusDrilldownTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "ConsensusDrilldownTest.moc"
