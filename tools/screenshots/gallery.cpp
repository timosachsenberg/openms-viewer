// Retained screenshot generator for OpenMS Viewer.
//
// Regenerates docs/screenshots/*.png so visual changes show up as image diffs in
// git. Raw-centric views (peak map, metadata, FAIMS, ion mobility / diaPASEF) use
// real datasets from archive.openms.de, cached locally by tools/screenshots/
// fetch-data.sh. Derived views (features, identifications, consensus, OpenSWATH)
// and the annotated spectrum use deterministic OpenMS test fixtures / synthetic
// bundles, since the archive ships raw runs only.
//
// Usage (offscreen, from the repo root):
//   QT_QPA_PLATFORM=offscreen ./build/openms-viewer-gallery
// Env overrides: SCREENSHOT_CACHE, SCREENSHOT_OUT, OPENMS_TEST_DIR.
// Real-data shots are SKIPPED (not failed) when their cached file is absent.

#include "MainWindow.h"
#include "widgets/ConsensusPanel.h"
#include "widgets/IonMobilityPanelWidget.h"
#include "widgets/MetadataBrowserWidget.h"
#include "widgets/OswPanel.h"
#include "widgets/PeakMapWidget.h"
#include "widgets/SpectrumWidget.h"

#include "annotation/SpectrumAnnotation.h"
#include "model/IdentificationData.h"

#include <OpenMS/CHEMISTRY/AASequence.h>
#include <OpenMS/CHEMISTRY/TheoreticalSpectrumGenerator.h>
#include <OpenMS/DATASTRUCTURES/Param.h>
#include <OpenMS/FORMAT/ImzMLFile.h>
#include <OpenMS/FORMAT/SqliteConnector.h>
#include <OpenMS/FORMAT/SqMassFile.h>
#include <OpenMS/KERNEL/MSExperiment.h>
#include <OpenMS/KERNEL/MSSpectrum.h>
#include <OpenMS/KERNEL/StandardTypes.h>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QWidget>

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace
{
  QString gOut;
  QString gCache;
  QString gFixtures;

  int gOk = 0, gSkip = 0, gFail = 0;

  void pump(int ms)
  {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
    {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QThread::msleep(5);
    }
  }

  bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
  {
    QElapsedTimer timer;
    timer.start();
    while (!predicate())
    {
      if (timer.elapsed() > timeoutMs) return false;
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      QThread::msleep(10);
    }
    return true;
  }

  bool saveGrab(QWidget& widget, const QString& name)
  {
    const QString path = QDir(gOut).filePath(name);
    if (widget.grab().save(path)) { qInfo("  [ok]   %s", qUtf8Printable(name)); ++gOk; return true; }
    qWarning("  [fail] %s (grab/save failed)", qUtf8Printable(name)); ++gFail; return false;
  }

  void skip(const QString& name, const QString& why)
  {
    qInfo("  [skip] %s (%s)", qUtf8Printable(name), qUtf8Printable(why)); ++gSkip;
  }

  QString cacheFile(const QString& rel) { return QDir(gCache).filePath(rel); }
  QString fixtureFile(const QString& rel) { return QDir(gFixtures).filePath(rel); }

  // The DIA .d unzips to a vendor-named directory; find it under the cache, and
  // never mistake the (MALDI) tissue .d for the diaPASEF one.
  QString firstDotD()
  {
    const QStringList dirs = QDir(gCache).entryList({QStringLiteral("*.d")}, QDir::Dirs | QDir::NoDotAndDotDot);
    QString fallback;
    for (const QString& d : dirs)
    {
      if (d.contains(QStringLiteral("moustissue"), Qt::CaseInsensitive)) continue;  // MALDI, not IM
      if (d.contains(QStringLiteral("DIA"), Qt::CaseInsensitive)) return QDir(gCache).filePath(d);
      if (fallback.isEmpty()) fallback = QDir(gCache).filePath(d);
    }
    return fallback;
  }

  // Load one file/folder into a MainWindow and wait for the experiment to appear.
  bool loadExperiment(OpenMSViewer::MainWindow& window, const QString& path, int timeoutMs = 60000)
  {
    window.loadFiles({path});
    auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
    return peakMap && waitUntil([peakMap] { return peakMap->hasExperiment(); }, timeoutMs);
  }

  bool grabDockPanel(OpenMSViewer::MainWindow& window, const QString& dockName,
                     int width, int height, const QString& png, int settleMs = 2500)
  {
    auto* dock = window.findChild<QDockWidget*>(dockName);
    if (!dock || !waitUntil([dock] { return dock->isEnabled(); }, 10000))
    {
      skip(png, QStringLiteral("panel never populated"));
      return false;
    }
    dock->setFloating(true);
    dock->resize(width, height);
    dock->show();
    dock->raise();
    pump(settleMs);
    return dock->widget() && saveGrab(*dock->widget(), png);
  }
}

