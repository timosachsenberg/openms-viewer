#include "export/PlotExporter.h"

#include <QApplication>
#include <QFileInfo>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyle>
#include <QSvgGenerator>
#include <QWidget>

namespace OpenMSViewer
{
  namespace
  {
    // Exported / printed plots always render on the light theme (white canvas,
    // dark axes) regardless of the on-screen theme, so figures read the same
    // everywhere. RAII: force the style's standard (light) palette on the widget
    // for the duration of the render, then restore its previous inheritance so a
    // live theme switch afterwards still reaches it (never leave it frozen).
    class LightPaletteScope
    {
    public:
      explicit LightPaletteScope(QWidget& widget)
        : widget_(widget),
          hadExplicitPalette_(widget.testAttribute(Qt::WA_SetPalette)),
          savedPalette_(widget.palette())
      {
        widget_.setPalette(QApplication::style()->standardPalette());
      }

      ~LightPaletteScope()
      {
        if (hadExplicitPalette_)
        {
          widget_.setPalette(savedPalette_);
        }
        else
        {
          // A default-constructed palette has an empty resolve mask, so setPalette()
          // itself clears WA_SetPalette and restores full inheritance from the app
          // palette — no manual attribute reset needed (and resetting it could clobber
          // a palette a widget reapplies synchronously in its PaletteChange handler).
          widget_.setPalette(QPalette());
        }
      }

      LightPaletteScope(const LightPaletteScope&) = delete;
      LightPaletteScope& operator=(const LightPaletteScope&) = delete;

    private:
      QWidget& widget_;
      bool hadExplicitPalette_;
      QPalette savedPalette_;
    };
  }

  QString PlotExporter::writePng(QWidget& widget, const QString& path)
  {
    if (path.isEmpty()) return QStringLiteral("No output path was selected.");
    if (widget.width() <= 0 || widget.height() <= 0)
      return QStringLiteral("The plot has no drawable size.");
    const LightPaletteScope lightScope(widget);
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
    const LightPaletteScope lightScope(widget);
    // Re-run the widget's own paintEvent into the SVG paint device (vector output).
    widget.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();
    return {};
  }
}
