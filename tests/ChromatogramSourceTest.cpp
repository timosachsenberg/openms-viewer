#include "model/ChromatogramSource.h"
#include "model/OswDocument.h"
#include "model/OswStore.h"

#include "model/SqliteWriteDb.h"
#include <OpenMS/FORMAT/SqMassFile.h>
#include <OpenMS/KERNEL/MSChromatogram.h>
#include <OpenMS/KERNEL/MSExperiment.h>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

// The .xic reader itself is exercised end-to-end against a real fixture in the
// preview harness; here we lock in graceful failure for the bad-input paths.
class ChromatogramSourceTest final : public QObject
{
  Q_OBJECT

private:
  // A minimal OpenSWATH .osw: precursor 100 with two library transitions (y5=1000,
  // b4=1001) — the mapping + annotation that the sqMass file itself lacks.
  static void buildOsw(const QString& path)
  {
    OpenMSViewer::SqliteWriteDb conn(path.toStdString());
    const char* schema[] = {
      "CREATE TABLE RUN (ID INT, FILENAME TEXT);",
      "INSERT INTO RUN VALUES (7,'run7.mzML');",
      "CREATE TABLE PEPTIDE (ID INT, UNMODIFIED_SEQUENCE TEXT, MODIFIED_SEQUENCE TEXT, DECOY INT);",
      "INSERT INTO PEPTIDE VALUES (10,'PEPTIDEK','PEPTIDEK',0);",
      "CREATE TABLE PRECURSOR (ID INT, PRECURSOR_MZ REAL, CHARGE INT, LIBRARY_RT REAL, DECOY INT);",
      "INSERT INTO PRECURSOR VALUES (100,500.25,2,1200.0,0);",
      "CREATE TABLE PRECURSOR_PEPTIDE_MAPPING (PRECURSOR_ID INT, PEPTIDE_ID INT);",
      "INSERT INTO PRECURSOR_PEPTIDE_MAPPING VALUES (100,10);",
      "CREATE TABLE TRANSITION (ID INT, PRODUCT_MZ REAL, CHARGE INT, TYPE TEXT, ANNOTATION TEXT,"
        " ORDINAL INT, DETECTING INT, IDENTIFYING INT, QUANTIFYING INT, LIBRARY_INTENSITY REAL, DECOY INT);",
      "INSERT INTO TRANSITION VALUES (1000,600.3,1,'y','y5',5,1,0,1,1000.0,0);",
      "INSERT INTO TRANSITION VALUES (1001,700.4,1,'b','b4',4,1,0,1,800.0,0);",
      "CREATE TABLE TRANSITION_PRECURSOR_MAPPING (TRANSITION_ID INT, PRECURSOR_ID INT);",
      "INSERT INTO TRANSITION_PRECURSOR_MAPPING VALUES (1000,100);",
      "INSERT INTO TRANSITION_PRECURSOR_MAPPING VALUES (1001,100);",
      "CREATE TABLE FEATURE (ID INT, RUN_ID INT, PRECURSOR_ID INT, EXP_RT REAL, EXP_IM REAL,"
        " NORM_RT REAL, DELTA_RT REAL, LEFT_WIDTH REAL, RIGHT_WIDTH REAL);",
      "INSERT INTO FEATURE VALUES (5000,7,100,1205.0,0,1200.0,5.0,1195.0,1215.0);",
    };
    for (const char* statement : schema) conn.executeStatement(statement);
  }

  // A sqMass whose CHROMATOGRAM.NATIVE_ID values are the transition ids (and an MS1
  // "<precursor>_Precursor_i0"), each a small Gaussian-ish trace.
  static void buildSqMass(const QString& path)
  {
    OpenMS::MSExperiment experiment;
    const auto add = [&](const std::string& nativeId, float scale)
    {
      OpenMS::MSChromatogram chromatogram;
      chromatogram.setNativeID(nativeId);
      for (int i = 0; i < 20; ++i)
      {
        OpenMS::ChromatogramPeak peak;
        peak.setRT(1190.0 + i);
        peak.setIntensity(scale * static_cast<float>(20 - std::abs(i - 10)));
        chromatogram.push_back(peak);
      }
      experiment.addChromatogram(chromatogram);
    };
    add("1000", 50.0F);   // y5
    add("1001", 40.0F);   // b4
    add("100_Precursor_i0", 80.0F);  // MS1 precursor trace
    add("9999", 10.0F);   // an unrelated chromatogram that must NOT be fetched
    OpenMS::SqMassFile().store(path.toStdString(), experiment);
  }

private slots:
  void reportsMissingAndInvalidInputs()
  {
    QString error;
    QVERIFY(OpenMSViewer::XicChromatogramSource::open({}, error) == nullptr);
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(OpenMSViewer::XicChromatogramSource::open(
              {QStringLiteral("/definitely/not/a/real/file.xic")}, error) == nullptr);
    QVERIFY(error.contains(QStringLiteral("not found")));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bogus = dir.filePath(QStringLiteral("bogus.xic"));
    QFile file(bogus);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not a parquet file");
    file.close();
    error.clear();
    QVERIFY(OpenMSViewer::XicChromatogramSource::open({bogus}, error) == nullptr);
    QVERIFY(!error.isEmpty());
  }