// ---- Fixture / synthetic views (deterministic) ------------------------------

static void heroWithFeatures()
{
  const QString mzml = fixtureFile(QStringLiteral("FeatureFinderCentroided_1_input.mzML"));
  const QString feat = fixtureFile(QStringLiteral("FeatureFinderCentroided_1_1_output.featureXML"));
  if (!QFile::exists(mzml)) { skip(QStringLiteral("main-window.png"), QStringLiteral("fixture missing")); return; }

  OpenMSViewer::MainWindow window;
  window.resize(1500, 940);
  window.show();
  window.loadFiles({mzml, feat});
  auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
  if (!peakMap || !waitUntil([peakMap] { return peakMap->hasExperiment() && !peakMap->rasterImage().isNull(); }, 20000))
  { skip(QStringLiteral("main-window.png"), QStringLiteral("load failed")); return; }
  pump(900);
  saveGrab(window, QStringLiteral("main-window.png"));
}

static void annotatedSpectrum()
{
  using namespace OpenMS;
  OpenMSViewer::MainWindow themeHost;  // installs the dark palette on qApp

  const AASequence peptide = AASequence::fromString("SYELPDGQVITIGNER");
  TheoreticalSpectrumGenerator generator;
  Param parameters = generator.getParameters();
  parameters.setValue("add_b_ions", "true");
  parameters.setValue("add_y_ions", "true");
  parameters.setValue("add_metainfo", "true");
  generator.setParameters(parameters);
  PeakSpectrum theoretical;
  generator.getSpectrum(theoretical, peptide, 1, 1);

  auto experiment = std::make_shared<MSExperiment>();
  MSSpectrum observed;
  observed.setMSLevel(2);
  observed.setRT(1850.0);
  observed.getPrecursors().resize(1);
  observed.getPrecursors()[0].setMZ(peptide.getMZ(2));
  observed.getPrecursors()[0].setCharge(2);
  for (const Peak1D& peak : theoretical)
  {
    const double envelope = std::exp(-0.5 * std::pow((peak.getMZ() - 900.0) / 520.0, 2.0));
    Peak1D observedPeak;
    observedPeak.setMZ(peak.getMZ());
    observedPeak.setIntensity(1000.0F * static_cast<float>(
      (0.25 + 0.75 * std::fabs(std::sin(peak.getMZ() * 0.7))) * (0.45 + envelope)));
    observed.push_back(observedPeak);
  }
  for (const double noiseMz : {233.1, 512.4, 707.9, 1044.2, 1330.8})
  {
    Peak1D noisePeak;
    noisePeak.setMZ(noiseMz);
    noisePeak.setIntensity(140.0F);
    observed.push_back(noisePeak);
  }
  observed.sortByPosition();
  experiment->addSpectrum(observed);

  OpenMSViewer::PeptideHitRecord hit;
  hit.sequence = QStringLiteral("SYELPDGQVITIGNER");
  hit.charge = 2;

  OpenMSViewer::SpectrumWidget widget;
  widget.resize(900, 560);
  widget.show();
  widget.setExperiment(experiment);
  widget.setSpectrumIndex(0);
  widget.setShowMzLabels(true);
  widget.setAnnotation(OpenMSViewer::computeSpectrumAnnotation(observed, hit, 0.05));
  widget.setAnnotationEnabled(true);
  widget.setShowUnmatchedTheoretical(true);
  widget.setMirrorMode(true);
  pump(300);
  saveGrab(widget, QStringLiteral("spectrum-annotated.png"));
}

