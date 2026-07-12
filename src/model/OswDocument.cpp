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

    // Chromatograms live in a sibling file with the same basename (the .osw is
    // scores/metadata only). Prefer the self-describing .xic Parquet; fall back to a
    // classic .sqMass (the common OpenSWATH output), which needs the store for its
    // precursor→transition mapping + library annotation. Absence is not an error.
    const QFileInfo info(result.sourcePath);
    const QString base = info.completeBaseName();
    const QString xicPath = info.dir().filePath(base + QStringLiteral(".xic"));
    if (QFileInfo(xicPath).isFile())
    {
      QString xicError;
      result.chromatograms = XicChromatogramSource::open({xicPath}, xicError);
      if (!result.chromatograms) result.chromatogramNote = xicError;
    }

    if (!result.chromatograms)
    {
      // OpenSWATH writes the chromatogram sqMass as either "<run>.sqMass" or
      // "<run>.chrom.sqMass" next to the results.
      for (const QString& suffix : {QStringLiteral(".sqMass"), QStringLiteral(".chrom.sqMass")})
      {
        const QString sqMassPath = info.dir().filePath(base + suffix);
        if (!QFileInfo(sqMassPath).isFile()) continue;
        QString sqMassError;
        result.chromatograms = SqMassChromatogramSource::open(sqMassPath, result.store, sqMassError);
        if (result.chromatograms) { result.chromatogramNote.clear(); break; }
        result.chromatogramNote = sqMassError;
      }
    }

    if (!result.chromatograms && result.chromatogramNote.isEmpty())
      result.chromatogramNote =
        QStringLiteral("No matching .xic or .sqMass chromatograms found next to the .osw.");
    return result;
  }
}
