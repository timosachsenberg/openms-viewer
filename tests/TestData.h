#pragma once

#include <OpenMS/KERNEL/MSExperiment.h>
#include <OpenMS/KERNEL/MSChromatogram.h>
#include <OpenMS/KERNEL/ChromatogramPeak.h>
#include <OpenMS/KERNEL/MSSpectrum.h>
#include <OpenMS/KERNEL/Peak1D.h>
#include <OpenMS/METADATA/Precursor.h>
#include <OpenMS/METADATA/Product.h>
#include <OpenMS/METADATA/PeptideIdentification.h>
#include <OpenMS/METADATA/PeptideIdentificationList.h>
#include <OpenMS/METADATA/PeptideHit.h>
#include <OpenMS/METADATA/ProteinIdentification.h>
#include <OpenMS/CHEMISTRY/AASequence.h>
#include <OpenMS/KERNEL/Feature.h>
#include <OpenMS/KERNEL/FeatureMap.h>
#include <OpenMS/DATASTRUCTURES/ConvexHull2D.h>
#include <OpenMS/IONMOBILITY/IMDataConverter.h>
#include <OpenMS/IONMOBILITY/IMTypes.h>

#include <array>
#include <initializer_list>
#include <utility>

namespace OpenMSViewer::TestData
{
  inline OpenMS::MSSpectrum spectrum(double rt, unsigned int msLevel,
                                     std::initializer_list<std::pair<double, float>> peaks)
  {
    OpenMS::MSSpectrum result;
    result.setRT(rt);
    result.setMSLevel(msLevel);
    for (const auto& [mz, intensity] : peaks)
    {
      OpenMS::Peak1D peak;
      peak.setMZ(mz);
      peak.setIntensity(intensity);
      result.push_back(peak);
    }
    result.sortByPosition();
    return result;
  }

  inline OpenMS::MSExperiment experiment()
  {
    OpenMS::MSExperiment result;
    result.addSpectrum(spectrum(10.0, 1, {{400.0, 10.0F}, {500.0, 100.0F}}));
    auto ms2 = spectrum(11.0, 2, {{200.0, 50.0F}, {450.0, 500.0F}, {700.0, 25.0F}});
    OpenMS::Precursor precursor;
    precursor.setMZ(500.2);
    precursor.setCharge(2);
    ms2.setPrecursors({precursor});
    result.addSpectrum(ms2);
    result.addSpectrum(spectrum(20.0, 1, {{410.0, 50.0F}, {600.0, 25.0F}}));
    result.sortSpectra(true);
    return result;
  }

  inline OpenMS::MSExperiment experimentWithChromatograms()
  {
    OpenMS::MSExperiment result = experiment();

    OpenMS::MSChromatogram tic;
    tic.setNativeID("TIC");
    tic.setChromatogramType(
      OpenMS::ChromatogramSettings::ChromatogramType::TOTAL_ION_CURRENT_CHROMATOGRAM);
    for (const auto& [rt, intensity] : {std::pair{9.0, 10.0F}, {15.0, 80.0F}, {21.0, 20.0F}})
    {
      OpenMS::ChromatogramPeak peak;
      peak.setRT(rt);
      peak.setIntensity(intensity);
      tic.push_back(peak);
    }
    result.addChromatogram(tic);

    OpenMS::MSChromatogram transition;
    transition.setNativeID("transition_500.2_200.0");
    transition.setChromatogramType(
      OpenMS::ChromatogramSettings::ChromatogramType::SELECTED_REACTION_MONITORING_CHROMATOGRAM);
    OpenMS::Precursor precursor;
    precursor.setMZ(500.2);
    precursor.setCharge(2);
    transition.setPrecursor(precursor);
    OpenMS::Product product;
    product.setMZ(200.0);
    transition.setProduct(product);
    for (const auto& [rt, intensity] : {std::pair{10.0, 5.0F}, {14.0, 50.0F}, {18.0, 7.0F}})
    {
      OpenMS::ChromatogramPeak peak;
      peak.setRT(rt);
      peak.setIntensity(intensity);
      transition.push_back(peak);
    }
    result.addChromatogram(transition);
    return result;
  }

  inline OpenMS::MSExperiment chromatogramOnlyExperiment()
  {
    OpenMS::MSExperiment result = experimentWithChromatograms();
    result.getSpectra().clear();
    return result;
  }