static void consensusView()
{
  const QString path = fixtureFile(QStringLiteral("FeatureFinderMultiplex_9_output.consensusXML"));
  if (!QFile::exists(path)) { skip(QStringLiteral("consensus.png"), QStringLiteral("fixture missing")); return; }

  OpenMSViewer::MainWindow window;
  window.resize(1200, 780);
  window.show();
  window.loadFiles({path});
  auto* panel = window.findChild<OpenMSViewer::ConsensusPanel*>();
  auto* table = panel ? panel->findChild<QAbstractItemView*>(QStringLiteral("consensusTable")) : nullptr;
  // The table widget exists from construction; wait for its model to be populated.
  if (!table || !waitUntil([table] { return table->model() && table->model()->rowCount() > 0; }, 15000))
  { skip(QStringLiteral("consensus.png"), QStringLiteral("panel not populated")); return; }
  pump(500);
  panel->resize(1120, 700);
  saveGrab(*panel, QStringLiteral("consensus.png"));
}

static void buildOsw(const QString& path)
{
  OpenMS::SqliteConnector conn(path.toStdString());
  const char* schema[] = {
    "CREATE TABLE RUN (ID INT, FILENAME TEXT);",
    "INSERT INTO RUN VALUES (7,'sample_run.mzML');",
    "CREATE TABLE PEPTIDE (ID INT, UNMODIFIED_SEQUENCE TEXT, MODIFIED_SEQUENCE TEXT, DECOY INT);",
    "INSERT INTO PEPTIDE VALUES (10,'ELVISLIVESK','ELVISLIVESK',0);",
    "INSERT INTO PEPTIDE VALUES (11,'PEPTIDESEQK','PEPTIDESEQK',0);",
    "CREATE TABLE PRECURSOR (ID INT, PRECURSOR_MZ REAL, CHARGE INT, LIBRARY_RT REAL, DECOY INT);",
    "INSERT INTO PRECURSOR VALUES (100,620.82,2,1218.0,0);",
    "INSERT INTO PRECURSOR VALUES (101,510.27,2,940.0,0);",
    "CREATE TABLE PRECURSOR_PEPTIDE_MAPPING (PRECURSOR_ID INT, PEPTIDE_ID INT);",
    "INSERT INTO PRECURSOR_PEPTIDE_MAPPING VALUES (100,10);",
    "INSERT INTO PRECURSOR_PEPTIDE_MAPPING VALUES (101,11);",
    "CREATE TABLE TRANSITION (ID INT, PRODUCT_MZ REAL, CHARGE INT, TYPE TEXT, ANNOTATION TEXT,"
      " ORDINAL INT, DETECTING INT, IDENTIFYING INT, QUANTIFYING INT, LIBRARY_INTENSITY REAL, DECOY INT);",
    "INSERT INTO TRANSITION VALUES (1000,405.2,1,'y','y3',3,1,0,1,4200.0,0);",
    "INSERT INTO TRANSITION VALUES (1001,633.3,1,'y','y5',5,1,0,1,9000.0,0);",
    "INSERT INTO TRANSITION VALUES (1002,760.4,1,'y','y6',6,1,0,1,6100.0,0);",
    "INSERT INTO TRANSITION VALUES (1003,504.2,1,'b','b4',4,1,0,1,3300.0,0);",
    "INSERT INTO TRANSITION VALUES (1004,881.5,1,'y','y7',7,1,0,1,2500.0,0);",
    "CREATE TABLE TRANSITION_PRECURSOR_MAPPING (TRANSITION_ID INT, PRECURSOR_ID INT);",
    "INSERT INTO TRANSITION_PRECURSOR_MAPPING VALUES (1000,100);",
    "INSERT INTO TRANSITION_PRECURSOR_MAPPING VALUES (1001,100);",
    "INSERT INTO TRANSITION_PRECURSOR_MAPPING VALUES (1002,100);",
    "INSERT INTO TRANSITION_PRECURSOR_MAPPING VALUES (1003,100);",
    "INSERT INTO TRANSITION_PRECURSOR_MAPPING VALUES (1004,100);",
    "CREATE TABLE FEATURE (ID INT, RUN_ID INT, PRECURSOR_ID INT, EXP_RT REAL, EXP_IM REAL,"
      " NORM_RT REAL, DELTA_RT REAL, LEFT_WIDTH REAL, RIGHT_WIDTH REAL);",
    "INSERT INTO FEATURE VALUES (5000,7,100,1220.0,0,1218.0,2.0,1204.0,1236.0);",
    "CREATE TABLE SCORE_MS2 (FEATURE_ID INTEGER, SCORE REAL, RANK INTEGER, PVALUE REAL, QVALUE REAL, PEP REAL);",
    "INSERT INTO SCORE_MS2 VALUES (5000,4.12,1,0.0004,0.008,0.011);",
    "CREATE TABLE FEATURE_MS2 (FEATURE_ID INT, AREA_INTENSITY REAL, APEX_INTENSITY REAL,"
      " VAR_XCORR_SHAPE REAL, VAR_LIBRARY_CORR REAL, VAR_LOG_SN_SCORE REAL);",
    "INSERT INTO FEATURE_MS2 VALUES (5000,284000.0,31000.0,0.97,0.94,4.6);",
  };
  for (const char* statement : schema) conn.executeStatement(statement);
}

