#include "TestData.h"

#include "annotation/SpectrumAnnotation.h"

#include <OpenMS/CHEMISTRY/AASequence.h>
#include <OpenMS/CHEMISTRY/TheoreticalSpectrumGenerator.h>

#include <QTest>

class SpectrumAnnotationTest final : public QObject
{
  Q_OBJECT

private slots:
  void classifiesStandardAndCrossLinkedNames()
  {
    QCOMPARE(OpenMSViewer::classifyIon(QStringLiteral("b3++")), OpenMSViewer::IonType::B);
    QCOMPARE(OpenMSViewer::classifyIon(QStringLiteral("y7-H2O+")), OpenMSViewer::IonType::Y);
    QCOMPARE(OpenMSViewer::classifyIon(QStringLiteral("[alpha|ci$b2]")), OpenMSViewer::IonType::B);
    QCOMPARE(OpenMSViewer::classifyIon(QStringLiteral("precursor++")), OpenMSViewer::IonType::Precursor);
  }

  void formatsIonOrdinalsChargesAndNeutralLosses()
  {
    QCOMPARE(OpenMSViewer::formatIonLabel(QStringLiteral("b3+")), QStringLiteral("b₃⁺"));
    QCOMPARE(OpenMSViewer::formatIonLabel(QStringLiteral("y15++")), QStringLiteral("y₁₅²⁺"));
    QCOMPARE(OpenMSViewer::formatIonLabel(QStringLiteral("y7-H2O+2")), QStringLiteral("y₇-H₂O²⁺"));
    QCOMPARE(OpenMSViewer::formatIonLabel(QStringLiteral("[alpha|ci$y3]")), QStringLiteral("[alpha|ci$y3]"));
    // Bracketed precursor/adduct labels keep their stoichiometry verbatim and
    // only superscript the trailing charge.
    QCOMPARE(OpenMSViewer::formatIonLabel(QStringLiteral("[M+2H]2+")), QStringLiteral("[M+2H]²⁺"));
    QCOMPARE(OpenMSViewer::formatIonLabel(QStringLiteral("[M+H]+")), QStringLiteral("[M+H]⁺"));
    QCOMPARE(OpenMSViewer::formatIonLabel(QStringLiteral("[M-H]-")), QStringLiteral("[M-H]⁻"));
    QCOMPARE(OpenMSViewer::formatIonLabel(QStringLiteral("[M+H")), QStringLiteral("[M+H"));
    // Charge magnitude is parsed order-independently: "+2", "2+" and "++" all mean 2+.
    QCOMPARE(OpenMSViewer::formatIonLabel(QStringLiteral("[M+H]+2")), QStringLiteral("[M+H]²⁺"));
    QCOMPARE(OpenMSViewer::formatIonLabel(QStringLiteral("[M+H]++")), QStringLiteral("[M+H]²⁺"));
  }

  void prioritizesExternalPeakAnnotations()
  {
    const auto experiment = OpenMSViewer::TestData::experiment();
    const auto [proteins, ids] = OpenMSViewer::TestData::identifications();
    Q_UNUSED(proteins);
    OpenMSViewer::PeptideHitRecord hit;
    hit.sequence = QStringLiteral("PEPTIDE");
    hit.charge = 2;
    hit.peakAnnotations.push_back({450.0, 1.0, 1, QStringLiteral("[alpha|ci$y4]")});

    const auto annotation = OpenMSViewer::computeSpectrumAnnotation(experiment[1], hit, 0.05);
    QVERIFY(annotation.error.isEmpty());
    QCOMPARE(annotation.matched.size(), std::size_t{1});
    QVERIFY(annotation.matched.front().external);
    QCOMPARE(annotation.matched.front().experimentalPeakIndex, std::size_t{1});
    QCOMPARE(annotation.matched.front().type, OpenMSViewer::IonType::Y);
  }

  void matchesGeneratedTheoreticalIons()
  {
    OpenMS::TheoreticalSpectrumGenerator generator;
    auto parameters = generator.getParameters();
    parameters.setValue("add_b_ions", "true");
    parameters.setValue("add_y_ions", "true");
    generator.setParameters(parameters);
    OpenMS::MSSpectrum theoretical;
    generator.getSpectrum(theoretical, OpenMS::AASequence::fromString("PEPTIDE"), 1, 2);
    QVERIFY(!theoretical.empty());

    OpenMS::MSSpectrum experimental;
    experimental.setMSLevel(2);
    OpenMS::Peak1D peak;
    peak.setMZ(theoretical.front().getMZ() + 0.01);
    peak.setIntensity(100.0F);
    experimental.push_back(peak);

    OpenMSViewer::PeptideHitRecord hit;
    hit.sequence = QStringLiteral("PEPTIDE");
    hit.charge = 2;
    const auto annotation = OpenMSViewer::computeSpectrumAnnotation(experimental, hit, 0.05);
    QVERIFY(annotation.error.isEmpty());
    QVERIFY(annotation.theoreticalCount > 0);
    QVERIFY(!annotation.matched.empty());
    QVERIFY(!annotation.matched.front().external);
    QVERIFY(qAbs(annotation.matched.front().mzError - 0.01) < 1e-6);
  }
};

int runSpectrumAnnotationTests(int argc, char** argv)
{
  SpectrumAnnotationTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "SpectrumAnnotationTest.moc"
