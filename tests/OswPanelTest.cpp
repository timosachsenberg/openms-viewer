#include "model/OswStore.h"
#include "widgets/OswPanel.h"

#include "model/SqliteWriteDb.h"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

// Drives the OSW panel with a minimal synthetic .osw and checks the tables
// populate and filter (no .xic → the plot shows its placeholder).
class OswPanelTest final : public QObject
{
  Q_OBJECT

private:
  static void buildOsw(const QString& path)
  {
    OpenMSViewer::SqliteWriteDb conn(path.toStdString());
    const char* schema[] = {
      "CREATE TABLE RUN (ID INT, FILENAME TEXT);",
      "INSERT INTO RUN VALUES (1,'run.mzML');",
      "CREATE TABLE PEPTIDE (ID INT, UNMODIFIED_SEQUENCE TEXT, MODIFIED_SEQUENCE TEXT, DECOY INT);",
      "INSERT INTO PEPTIDE VALUES (10,'PEPTIDEK','PEPTIDEK',0);",
      "INSERT INTO PEPTIDE VALUES (11,'DECOYSEQK','DECOYSEQK',1);",
      "CREATE TABLE PRECURSOR (ID INT, PRECURSOR_MZ REAL, CHARGE INT, LIBRARY_RT REAL, DECOY INT);",
      "INSERT INTO PRECURSOR VALUES (100,500.25,2,1200.0,0);",
      "INSERT INTO PRECURSOR VALUES (101,650.0,3,900.0,1);",
      "CREATE TABLE PRECURSOR_PEPTIDE_MAPPING (PRECURSOR_ID INT, PEPTIDE_ID INT);",
      "INSERT INTO PRECURSOR_PEPTIDE_MAPPING VALUES (100,10);",
      "INSERT INTO PRECURSOR_PEPTIDE_MAPPING VALUES (101,11);",
      "CREATE TABLE FEATURE (ID INT, RUN_ID INT, PRECURSOR_ID INT, EXP_RT REAL, EXP_IM REAL,"
        " NORM_RT REAL, DELTA_RT REAL, LEFT_WIDTH REAL, RIGHT_WIDTH REAL);",
      "INSERT INTO FEATURE VALUES (5000,1,100,1205.0,0,1200.0,5.0,1195.0,1215.0);",
      "INSERT INTO FEATURE VALUES (5001,1,100,1180.0,0,1200.0,-20.0,1170.0,1190.0);",
      "CREATE TABLE SCORE_MS2 (FEATURE_ID INTEGER, SCORE REAL, RANK INTEGER, PVALUE REAL, QVALUE REAL, PEP REAL);",
      "INSERT INTO SCORE_MS2 VALUES (5000,3.5,1,0.001,0.01,0.02);",
      "INSERT INTO SCORE_MS2 VALUES (5001,1.2,2,0.02,0.08,0.20);",
    };
    for (const char* statement : schema) conn.executeStatement(statement);
  }

private slots:
  void tablesPopulateAndFilter()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("panel.osw"));
    buildOsw(path);
    QString error;
    auto store = OpenMSViewer::OswStore::open(path, error);
    QVERIFY2(store != nullptr, qPrintable(error));

    OpenMSViewer::OswPanel panel;
    panel.resize(820, 520);
    panel.setData(store, nullptr, QStringLiteral("no chromatograms"));

    auto* precursorTable = panel.findChild<QTableView*>(QStringLiteral("oswPrecursorTable"));
    auto* peakGroupTable = panel.findChild<QTableView*>(QStringLiteral("oswPeakGroupTable"));
    QVERIFY(precursorTable && peakGroupTable);
    QCOMPARE(precursorTable->model()->rowCount(), 2);
    // The first precursor is auto-selected → its two ranked peak groups show.
    QCOMPARE(peakGroupTable->model()->rowCount(), 2);

    // Hiding decoys drops the decoy precursor.
    auto* hideDecoys = panel.findChild<QCheckBox*>(QStringLiteral("oswHideDecoys"));
    QVERIFY(hideDecoys != nullptr);
    hideDecoys->setChecked(true);
    QCOMPARE(precursorTable->model()->rowCount(), 1);

    // Renders without a chromatogram source (plot shows its placeholder).
    QVERIFY(!panel.grab().isNull());
  }
};

int runOswPanelTests(int argc, char** argv)
{
  OswPanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "OswPanelTest.moc"
