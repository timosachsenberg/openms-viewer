#include "export/PlotExporter.h"

#include <QFileInfo>
#include <QPixmap>
#include <QWidget>

namespace OpenMSViewer
{
  QString PlotExporter::writePng(QWidget& widget, const QString& path)
  {
    if (path.isEmpty()) return QStringLiteral("No output path was selected.");
    if (widget.width() <= 0 || widget.height() <= 0)
      return QStringLiteral("The plot has no drawable size.");
    const QPixmap image = widget.grab();
    if (image.isNull()) return QStringLiteral("The plot could not be captured.");
    if (!image.save(QFileInfo(path).absoluteFilePath(), "PNG"))
      return QStringLiteral("The PNG file could not be written.");
    return {};
  }
}