static void buildSqMass(const QString& path)
{
  OpenMS::MSExperiment experiment;
  const auto add = [&](const std::string& nativeId, float scale, double apex)
  {
    OpenMS::MSChromatogram chromatogram;
    chromatogram.setNativeID(nativeId);
    for (int i = 0; i < 60; ++i)
    {
      const double rt = 1195.0 + i * 0.8;
      const double x = (rt - apex) / 7.0;
      OpenMS::ChromatogramPeak peak;
      peak.setRT(rt);
      peak.setIntensity(scale * static_cast<float>(std::exp(-0.5 * x * x))
                        + scale * 0.04F * static_cast<float>((i * 7) % 5));
      chromatogram.push_back(peak);
    }
    experiment.addChromatogram(chromatogram);
  };
  add("1000", 4200.0F, 1220.0);
  add("1001", 9000.0F, 1220.0);
  add("1002", 6100.0F, 1220.5);
  add("1003", 3300.0F, 1219.5);
  add("1004", 2500.0F, 1220.0);
  add("100_Precursor_i0", 12000.0F, 1220.0);
  OpenMS::SqMassFile().store(path.toStdString(), experiment);
}

static void openSwathView()
{
  QTemporaryDir dir;
  if (!dir.isValid()) { skip(QStringLiteral("openswath.png"), QStringLiteral("no temp dir")); return; }
  const QString oswPath = dir.filePath(QStringLiteral("dia.osw"));
  const QString sqMassPath = dir.filePath(QStringLiteral("dia.sqMass"));
  buildOsw(oswPath);
  buildSqMass(sqMassPath);

  OpenMSViewer::MainWindow window;
  window.resize(1280, 820);
  window.show();
  window.loadFiles({oswPath});
  auto* panel = window.findChild<OpenMSViewer::OswPanel*>();
  auto* precTable = panel ? panel->findChild<QTableView*>(QStringLiteral("oswPrecursorTable")) : nullptr;
  if (!precTable || !waitUntil([precTable] { return precTable->model() && precTable->model()->rowCount() >= 2; }, 15000))
  { skip(QStringLiteral("openswath.png"), QStringLiteral("panel not ready")); return; }

  for (int row = 0; row < precTable->model()->rowCount(); ++row)
    if (precTable->model()->index(row, 0).data().toString() == QStringLiteral("ELVISLIVESK"))
    { precTable->selectRow(row); break; }
  auto* pgTable = panel->findChild<QTableView*>(QStringLiteral("oswPeakGroupTable"));
  waitUntil([pgTable] { return pgTable && pgTable->model() && pgTable->model()->rowCount() >= 1; }, 8000);
  pump(1500);
  panel->resize(1200, 760);
  saveGrab(*panel, QStringLiteral("openswath.png"));
}

