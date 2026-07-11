#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

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
}

int main(int argc, char* argv[])
{
  applyWslPlatformDefaults();

  QApplication application(argc, argv);
  QApplication::setApplicationName(QStringLiteral("OpenMS Viewer"));
  QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
  QApplication::setOrganizationName(QStringLiteral("OpenMS"));
  QApplication::setStyle(QStringLiteral("Fusion"));

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
