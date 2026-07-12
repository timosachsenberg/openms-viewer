#pragma once

#include "model/ChromatogramSource.h"
#include "model/OswStore.h"

#include <QString>

#include <memory>

namespace OpenMSViewer
{
  struct OswLoadResult
  {
    std::shared_ptr<OswStore> store;
    std::shared_ptr<ChromatogramSource> chromatograms;  ///< null if no readable sibling .xic
    QString sourcePath;
    QString chromatogramNote;  ///< why chromatograms are absent, if they are
    QString error;

    [[nodiscard]] bool succeeded() const noexcept { return store != nullptr && error.isEmpty(); }
  };

  // Loads an OpenSWATH .osw results database (read-only) plus, when present, the
  // sibling .xic chromatogram store (same basename) that carries the transition
  // XICs. Runs off the GUI thread; return value is adopted on the GUI thread.
  namespace OswDocument
  {
    [[nodiscard]] OswLoadResult read(const QString& oswPath);
  }
}