static void syntheticImaging()
{
  // The viewer's imaging is imzML-only; the archive MALDI dataset is Bruker .tsf,
  // which loads as a plain run, not a spatial image. Until a .tsf->imzML step is
  // added, the MSI view is shown on a synthetic continuous-mode image.
  using namespace OpenMS;
  QTemporaryDir dir;
  if (!dir.isValid()) { skip(QStringLiteral("imaging.png"), QStringLiteral("no temp dir")); return; }
  const QString path = dir.filePath(QStringLiteral("tissue.imzML"));

  std::vector<double> mzGrid;
  for (int k = 0; k < 60; ++k) mzGrid.push_back(150.0 + k * (800.0 / 59.0));
  const auto blob = [](double px, double py, double cx, double cy, double sigma)
  { return std::exp(-((px - cx) * (px - cx) + (py - cy) * (py - cy)) / (2.0 * sigma * sigma)); };

  MSExperiment experiment;
  const int width = 48, height = 36;
  for (int y = 1; y <= height; ++y)
    for (int x = 1; x <= width; ++x)
    {
      const double px = static_cast<double>(x) / width;
      const double py = static_cast<double>(y) / height;
      const double ionA = blob(px, py, 0.32, 0.38, 0.16);
      const double ionB = blob(px, py, 0.70, 0.64, 0.13);
      MSSpectrum spectrum;
      spectrum.setMSLevel(1);
      spectrum.setMetaValue("imzml:x", static_cast<int>(x));
      spectrum.setMetaValue("imzml:y", static_cast<int>(y));
      spectrum.setMetaValue("imzml:z", 1);
      for (const double mz : mzGrid)
      {
        const double a = 4000.0 * ionA * std::exp(-0.5 * std::pow((mz - 430.0) / 9.0, 2.0));
        const double b = 5200.0 * ionB * std::exp(-0.5 * std::pow((mz - 690.0) / 9.0, 2.0));
        const double background = 120.0 * std::exp(-0.5 * std::pow((mz - 250.0) / 140.0, 2.0));
        spectrum.push_back(Peak1D(mz, static_cast<float>(a + b + background + 15.0)));
      }
      experiment.addSpectrum(spectrum);
    }
  experiment.setMetaValue("imzml:max_count_x", width);
  experiment.setMetaValue("imzml:max_count_y", height);
  experiment.setMetaValue("imzml:max_count_z", 1);
  experiment.setMetaValue("imzml:imaging_mode", "continuous");
  ImzMLFile().store(path.toStdString(), experiment);

  OpenMSViewer::MainWindow window;
  window.resize(1200, 760);
  window.show();
  window.loadFiles({path});
  auto* dock = window.findChild<QDockWidget*>(QStringLiteral("imagingDock"));
  if (!dock || !waitUntil([dock] { return dock->isEnabled(); }, 15000))
  { skip(QStringLiteral("imaging.png"), QStringLiteral("imaging load failed")); return; }
  pump(1200);
  dock->setFloating(true);
  dock->resize(1000, 640);
  dock->show();
  dock->raise();
  pump(600);
  if (dock->widget()) saveGrab(*dock->widget(), QStringLiteral("imaging.png"));
}

