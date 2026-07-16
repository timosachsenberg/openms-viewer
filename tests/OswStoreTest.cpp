#include "model/OswStore.h"

#include "model/SqliteWriteDb.h"

#include <QTemporaryDir>
#include <QTest>

// Builds a minimal OpenSWATH .osw with the test-only SqliteWriteDb helper, then
// reads it back through the read-only OswStore.
class OswStoreTest final : public QObject
{
  Q_OBJECT

private:
  static void buildOsw(const QString& path)
  {
    OpenMSViewer::SqliteWriteDb conn(path.toStdString());  // READWRITE_OR_CREATE
    const char* schema[] = {
      "CREATE TABLE RUN (ID INT, FILENAME TEXT);",
      "INSERT INTO RUN VALUES (1,'run1.mzML');",
      "CREATE TABLE PEPTIDE (ID INT, UNMODIFIED_SEQUENCE TEXT, MODIFIED_SEQUENCE TEXT, DECOY INT);",
      "INSERT INTO PEPTIDE VALUES (10,'PEPTIDE','PEPT(Phospho)IDE',0);",
      "CREATE TABLE PRECURSOR (ID INT, PRECURSOR_MZ REAL, CHARGE INT, LIBRARY_RT REAL, DECOY INT);",
      "INSERT INTO PRECURSOR VALUES (100,500.25,2,1200.0,0);",
      "INSERT INTO PRECURSOR VALUES (101,650.0,3,900.0,1);",  // decoy, no features
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
      "INSERT INTO FEATURE VALUES (5000,1,100,1205.0,0,1200.0,5.0,1195.0,1215.0);",
      "CREATE TABLE SCORE_MS2 (FEATURE_ID INTEGER, SCORE REAL, RANK INTEGER, PVALUE REAL, QVALUE REAL, PEP REAL);",
      "INSERT INTO SCORE_MS2 VALUES (5000,3.5,1,0.001,0.01,0.02);",
      "CREATE TABLE FEATURE_MS2 (FEATURE_ID INT, AREA_INTENSITY REAL, APEX_INTENSITY REAL,"
        " VAR_XCORR_SHAPE REAL, VAR_LOG_SN_SCORE REAL);",
      "INSERT INTO FEATURE_MS2 VALUES (5000,12345.0,999.0,0.95,4.2);",
    };
    for (const char* statement : schema) conn.executeStatement(statement);
  }

private slots:
  void readsHierarchyScoresAndTransitions()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("test.osw"));
    buildOsw(path);

    QString error;
    const auto store = OpenMSViewer::OswStore::open(path, error);
    QVERIFY2(store != nullptr, qPrintable(error));
    QCOMPARE(store->runs().size(), std::size_t{1});
    QCOMPARE(store->runs().front().filename, QStringLiteral("run1.mzML"));
    QVERIFY(store->hasScores());

    const auto precursors = store->precursors();
    QCOMPARE(precursors.size(), std::size_t{2});
    // Scored precursor sorts first (best q-value).
    QCOMPARE(precursors.front().modifiedSequence, QStringLiteral("PEPT(Phospho)IDE"));
    QCOMPARE(precursors.front().charge, 2);
    QCOMPARE(precursors.front().peakGroupCount, 1);
    QVERIFY(precursors.front().bestQValue.has_value());
    QVERIFY(qFuzzyCompare(*precursors.front().bestQValue, 0.01));
    QVERIFY(precursors.back().decoy);
    QCOMPARE(precursors.back().peakGroupCount, 0);

    const auto groups = store->peakGroups(100);
    QCOMPARE(groups.size(), std::size_t{1});
    QCOMPARE(groups.front().featureId, std::int64_t{5000});
    QVERIFY(qFuzzyCompare(groups.front().apexRt, 1205.0));
    QVERIFY(qFuzzyCompare(groups.front().leftWidth, 1195.0));
    QVERIFY(qFuzzyCompare(groups.front().rightWidth, 1215.0));
    QVERIFY(groups.front().qValue.has_value());
    QVERIFY(groups.front().areaIntensity.has_value());
    QVERIFY(qFuzzyCompare(*groups.front().areaIntensity, 12345.0));

    const auto transitions = store->transitions(100);
    QCOMPARE(transitions.size(), std::size_t{2});
    QCOMPARE(transitions.front().annotation, QStringLiteral("b4"));  // ordered by ORDINAL (4 before 5)
    QCOMPARE(transitions.back().annotation, QStringLiteral("y5"));

    const auto subScores = store->subScores(5000);
    QCOMPARE(subScores.size(), std::size_t{2});  // xcorr shape + log sn score

    // A missing database fails gracefully (nullptr + message), never a crash.
    QString missingError;
    QVERIFY(OpenMSViewer::OswStore::open(dir.filePath(QStringLiteral("nope.osw")), missingError) == nullptr);
    QVERIFY(!missingError.isEmpty());
  }

  void rejectsNonOswDatabase()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plain.sqlite"));
    {
      OpenMSViewer::SqliteWriteDb conn(path.toStdString());
      conn.executeStatement("CREATE TABLE UNRELATED (ID INT);");
    }
    QString error;
    QVERIFY(OpenMSViewer::OswStore::open(path, error) == nullptr);
    QVERIFY(error.contains(QStringLiteral("OpenSWATH")));
  }
};

int runOswStoreTests(int argc, char** argv)
{
  OswStoreTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "OswStoreTest.moc"
