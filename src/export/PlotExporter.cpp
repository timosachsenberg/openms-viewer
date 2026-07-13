#include "export/PlotExporter.h"

#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QSvgGenerator>
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

  QString PlotExporter::writeSvg(QWidget& widget, const QString& path)
  {
    if (path.isEmpty()) return QStringLiteral("No output path was selected.");
    if (widget.width() <= 0 || widget.height() <= 0)
      return QStringLiteral("The plot has no drawable size.");

    QSvgGenerator generator;
    generator.setFileName(QFileInfo(path).absoluteFilePath());
    generator.setSize(widget.size());
    generator.setViewBox(QRect(QPoint(0, 0), widget.size()));
    generator.setTitle(widget.windowTitle().isEmpty() ? QStringLiteral("OpenMS Viewer plot")
                                                       : widget.windowTitle());
    generator.setDescription(QStringLiteral("Exported from OpenMS Viewer"));

    QPainter painter;
    if (!painter.begin(&generator))
      return QStringLiteral("The SVG file could not be opened for writing.");
    // Re-run the widget's own paintEvent into the SVG paint device (vector output).
    widget.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();
    return {};
  }
}
