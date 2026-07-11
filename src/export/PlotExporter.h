#pragma once

#include <QString>

class QWidget;

namespace OpenMSViewer
{
  class PlotExporter
  {
  public:
    [[nodiscard]] static QString writePng(QWidget& widget, const QString& path);
  };
}