  inline OpenMS::MSExperiment ionMobilityExperiment()
  {
    OpenMS::MSExperiment result;
    auto ms1 = spectrum(10.0, 1, {{400.0, 10.0F}, {500.0, 100.0F}, {600.0, 30.0F}});
    OpenMS::MSSpectrum::FloatDataArray ms1Mobility;
    OpenMS::IMDataConverter::setIMUnit(ms1Mobility, OpenMS::DriftTimeUnit::VSSC);
    ms1Mobility.push_back(0.8F);
    ms1Mobility.push_back(1.0F);
    ms1Mobility.push_back(1.2F);
    ms1.setFloatDataArrays({ms1Mobility});
    result.addSpectrum(ms1);

    auto ms2 = spectrum(11.0, 2, {{200.0, 20.0F}, {500.0, 80.0F}, {700.0, 10.0F}});
    OpenMS::Precursor precursor;
    precursor.setMZ(500.0);
    precursor.setCharge(2);
    precursor.setIsolationWindowLowerOffset(2.0);
    precursor.setIsolationWindowUpperOffset(3.0);
    ms2.setPrecursors({precursor});
    OpenMS::MSSpectrum::FloatDataArray ms2Mobility;
    OpenMS::IMDataConverter::setIMUnit(ms2Mobility, OpenMS::DriftTimeUnit::VSSC);
    ms2Mobility.push_back(0.85F);
    ms2Mobility.push_back(1.05F);
    ms2Mobility.push_back(1.25F);
    ms2.setFloatDataArrays({ms2Mobility});
    result.addSpectrum(ms2);
    result.addSpectrum(spectrum(12.0, 1, {{450.0, 5.0F}}));
    result.sortSpectra(true);
    return result;
  }

  inline OpenMS::MSExperiment faimsExperiment()
  {
    OpenMS::MSExperiment result;
    const std::array<double, 4> rts{10.0, 20.0, 30.0, 40.0};
    const std::array<double, 4> cvs{-50.0, -40.0, -50.0, -40.0};
    for (std::size_t index = 0; index < rts.size(); ++index)
    {
      auto scan = spectrum(rts[index], 1,
                           {{400.0 + static_cast<double>(index), 20.0F},
                            {500.0 + static_cast<double>(index), 100.0F}});
      scan.setDriftTime(cvs[index]);
      scan.setDriftTimeUnit(OpenMS::DriftTimeUnit::FAIMS_COMPENSATION_VOLTAGE);
      scan.setMetaValue("FAIMS compensation voltage", cvs[index]);
      result.addSpectrum(scan);
    }
    result.sortSpectra(true);
    return result;
  }

  inline std::pair<std::vector<OpenMS::ProteinIdentification>, OpenMS::PeptideIdentificationList>
  identifications()
  {
    OpenMS::ProteinIdentification protein;
    protein.setIdentifier("run_1");
    protein.setSearchEngine("TestEngine");
    protein.setSearchEngineVersion("1.0");

    OpenMS::PeptideIdentification linked;
    linked.setIdentifier("run_1");
    linked.setRT(11.2);
    linked.setMZ(500.25);
    linked.setScoreType("hyperscore");
    linked.setHigherScoreBetter(true);
    linked.setMetaValue("spectrum_reference", "scan=2");

    OpenMS::PeptideHit best;
    best.setSequence(OpenMS::AASequence::fromString("PEPTIDE"));
    best.setScore(42.0);
    best.setCharge(2);
    best.setMetaValue("target_decoy", "target");
    OpenMS::PeptideHit::PeakAnnotation annotation;
    annotation.annotation = "y4+";
    annotation.charge = 1;
    annotation.mz = 450.0;
    annotation.intensity = 1.0;
    best.setPeakAnnotations({annotation});

    OpenMS::PeptideHit second;
    second.setSequence(OpenMS::AASequence::fromString("PEPTIDER"));
    second.setScore(30.0);
    second.setCharge(2);
    linked.setHits({best, second});

    OpenMS::PeptideIdentification unlinked;
    unlinked.setIdentifier("run_1");
    unlinked.setRT(50.0);
    unlinked.setMZ(800.0);
    unlinked.setScoreType("hyperscore");
    OpenMS::PeptideHit unlinkedHit;
    unlinkedHit.setSequence(OpenMS::AASequence::fromString("UNLINKED"));
    unlinkedHit.setScore(12.0);
    unlinkedHit.setCharge(3);
    unlinked.setHits({unlinkedHit});

    OpenMS::PeptideIdentificationList peptideIds;
    peptideIds.push_back(linked);
    peptideIds.push_back(unlinked);
    return {{protein}, peptideIds};
  }

  inline OpenMS::FeatureMap featureMap()
  {
    OpenMS::FeatureMap result;
    result.ensureUniqueId();

    OpenMS::Feature first;
    first.setRT(100.5);
    first.setMZ(500.25);
    first.setIntensity(1000.0F);
    first.setCharge(2);
    first.setOverallQuality(0.95F);
    first.ensureUniqueId();
    OpenMS::ConvexHull2D hull;
    OpenMS::ConvexHull2D::PointArrayType points;
    points.emplace_back(99.0, 499.5);
    points.emplace_back(99.0, 501.0);
    points.emplace_back(102.0, 501.0);
    points.emplace_back(102.0, 499.5);
    hull.setHullPoints(points);
    first.getConvexHulls().push_back(hull);
    result.push_back(first);

    OpenMS::Feature second;
    second.setRT(150.0);
    second.setMZ(700.0);
    second.setIntensity(250.0F);
    second.setCharge(0);
    second.setOverallQuality(0.0F);
    second.ensureUniqueId();
    result.push_back(second);
    result.updateRanges();
    return result;
  }
}
