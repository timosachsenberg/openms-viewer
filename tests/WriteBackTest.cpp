#include "TestData.h"

#include "model/ConsensusDocument.h"
#include "model/ViewerDocument.h"

#include <OpenMS/FORMAT/ConsensusXMLFile.h>
#include <OpenMS/FORMAT/FeatureXMLFile.h>
#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/KERNEL/ConsensusFeature.h>
#include <OpenMS/KERNEL/ConsensusMap.h>
#include <OpenMS/KERNEL/FeatureMap.h>

#include <QTemporaryDir>
#include <QTest>

// Native write-back: load → save → read back and verify the data round-trips.
class WriteBackTest final : public QObject
{
  Q_OBJECT

private slots:
  void savesFeaturesRoundTrip()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString in = dir.filePath(QStringLiteral("in.featureXML"));
    OpenMS::FeatureXMLFile().store(in.toStdString(), OpenMSViewer::TestData::featureMap());

    OpenMSViewer::ViewerDocument document;
    QVERIFY(document.adoptFeatures(OpenMSViewer::ViewerDocument::readFeatureXML(in)));
    QVERIFY(document.hasFeatures());

    const QString out = dir.filePath(QStringLiteral("out.featureXML"));
    QString error;
    QVERIFY2(document.saveFeatures(out, error), qPrintable(error));

    OpenMS::FeatureMap reloaded;
    OpenMS::FeatureXMLFile().load(out.toStdString(), reloaded);
    QCOMPARE(reloaded.size(), OpenMSViewer::TestData::featureMap().size());
  }

  void savesIdentificationsRoundTrip()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString in = dir.filePath(QStringLiteral("in.idXML"));
    const auto [proteins, peptides] = OpenMSViewer::TestData::identifications();
    OpenMS::IdXMLFile().store(in.toStdString(), proteins, peptides);

    OpenMSViewer::ViewerDocument document;
    QVERIFY(document.adoptIdentifications(OpenMSViewer::ViewerDocument::readIdXML(in)));
    QVERIFY(document.hasIdentifications());

    const QString out = dir.filePath(QStringLiteral("out.idXML"));
    QString error;
    QVERIFY2(document.saveIdentifications(out, error), qPrintable(error));

    std::vector<OpenMS::ProteinIdentification> reloadedProteins;
    OpenMS::PeptideIdentificationList reloadedPeptides;
    OpenMS::IdXMLFile().load(out.toStdString(), reloadedProteins, reloadedPeptides);
    QCOMPARE(reloadedPeptides.size(), peptides.size());
  }

  void savesConsensusRoundTrip()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    OpenMS::ConsensusMap map;
    OpenMS::ConsensusMap::ColumnHeader header;
    header.label = "s0";
    header.size = 1;
    map.getColumnHeaders()[0] = header;
    OpenMS::ConsensusFeature feature;
    feature.setRT(1000.0);
    feature.setMZ(500.0);
    feature.setIntensity(1234.0);
    feature.ensureUniqueId();
    map.push_back(feature);

    const QString out = dir.filePath(QStringLiteral("out.consensusXML"));
    QString error;
    QVERIFY2(OpenMSViewer::ConsensusDocument::save(map, out, error), qPrintable(error));

    OpenMS::ConsensusMap reloaded;
    OpenMS::ConsensusXMLFile().load(out.toStdString(), reloaded);
    QCOMPARE(reloaded.size(), std::size_t{1});
  }

  void reportsNothingToSave()
  {
    OpenMSViewer::ViewerDocument document;
    QString error;
    QVERIFY(!document.saveFeatures(QStringLiteral("/tmp/none.featureXML"), error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!document.saveIdentifications(QStringLiteral("/tmp/none.idXML"), error));
    QVERIFY(!error.isEmpty());
  }
};

int runWriteBackTests(int argc, char** argv)
{
  WriteBackTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "WriteBackTest.moc"
