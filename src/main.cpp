#include "MainWindow.h"

#include <OpenMS/SYSTEM/BuildInfo.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QThread>
#include <QThreadPool>

#include <algorithm>

#ifdef OPENMS_VIEWER_PORTABLE
#include <OpenMS/CONCEPT/VersionInfo.h>
#include <OpenMS/SYSTEM/File.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace
{
  // WSLg defaults to the Wayland platform, where Qt draws the cursor client-side
  // with no size hint, giving a giant pointer. Preferring XWayland (xcb) and
  // setting a cursor size sidesteps it. Both are only defaults: an explicit
  // QT_QPA_PLATFORM or XCURSOR_SIZE still wins.
  // (Panel dragging no longer depends on the platform at all — panels never
  // float, so nothing needs the compositor to move a top-level surface.)
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

#ifdef OPENMS_VIEWER_PORTABLE
  // Absolute path of the libOpenMS image actually mapped into this process,
  // identified from the address of an OpenMS symbol. Empty if it cannot be
  // determined. Used to detect a second OpenMS install shadowing the bundle.
  QString loadedOpenMSLibraryPath()
  {
    auto* const symbol = reinterpret_cast<const void*>(&OpenMS::VersionInfo::getVersion);
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                             | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(symbol), &module)
        && module != nullptr)
    {
      wchar_t buffer[MAX_PATH];
      const DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH);
      if (length > 0 && length < MAX_PATH) return QString::fromWCharArray(buffer, int(length));
    }
    return {};
#else
    Dl_info info{};
    if (dladdr(symbol, &info) != 0 && info.dli_fname != nullptr)
      return QString::fromUtf8(info.dli_fname);
    return {};
#endif
  }