// ---- Real archive views -----------------------------------------------------

static void realPeakMapAndMetadata()
{
  const QString mzml = cacheFile(QStringLiteral("Thermo-LTQ-Velos-PXD000155.mzML"));
  if (!QFile::exists(mzml))
  { skip(QStringLiteral("peak-map.png"), QStringLiteral("cache miss")); skip(QStringLiteral("metadata.png"), QStringLiteral("cache miss")); return; }

  OpenMSViewer::MainWindow window;
  window.resize(1400, 900);
  window.show();
  if (!loadExperiment(window, mzml))
  { skip(QStringLiteral("peak-map.png"), QStringLiteral("load failed")); skip(QStringLiteral("metadata.png"), QStringLiteral("load failed")); return; }
  auto* peakMap = window.findChild<OpenMSViewer::PeakMapWidget*>();
  if (!peakMap || !waitUntil([peakMap] { return !peakMap->rasterImage().isNull(); }, 15000))
    skip(QStringLiteral("peak-map.png"), QStringLiteral("raster never rendered"));
  else
  {
    pump(300);  // the bounded peak-map canvas is already rendered 1:1
    saveGrab(*peakMap, QStringLiteral("peak-map.png"));
  }

  auto* metadata = window.findChild<OpenMSViewer::MetadataBrowserWidget*>();
  auto* metadataDock = window.findChild<QDockWidget*>(QStringLiteral("metadataDock"));
  if (metadata && metadataDock)
  {
    if (auto* tree = metadata->findChild<QTreeWidget*>()) tree->expandToDepth(1);
    metadataDock->setFloating(true);
    metadataDock->resize(460, 640);
    metadataDock->show();
    metadataDock->raise();
    pump(300);
    saveGrab(*metadata, QStringLiteral("metadata.png"));
  }
  else skip(QStringLiteral("metadata.png"), QStringLiteral("no metadata panel"));
}

static void realFaims()
{
  const QString mzml = cacheFile(QStringLiteral("ExplorisFAIMS-DDA-LFQ.mzML"));
  if (!QFile::exists(mzml)) { skip(QStringLiteral("faims.png"), QStringLiteral("cache miss")); return; }
  OpenMSViewer::MainWindow window;
  window.resize(1200, 760);
  window.show();
  if (!loadExperiment(window, mzml, 180000))  // 1GB mzML — allow time
  { skip(QStringLiteral("faims.png"), QStringLiteral("load failed")); return; }
  grabDockPanel(window, QStringLiteral("faimsDock"), 1120, 700, QStringLiteral("faims.png"));
}

