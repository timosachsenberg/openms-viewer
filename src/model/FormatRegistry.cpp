#include "model/FormatRegistry.h"

#include <OpenMS/FORMAT/FileHandler.h>

#include <QFileInfo>

#include <algorithm>

namespace OpenMSViewer::FormatRegistry
{
  using Type = OpenMS::FileTypes::Type;

  std::vector<Type> allowedTypes(Category category)
  {
    switch (category)
    {
      // Conservative, audited allowlists — expand deliberately, never auto-derive
      // from FileProperties (which are inconsistent across OpenMS formats).
      case Category::Experiment:
        return {Type::MZML, Type::MZXML, Type::MZDATA, Type::SQMASS};
      case Category::Features:
        return {Type::FEATUREXML, Type::FEATUREPARQUET};
      case Category::Identifications:
        return {Type::IDXML, Type::MZIDENTML, Type::IDPARQUET};
      case Category::Consensus:
        return {Type::CONSENSUSXML, Type::CONSENSUSPARQUET};
      case Category::Osw:
        return {Type::OSW};
      case Category::ChromatogramSource:
        return {Type::CHROMPARQUET};
      case Category::Imaging:
        return {Type::IMZML};
      case Category::Unknown:
        break;
    }
    return {};
  }

  namespace
  {
    Category categoryForType(Type type)
    {
      for (const Category category : {Category::Experiment, Category::Features,
                                      Category::Identifications, Category::Consensus,
                                      Category::Osw, Category::ChromatogramSource,
                                      Category::Imaging})
      {
        const auto types = allowedTypes(category);
        if (std::find(types.begin(), types.end(), type) != types.end()) return category;
      }
      return Category::Unknown;
    }
  }

  FormatInfo detect(const QString& path)
  {
    FormatInfo info;
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists()) return info;
    const std::string absolute = fileInfo.absoluteFilePath().toStdString();
    const bool isDir = fileInfo.isDir();
    const bool isFile = fileInfo.isFile();
    info.shape = isDir ? Shape::Directory : Shape::File;

    // Only content-probe REGULAR files — opening a FIFO/socket could block the
    // worker. Directories and other special objects are classified by name only.
    try
    {
      info.type = isFile ? OpenMS::FileHandler::getType(absolute)
                         : OpenMS::FileHandler::getTypeByFileName(absolute);
    }
    catch (...)
    {
      info.type = OpenMS::FileTypes::UNKNOWN;
    }
    if (info.type == OpenMS::FileTypes::UNKNOWN)
    {
      try { info.type = OpenMS::FileHandler::getTypeByFileName(absolute); }
      catch (...) { info.type = OpenMS::FileTypes::UNKNOWN; }
    }

    info.category = categoryForType(info.type);
    // A directory-shaped OpenMS type must be a directory on disk; every other type
    // must be a regular file. This rejects shape mismatches (a plain file named
    // foo.featureparquet) and non-regular objects (a FIFO named input.idXML).
    const bool shapeOk = OpenMS::FileTypes::isDirectoryType(info.type) ? isDir : isFile;
    info.supported = info.category != Category::Unknown && shapeOk;
    return info;
  }

  QString categoryLabel(Category category)
  {
    switch (category)
    {
      case Category::Experiment:         return QStringLiteral("spectra");
      case Category::Features:           return QStringLiteral("feature map");
      case Category::Identifications:    return QStringLiteral("identifications");
      case Category::Consensus:          return QStringLiteral("consensus map");
      case Category::Osw:                return QStringLiteral("OpenSWATH results");
      case Category::ChromatogramSource: return QStringLiteral("chromatograms");
      case Category::Imaging:            return QStringLiteral("imaging data");
      case Category::Unknown:            break;
    }
    return QStringLiteral("data");
  }
}