#endif  // OPENMS_VIEWER_PORTABLE

  // In a portable package the bundled share/OpenMS data tree and libOpenMS are
  // version-locked to each other. Force OpenMS onto OUR data (never an inherited
  // OPENMS_DATA_PATH or a system /usr/local/share/OpenMS), then verify that both
  // the resolved data path and the loaded library live inside the extracted
  // folder. Returns a human-readable error (fatal) or an empty string on success.
  // A no-op in developer builds, where normal OpenMS resolution applies.
  QString configureBundledOpenMSRuntime()
  {
#ifdef OPENMS_VIEWER_PORTABLE
    const QDir applicationDirectory(QApplication::applicationDirPath());
    const QString canonicalRoot = QFileInfo(
      QDir::cleanPath(applicationDirectory.filePath(QStringLiteral("..")))).canonicalFilePath();
    const QString bundledShare =
      QDir::cleanPath(applicationDirectory.filePath(QStringLiteral("../share/OpenMS")));
    const bool useExternal =
      qEnvironmentVariableIntValue("OPENMS_VIEWER_USE_EXTERNAL_DATA") == 1;

    // A canonical path is the bundle root or lives inside it. The trailing
    // separator stops a sibling like "<root>-other" from matching; reusing an
    // existing separator keeps a filesystem-root bundle ("/", "C:/") working.
    const auto withinBundle = [&canonicalRoot](const QString& canonicalPath)
    {
      if (canonicalRoot.isEmpty() || canonicalPath.isEmpty()) return false;
      if (canonicalPath == canonicalRoot) return true;
      const QString prefix = canonicalRoot.endsWith(QLatin1Char('/'))
                               ? canonicalRoot : canonicalRoot + QLatin1Char('/');
      return canonicalPath.startsWith(prefix);
    };

    // Confirm the libOpenMS mapped into this process is our bundled one BEFORE
    // calling into it: a mismatched cross-minor ABI could crash on the first
    // call. Fail closed if the location cannot be positively confirmed (an
    // undeterminable or out-of-bundle path is treated as a hijack).
    const QString library = loadedOpenMSLibraryPath();
    const QString canonicalLibrary =
      library.isEmpty() ? QString() : QFileInfo(library).canonicalFilePath();
    if (!withinBundle(canonicalLibrary))
      return QApplication::translate("main",
        "Could not confirm the bundled OpenMS library is the one loaded (loaded from\n%1"
        ").\nThis portable build requires a library under\n%2")
        .arg(QDir::toNativeSeparators(canonicalLibrary.isEmpty()
               ? QStringLiteral("an undeterminable location") : canonicalLibrary),
             QDir::toNativeSeparators(canonicalRoot.isEmpty()
               ? QStringLiteral("(unresolved)") : canonicalRoot));

    if (useExternal)
    {
      // Deliberate expert opt-out: require an explicit external tree AND confirm
      // OpenMS actually resolved to it — an invalid path would otherwise fall
      // through to the compiled-in data silently.
      if (qEnvironmentVariableIsEmpty("OPENMS_DATA_PATH"))
        return QApplication::translate("main",
          "OPENMS_VIEWER_USE_EXTERNAL_DATA is set but OPENMS_DATA_PATH is empty. Point "
          "OPENMS_DATA_PATH at a matching OpenMS share directory, or unset the override.");
      const QString requested =
        QFileInfo(QString::fromLocal8Bit(qgetenv("OPENMS_DATA_PATH"))).canonicalFilePath();
      const QString resolved = QFileInfo(
        QString::fromStdString(OpenMS::File::getOpenMSDataPath())).canonicalFilePath();
      if (resolved.isEmpty() || resolved != requested)
        return QApplication::translate("main",
          "OPENMS_DATA_PATH points at an external tree that OpenMS did not accept; it "
          "resolved to\n%1\ninstead.")
          .arg(QDir::toNativeSeparators(resolved.isEmpty()
                 ? QStringLiteral("(unresolved)") : resolved));
      return {};
    }

    // Bundled mode: the data tree must exist, must resolve inside the package (a
    // share symlink to another install must not be accepted), and must win over
    // any inherited OPENMS_DATA_PATH and the compiled-in fallback.
    if (!QFileInfo::exists(QDir(bundledShare).filePath(QStringLiteral("CHEMISTRY/unimod.xml"))))
      return QApplication::translate("main",
        "This portable package is incomplete: its bundled OpenMS data directory was not "
        "found at\n%1").arg(QDir::toNativeSeparators(bundledShare));
    const QString expected = QFileInfo(bundledShare).canonicalFilePath();
    if (!withinBundle(expected))
      return QApplication::translate("main",
        "The bundled OpenMS data directory resolves outside the package to\n%1")
        .arg(QDir::toNativeSeparators(expected));
    qputenv("OPENMS_DATA_PATH", QDir::toNativeSeparators(bundledShare).toUtf8());

    // Resolve now (OpenMS caches the result in a function-static) and confirm the
    // bundle won — the unimod.xml probe above is only a marker.
    const QString resolved = QFileInfo(
      QString::fromStdString(OpenMS::File::getOpenMSDataPath())).canonicalFilePath();
    if (resolved.isEmpty() || resolved != expected)
      return QApplication::translate("main",
        "OpenMS resolved its shared-data directory to\n%1\nbut this portable build "
        "requires its own bundled data at\n%2")
        .arg(QDir::toNativeSeparators(resolved.isEmpty()
               ? QStringLiteral("(unresolved)") : resolved),
             QDir::toNativeSeparators(expected));
#endif  // OPENMS_VIEWER_PORTABLE
    return {};
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

  // Portable builds must run against their own bundled OpenMS data + library.
  // Enforce this before any OpenMS data access (and before --version parsing,
  // which would otherwise let a misconfigured package report success).
  if (const QString runtimeError = configureBundledOpenMSRuntime(); !runtimeError.isEmpty())
  {
    // stderr for headless/CI diagnosis (Windows hides it, hence also the dialog);
    // skip the modal dialog under headless platforms so a misconfigured package
    // fails fast instead of blocking on a button nobody can click.
    qCritical().noquote() << QStringLiteral("OpenMS Viewer cannot start: %1").arg(runtimeError);
    const QString platform = QGuiApplication::platformName();
    // Suppress the modal on headless platforms and whenever GUI message boxes are
    // disabled (Windows CI/service runs set QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES),
    // so a misconfigured package fails fast instead of blocking on an unclickable dialog.
    const bool headless = platform == QLatin1String("offscreen")
                          || platform == QLatin1String("minimal")
                          || qEnvironmentVariableIsSet("QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES");
    if (!headless)
      QMessageBox::critical(nullptr, QApplication::translate("main", "OpenMS Viewer"),
                            QApplication::translate("main", "Cannot start OpenMS Viewer.\n\n%1")
                              .arg(runtimeError));
    return 1;
  }

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
