#pragma once

#include <OpenMS/FORMAT/FileTypes.h>

#include <QString>

#include <vector>

namespace OpenMSViewer::FormatRegistry
{
  // The semantic kind of data a file/directory carries — this is what the viewer
  // routes on, NOT the file suffix (which is ambiguous) and NOT FileProperties
  // (which are inconsistent across OpenMS formats).
  enum class Category
  {
    Unknown,
    Experiment,          ///< raw spectra/chromatograms → MSExperiment
    Features,            ///< FeatureMap
    Identifications,     ///< protein + peptide identifications
    Consensus,           ///< ConsensusMap (multi-run)
    Osw,                 ///< OpenSWATH results (SQLite)
    ChromatogramSource,  ///< OpenSWATH .xic chromatogram store
    Imaging              ///< imzML (handled by the imaging document)
  };

  enum class Shape
  {
    File,
    Directory            ///< e.g. Bruker .d, *.featureparquet / *.consensusparquet / *.idparquet
  };

  struct FormatInfo
  {
    OpenMS::FileTypes::Type type{OpenMS::FileTypes::UNKNOWN};
    Category category{Category::Unknown};
    Shape shape{Shape::File};
    bool supported{false};  ///< in the viewer's explicit allowlist
  };

  // Detect a path's OpenMS type + viewer category. Directory-shaped bundles are
  // classified by name (their content isn't a single parseable file); plain files
  // use OpenMS content+name detection. Never throws.
  [[nodiscard]] FormatInfo detect(const QString& path);

  // The OpenMS types allowed for a category — passed to FileHandler's allowlist
  // so a mis-detected file is rejected rather than mis-parsed.
  [[nodiscard]] std::vector<OpenMS::FileTypes::Type> allowedTypes(Category category);

  // Short human label for status/error messages.
  [[nodiscard]] QString categoryLabel(Category category);
}