static void realIonMobility()
{
  const QString dPath = firstDotD();
  if (dPath.isEmpty()) { skip(QStringLiteral("ion-mobility.png"), QStringLiteral("no .d in cache")); skip(QStringLiteral("dia-windows.png"), QStringLiteral("no .d in cache")); return; }

  OpenMSViewer::MainWindow window;
  window.resize(1200, 760);
  window.show();
  if (!loadExperiment(window, dPath, 180000))
  { skip(QStringLiteral("ion-mobility.png"), QStringLiteral("opentims load failed")); skip(QStringLiteral("dia-windows.png"), QStringLiteral("opentims load failed")); return; }

  auto* imPlot = window.findChild<OpenMSViewer::IonMobilityPlotWidget*>();
  auto* imDock = window.findChild<QDockWidget*>(QStringLiteral("ionMobilityDock"));
  if (!imPlot || !imDock || !waitUntil([imDock] { return imDock->isEnabled(); }, 10000))
  { skip(QStringLiteral("ion-mobility.png"), QStringLiteral("IM panel not populated")); skip(QStringLiteral("dia-windows.png"), QStringLiteral("IM panel not populated")); return; }

  imDock->setFloating(true);
  imDock->resize(1000, 640);
  imDock->show();
  imDock->raise();

  // Toggle the panel's own "DIA windows" checkbox so the control reflects the view.
  QCheckBox* diaBox = nullptr;
  for (QCheckBox* box : window.findChildren<QCheckBox*>())
    if (box->text().contains(QStringLiteral("DIA"), Qt::CaseInsensitive)) { diaBox = box; break; }

  if (diaBox) diaBox->setChecked(false); else imPlot->setShowDiaWindows(false);
  pump(2500);
  if (imDock->widget()) saveGrab(*imDock->widget(), QStringLiteral("ion-mobility.png"));

  if (diaBox) diaBox->setChecked(true); else imPlot->setShowDiaWindows(true);
  pump(2000);
  if (imDock->widget()) saveGrab(*imDock->widget(), QStringLiteral("dia-windows.png"));
}

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty() ? QByteArray("offscreen") : qgetenv("QT_QPA_PLATFORM"));
  QApplication app(argc, argv);

  // Isolate from the user's real settings and drive the theme MainWindow restores
  // at construction. SCREENSHOT_THEME=light renders the whole gallery in light mode.
  QApplication::setOrganizationName(QStringLiteral("OpenMSViewerGallery"));
  QApplication::setApplicationName(QStringLiteral("OpenMSViewerGallery"));
  QStandardPaths::setTestModeEnabled(true);
  const bool lightTheme =
    qEnvironmentVariable("SCREENSHOT_THEME").compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0;
  { QSettings settings; settings.setValue(QStringLiteral("appearance/dark"), !lightTheme); }

  // A failed load pops a modal error dialog whose exec() would block forever under
  // the offscreen platform. Auto-dismiss any modal so the load resolves to [skip].
  QTimer modalWatchdog;
  QObject::connect(&modalWatchdog, &QTimer::timeout, [] {
    if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
  });
  modalWatchdog.start(300);

  const QString repo = QDir::currentPath();
  gOut = qEnvironmentVariable("SCREENSHOT_OUT", QDir(repo).filePath(QStringLiteral("docs/screenshots")));
  gCache = qEnvironmentVariable("SCREENSHOT_CACHE", QDir(repo).filePath(QStringLiteral(".screenshot-cache")));
  gFixtures = qEnvironmentVariable("OPENMS_TEST_DIR",
    QDir::home().filePath(QStringLiteral("Development/OpenMS/src/tests/topp")));
  QDir().mkpath(gOut);

  qInfo("gallery: out=%s", qUtf8Printable(gOut));
  qInfo("gallery: cache=%s", qUtf8Printable(gCache));
  qInfo("gallery: fixtures=%s", qUtf8Printable(gFixtures));

  // Optional args select a subset of view tags to regenerate (default: all).
  QStringList filter;
  for (int i = 1; i < argc; ++i) filter << QString::fromLocal8Bit(argv[i]);
  const auto want = [&](const char* tag) { return filter.isEmpty() || filter.contains(QLatin1String(tag)); };

  qInfo("Fixture / synthetic views:");
  if (want("hero")) heroWithFeatures();
  if (want("spectrum")) annotatedSpectrum();
  if (want("consensus")) consensusView();
  if (want("osw")) openSwathView();
  if (want("imaging")) syntheticImaging();

  qInfo("Real archive views:");
  if (want("peakmap")) realPeakMapAndMetadata();
  if (want("faims")) realFaims();
  if (want("im")) realIonMobility();

  qInfo("gallery: %d ok, %d skipped, %d failed", gOk, gSkip, gFail);
  return gFail > 0 ? 1 : 0;
}
