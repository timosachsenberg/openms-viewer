#include "MainWindow.h"

#include <OpenMS/SYSTEM/BuildInfo.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QThreadPool>

#include <algorithm>

namespace
{
  // WSLg defaults to the Wayland platform, where Qt draws the cursor client-side
  // with no size hint (giant pointer) and cannot position its own top-level
  // surfaces (floating panels can only be moved by the compositor). Preferring
  // XWayland (xcb) and setting a cursor size sidesteps both. Both are only
  // defaults: an explicit QT_QPA_PLATFORM or XCURSOR_SIZE still wins.
  void applyWslPlatformDefaults()
  {
    const bool isWsl = qEnvironmentVariableIsSet("WSL_INTEROP")
                       || qEnvironmentVariableIsSet("WSL_DISTRO_NAME");
    if (!isWsl) return;

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
      qputenv("QT_QPA_PLATFORM", "xcb");
    if (qEnvironmentVariableIsEmpty("XCURSOR_SIZE"))
      qputenv("XCURSOR_SIZE", "24");
  }

  void applyBundledDotNetDefault()
  {
    // Portable packages place the .NET runtime next to bin/. Honour an
    // administrator/user override, otherwise let the Thermo bridge's nethost
    // loader resolve the bundled runtime when a RAW file is opened.
    if (!qEnvironmentVariableIsEmpty("DOTNET_ROOT")) return;

    const QDir applicationDirectory(QApplication::applicationDirPath());
    const QString bundledRoot = QDir::cleanPath(applicationDirectory.filePath("../dotnet"));
    if (QFileInfo::exists(QDir(bundledRoot).filePath("host/fxr")))
      qputenv("DOTNET_ROOT", QDir::toNativeSeparators(bundledRoot).toUtf8());
  }

  void configureWorkerThreads()
  {
    // QtConcurrent drives file loading and background rendering. OpenMS's mzML
    // handler then uses OpenMP inside that worker to decode spectrum binary-data
    // batches in parallel. Match both pools to the CPUs available to this process.
    const int cpuThreads = std::max(1, QThread::idealThreadCount());
    QThreadPool::globalInstance()->setMaxThreadCount(cpuThreads);

    // Preserve an explicit administrator/user cap, otherwise make the OpenMS
    // decoder's maximum match Qt and the process CPU affinity.
    if (!qEnvironmentVariableIsSet("OMP_NUM_THREADS"))
      OpenMS::Internal::OpenMSBuildInfo::setOpenMPNumThreads(cpuThreads);

    qInfo().noquote()
      << QStringLiteral("Worker threads: %1 CPUs · Qt %2 · OpenMS/OpenMP %3%4")
           .arg(cpuThreads)
           .arg(QThreadPool::globalInstance()->maxThreadCount())
           .arg(OpenMS::Internal::OpenMSBuildInfo::getOpenMPMaxNumThreads())
           .arg(qEnvironmentVariableIsSet("OMP_NUM_THREADS")
                  ? QStringLiteral(" (OMP_NUM_THREADS override)") : QString());
  }
}

int main(int argc, char* argv[])
{
  applyWslPlatformDefaults();

  QApplication application(argc, argv);
  applyBundledDotNetDefault();
  QApplication::setApplicationName(QStringLiteral("OpenMS Viewer"));
  QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
  QApplication::setOrganizationName(QStringLiteral("OpenMS"));
  QApplication::setStyle(QStringLiteral("Fusion"));
  configureWorkerThreads();

  QCommandLineParser parser;
  parser.setApplicationDescription(
    QStringLiteral("Standalone OpenMS mass-spectrometry data viewer"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument(QStringLiteral("file"),
                               QStringLiteral("mzML, FeatureXML, and idXML files to open at startup"),
                               QStringLiteral("[files...]"));
  parser.process(application);

  OpenMSViewer::MainWindow window;
  window.show();

  const QStringList files = parser.positionalArguments();
  QStringList absoluteFiles;
  for (const QString& file : files) absoluteFiles.push_back(QFileInfo(file).absoluteFilePath());
  if (!absoluteFiles.isEmpty()) window.loadFiles(absoluteFiles);
  return application.exec();
}