  void sqMassLazilyDecodesSelectedTransitions()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString oswPath = dir.filePath(QStringLiteral("run7.osw"));
    const QString sqMassPath = dir.filePath(QStringLiteral("run7.sqMass"));
    buildOsw(oswPath);
    buildSqMass(sqMassPath);

    QString error;
    const auto store = OpenMSViewer::OswStore::open(oswPath, error);
    QVERIFY2(store != nullptr, qPrintable(error));

    const auto source = OpenMSViewer::SqMassChromatogramSource::open(sqMassPath, store, error);
    QVERIFY2(source != nullptr, qPrintable(error));
    QVERIFY(source->provenance().contains(QStringLiteral("sqMass")));

    const auto traces = source->fetch(/*precursorId*/ 100, /*runId*/ -1);
    // 2 fragment transitions + the MS1 precursor trace; the unrelated "9999"
    // chromatogram is never touched.
    QCOMPARE(traces.size(), std::size_t{3});

    const auto findTransition = [&](std::int64_t id)
    {
      return std::find_if(traces.begin(), traces.end(),
                          [id](const auto& trace) { return trace.transitionId == id; });
    };
    const auto y5 = findTransition(1000);
    QVERIFY(y5 != traces.end());
    QCOMPARE(y5->ionType, QStringLiteral("y"));
    QCOMPARE(y5->annotation, QStringLiteral("y5"));  // library metadata from the .osw
    QCOMPARE(y5->ordinal, 5);
    QCOMPARE(y5->msLevel, 2);
    QCOMPARE(y5->rt.size(), std::size_t{20});        // RT/intensity decoded from sqMass
    QCOMPARE(y5->intensity.size(), std::size_t{20});
    QVERIFY(*std::max_element(y5->intensity.begin(), y5->intensity.end()) > 0.0);

    // The MS1 precursor trace resolved and is level 1.
    const auto ms1 = std::find_if(traces.begin(), traces.end(),
                                  [](const auto& trace) { return trace.isMs1(); });
    QVERIFY(ms1 != traces.end());
    QVERIFY(!ms1->rt.empty());

    // Run filter: the sqMass reports its own run id; fetching that run serves, a
    // different run does not (correct disambiguation for a multi-run OSW).
    QCOMPARE(source->runs().size(), std::size_t{1});
    const std::int64_t sqMassRun = source->runs().front().runId;
    QCOMPARE(source->fetch(100, sqMassRun).size(), std::size_t{3});
    QVERIFY(source->fetch(100, sqMassRun + 4242).empty());
  }

  // A duplicated TRANSITION_PRECURSOR_MAPPING row (merged/IPF libraries) must not
  // make the batch decode throw and blank the whole precursor.
  void sqMassDeduplicatesRepeatedTransitionMapping()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString oswPath = dir.filePath(QStringLiteral("dup.osw"));
    const QString sqMassPath = dir.filePath(QStringLiteral("dup.sqMass"));
    buildOsw(oswPath);
    {
      OpenMSViewer::SqliteWriteDb conn(oswPath.toStdString());
      conn.executeStatement("INSERT INTO TRANSITION_PRECURSOR_MAPPING VALUES (1000,100);");
    }
    buildSqMass(sqMassPath);

    QString error;
    const auto store = OpenMSViewer::OswStore::open(oswPath, error);
    QVERIFY2(store != nullptr, qPrintable(error));
    const auto source = OpenMSViewer::SqMassChromatogramSource::open(sqMassPath, store, error);
    QVERIFY2(source != nullptr, qPrintable(error));

    const auto traces = source->fetch(100, -1);
    QCOMPARE(traces.size(), std::size_t{3});  // deduped, not doubled, not blanked
    const auto withData = std::count_if(traces.begin(), traces.end(),
                                        [](const auto& trace) { return !trace.rt.empty(); });
    QCOMPARE(withData, static_cast<decltype(withData)>(3));
  }

  // Loading an .osw with a sibling .sqMass (and no .xic) wires the sqMass source.
  void oswDocumentFallsBackToSiblingSqMass()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    buildOsw(dir.filePath(QStringLiteral("run7.osw")));
    buildSqMass(dir.filePath(QStringLiteral("run7.sqMass")));

    const auto result = OpenMSViewer::OswDocument::read(dir.filePath(QStringLiteral("run7.osw")));
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QVERIFY(result.chromatograms != nullptr);
    QVERIFY(result.chromatograms->provenance().contains(QStringLiteral("sqMass")));
    QCOMPARE(result.chromatograms->fetch(100, -1).size(), std::size_t{3});
  }

  void sqMassReportsInvalidInputs()
  {
    QString error;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString oswPath = dir.filePath(QStringLiteral("run7.osw"));
    buildOsw(oswPath);
    const auto store = OpenMSViewer::OswStore::open(oswPath, error);
    QVERIFY(store != nullptr);

    // Null store.
    QVERIFY(OpenMSViewer::SqMassChromatogramSource::open(
              dir.filePath(QStringLiteral("x.sqMass")), nullptr, error) == nullptr);
    // Missing file.
    error.clear();
    QVERIFY(OpenMSViewer::SqMassChromatogramSource::open(
              QStringLiteral("/definitely/not/real.sqMass"), store, error) == nullptr);
    QVERIFY(error.contains(QStringLiteral("not found")));
  }
};

int runChromatogramSourceTests(int argc, char** argv)
{
  ChromatogramSourceTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "ChromatogramSourceTest.moc"
