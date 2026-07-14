#pragma once

#include <QDialog>

class QTextBrowser;

namespace OpenMSViewer
{
  /**
   * @brief Rich, card-styled "Help & Reference" dialog.
   *
   * Consolidates keyboard shortcuts, mouse/plot interactions, 1D spectrum
   * tools, colour legends (ion annotations and peak-map overlays, with live
   * swatches), and the supported-file list into one scrollable reference —
   * the native counterpart to pyopenms-viewer's Help panel.
   *
   * The colour swatches are generated from the same RGB values the widgets
   * paint with, so the legend stays honest if those constants change.
   */
  class HelpDialog : public QDialog
  {
    Q_OBJECT

  public:
    explicit HelpDialog(QWidget* parent = nullptr);

    /// Convenience: construct (or raise an existing) modeless dialog on @p parent.
    static void showHelp(QWidget* parent);

  private:
    static QString buildHtml();

    QTextBrowser* browser_{nullptr};
  };
}
