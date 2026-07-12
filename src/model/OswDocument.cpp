#include "model/OswDocument.h"

#include <QDir>
#include <QFileInfo>

namespace OpenMSViewer::OswDocument
{
  OswLoadResult read(const QString& oswPath)
  {
    OswLoadResult result;
    result.sourcePath = QFileInfo(oswPath).absoluteFilePath();
    result.store = OswStore::open(result.sourcePath, result.error);
    if (!result.store) return result;

    // Chromatograms live in a sibling .xic with the same basename (the .osw is
    // scores/metadata only). Its absence is not an error — the tables still work.
    const QFileInfo info(result.sourcePath);
    const QString xicPath = info.dir().filePath(info.completeBaseName() + QStringLiteral(".xic"));
    if (QFileInfo(xicPath).isFile())
    {
      QString xicError;
      result.chromatograms = XicChromatogramSource::open({xicPath}, xicError);
      if (!result.chromatograms)
        result.chromatogramNote = xicError;
    }
    else
    {
      result.chromatogramNote =
        QStringLiteral("No matching .xic chromatograms found next to the .osw.");
    }
    return result;
  }
}
