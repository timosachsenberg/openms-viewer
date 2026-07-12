#include "widgets/MetadataBrowserWidget.h"

#include <OpenMS/KERNEL/MSExperiment.h>
#include <OpenMS/METADATA/Instrument.h>
#include <OpenMS/METADATA/IonSource.h>
#include <OpenMS/METADATA/MassAnalyzer.h>
#include <OpenMS/METADATA/Sample.h>

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTest>

#include <functional>
#include <memory>

class MetadataBrowserTest final : public QObject
{
  Q_OBJECT

private:
  static QStringList flatten(const QTreeWidget* tree)
  {
    QStringList out;
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* item)
    {
      out << item->text(0) << item->text(1);
      for (int i = 0; i < item->childCount(); ++i) walk(item->child(i));
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) walk(tree->topLevelItem(i));
    return out;
  }

  static bool containsSubstring(const QStringList& list, const QString& needle)
  {
    for (const QString& value : list)
      if (value.contains(needle)) return true;
    return false;
  }

private slots:
  void populatesExperimentAndScanMetadata()
  {
    auto experiment = std::make_shared<OpenMS::MSExperiment>();
    OpenMS::Instrument instrument;
    instrument.setName("Q Exactive");
    instrument.setVendor("Thermo");
    OpenMS::IonSource source;
    source.setIonizationMethod(OpenMS::IonSource::IonizationMethod::ESI);
    instrument.setIonSources({source});
    OpenMS::MassAnalyzer analyzer;
    analyzer.setType(OpenMS::MassAnalyzer::AnalyzerType::ORBITRAP);
    instrument.setMassAnalyzers({analyzer});
    experiment->setInstrument(instrument);
    OpenMS::Sample sample;
    sample.setName("HeLa");
    sample.setOrganism("Homo sapiens");
    experiment->setSample(sample);

    OpenMS::MSSpectrum spectrum;
    spectrum.setRT(1200.0);
    spectrum.setMSLevel(2);
    spectrum.setNativeID("scan=5");
    OpenMS::Precursor precursor;
    precursor.setMZ(500.25);
    precursor.setCharge(2);
    precursor.setIsolationWindowLowerOffset(1.0);
    precursor.setIsolationWindowUpperOffset(1.0);
    spectrum.setPrecursors({precursor});
    experiment->addSpectrum(spectrum);

    OpenMSViewer::MetadataBrowserWidget browser;
    browser.setExperiment(experiment);
    browser.setSpectrumIndex(0);

    auto* tree = browser.findChild<QTreeWidget*>(QStringLiteral("metadataTree"));
    QVERIFY(tree != nullptr);
    const QStringList all = flatten(tree);
    QVERIFY(containsSubstring(all, QStringLiteral("Q Exactive")));   // instrument name
    QVERIFY(containsSubstring(all, QStringLiteral("Thermo")));       // vendor
    QVERIFY(containsSubstring(all, QStringLiteral("HeLa")));         // sample
    QVERIFY(containsSubstring(all, QStringLiteral("Homo sapiens"))); // organism
    QVERIFY(containsSubstring(all, QStringLiteral("scan=5")));       // per-scan native id
    QVERIFY(containsSubstring(all, QStringLiteral("500.2500")));     // precursor m/z
    QVERIFY(containsSubstring(all, QStringLiteral("Scan #1")));      // scan node header

    browser.clear();
    QVERIFY(containsSubstring(flatten(tree), QStringLiteral("No data")));
  }
};

int runMetadataBrowserTests(int argc, char** argv)
{
  MetadataBrowserTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "MetadataBrowserTest.moc"
