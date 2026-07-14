#include "widgets/HelpDialog.h"

#include <QApplication>
#include <QColor>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace OpenMSViewer
{
  namespace
  {
    // A single "colour swatch + label + description" row inside a legend table.
    QString swatchRow(const QColor& color, const QString& name, const QString& desc)
    {
      return QStringLiteral(
               "<tr>"
               "<td width='18' style='padding:2px 8px 2px 0'>"
               "<table cellspacing='0' cellpadding='0'><tr>"
               "<td width='14' height='14' bgcolor='%1' style='border:1px solid #808080'>&nbsp;</td>"
               "</tr></table></td>"
               "<td style='padding:2px 10px 2px 0;white-space:nowrap'><b>%2</b></td>"
               "<td style='padding:2px 0'>%3</td>"
               "</tr>")
        .arg(color.name(), name.toHtmlEscaped(), desc.toHtmlEscaped());
    }

    // A "key / action" row inside a shortcut or mouse table.
    QString keyRow(const QString& keys, const QString& action)
    {
      return QStringLiteral(
               "<tr>"
               "<td style='padding:2px 14px 2px 0;white-space:nowrap'><code>%1</code></td>"
               "<td style='padding:2px 0'>%2</td>"
               "</tr>")
        .arg(keys.toHtmlEscaped(), action.toHtmlEscaped());
    }

    QString cardOpen(const QString& title)
    {
      return QStringLiteral(
               "<table width='100%' cellspacing='0' cellpadding='8' "
               "style='background-color:rgba(128,128,128,0.10);border:1px solid rgba(128,128,128,0.35)'>"
               "<tr><td>"
               "<div style='font-size:13px'><b>%1</b></div>"
               "<div style='height:6px'></div>")
        .arg(title.toHtmlEscaped());
    }

    const char* cardClose = "</td></tr></table><div style='height:12px'></div>";
  }

  QString HelpDialog::buildHtml()
  {
    QString html;
    html += QStringLiteral(
      "<div style='font-size:12px'>"
      "<h2 style='margin:0 0 2px 0'>OpenMS Viewer — Help &amp; Reference</h2>"
      "<div style='color:#808080;margin-bottom:14px'>Interactive mass-spectrometry inspection. "
      "Load files from <b>File → Open</b> or drag &amp; drop them onto the window.</div>");

    // --- Keyboard shortcuts ---
    html += cardOpen(QStringLiteral("Keyboard shortcuts"));
    html += QStringLiteral("<table cellspacing='0'>");
    html += keyRow(QStringLiteral("Ctrl+O"), QStringLiteral("Open file(s)"));
    html += keyRow(QStringLiteral("Ctrl+R"), QStringLiteral("Reload current data"));
    html += keyRow(QStringLiteral("Ctrl+W"), QStringLiteral("Close data"));
    html += keyRow(QStringLiteral("Ctrl+S"), QStringLiteral("Save features"));
    html += keyRow(QStringLiteral("Ctrl+Z / Ctrl+Y"), QStringLiteral("Undo / redo feature edit"));
    html += keyRow(QStringLiteral("+ / − "), QStringLiteral("Zoom in / out"));
    html += keyRow(QStringLiteral("Arrow keys"), QStringLiteral("Pan (← → RT, ↑ ↓ m/z)"));
    html += keyRow(QStringLiteral("G"), QStringLiteral("Go to exact RT / m/z range"));
    html += keyRow(QStringLiteral("Home"), QStringLiteral("Reset to full view"));
    html += keyRow(QStringLiteral("Alt+Left"), QStringLiteral("Previous (zoom-back) view"));
    html += keyRow(QStringLiteral("PageUp / PageDown"), QStringLiteral("Previous / next spectrum"));
    html += keyRow(QStringLiteral("Ctrl+Home / Ctrl+End"), QStringLiteral("First / last spectrum"));
    html += keyRow(QStringLiteral("Delete"), QStringLiteral("Delete selected measurement / label"));
    html += keyRow(QStringLiteral("F11"), QStringLiteral("Toggle fullscreen"));
    html += keyRow(QStringLiteral("F1"), QStringLiteral("This help"));
    html += QStringLiteral("</table>");
    html += QLatin1String(cardClose);

    // --- Mouse & peak-map interaction ---
    html += cardOpen(QStringLiteral("Mouse & plot interaction"));
    html += QStringLiteral("<table cellspacing='0'>");
    html += keyRow(QStringLiteral("Scroll wheel"), QStringLiteral("Zoom in / out at the cursor"));
    html += keyRow(QStringLiteral("Drag"), QStringLiteral("Select a region to zoom into"));
    html += keyRow(QStringLiteral("Ctrl / Alt + drag"), QStringLiteral("Pan (grab & move)"));
    html += keyRow(QStringLiteral("Shift + drag"), QStringLiteral("Measure distance"));
    html += keyRow(QStringLiteral("Double-click"), QStringLiteral("Reset to full view"));
    html += keyRow(QStringLiteral("Click TIC"), QStringLiteral("Jump to the spectrum at that RT"));
    html += keyRow(QStringLiteral("Click table row"), QStringLiteral("Select spectrum / feature / ID"));
    html += QStringLiteral("</table>");
    html += QStringLiteral(
      "<div style='height:6px'></div>"
      "<div style='color:#808080'>Peak-map modes: "
      "<code>Z</code> zoom&nbsp;&nbsp;<code>P</code> pan&nbsp;&nbsp;<code>M</code> measure "
      "(toolbar buttons above the map).</div>");
    html += QLatin1String(cardClose);

    // --- 1D spectrum tools ---
    html += cardOpen(QStringLiteral("1D spectrum tools"));
    html += QStringLiteral("<table cellspacing='0'>");
    html += keyRow(QStringLiteral("Ctrl+M"), QStringLiteral("Measure — click two peaks for Δ m/z"));
    html += keyRow(QStringLiteral("Ctrl+L"), QStringLiteral("Label — click a peak to annotate it"));
    html += keyRow(QStringLiteral("m/z labels"), QStringLiteral("Toggle m/z text on every peak"));
    html += keyRow(QStringLiteral("Auto Y"), QStringLiteral("Auto-scale Y to the visible window"));
    html += keyRow(QStringLiteral("Mirror"), QStringLiteral("Mirror experimental vs. theoretical"));
    html += keyRow(QStringLiteral("3D view"), QStringLiteral("Rotatable peak-surface of a zoomed region"));
    html += QStringLiteral("</table>");
    html += QLatin1String(cardClose);

    // --- Ion annotation colours ---
    html += cardOpen(QStringLiteral("Ion annotation colours"));
    html += QStringLiteral("<table cellspacing='0'>");
    html += swatchRow(QColor(31, 119, 180), QStringLiteral("b-ions"), QStringLiteral("N-terminal fragments"));
    html += swatchRow(QColor(214, 39, 40), QStringLiteral("y-ions"), QStringLiteral("C-terminal fragments"));
    html += swatchRow(QColor(44, 160, 44), QStringLiteral("a-ions"), QStringLiteral("N-terminal (−CO)"));
    html += swatchRow(QColor(148, 103, 189), QStringLiteral("c-ions"), QStringLiteral("N-terminal fragments"));
    html += swatchRow(QColor(140, 86, 75), QStringLiteral("x-ions"), QStringLiteral("C-terminal fragments"));
    html += swatchRow(QColor(227, 119, 194), QStringLiteral("z-ions"), QStringLiteral("C-terminal fragments"));
    html += swatchRow(QColor(255, 127, 14), QStringLiteral("Precursor"), QStringLiteral("Precursor / related ion"));
    html += swatchRow(QColor(145, 145, 155), QStringLiteral("Unmatched"), QStringLiteral("No theoretical match"));
    html += QStringLiteral("</table>");
    html += QLatin1String(cardClose);

    // --- Peak-map overlay colours ---
    html += cardOpen(QStringLiteral("Peak-map overlay colours"));
    html += QStringLiteral("<table cellspacing='0'>");
    html += swatchRow(QColor(0, 255, 100), QStringLiteral("Feature centroid"), QStringLiteral("Detected feature apex"));
    html += swatchRow(QColor(0, 200, 255), QStringLiteral("Feature hull"), QStringLiteral("Convex hull outline"));
    html += swatchRow(QColor(255, 225, 70), QStringLiteral("Feature box"), QStringLiteral("Bounding box"));
    html += swatchRow(QColor(255, 100, 255), QStringLiteral("Selected"), QStringLiteral("Selected overlay item"));
    html += swatchRow(QColor(255, 200, 0), QStringLiteral("Hovered"), QStringLiteral("Item under the cursor"));
    html += swatchRow(QColor(90, 200, 255), QStringLiteral("MS1 marker"), QStringLiteral("Selected MS1 spectrum"));
    html += swatchRow(QColor(255, 140, 40), QStringLiteral("MS2 marker"), QStringLiteral("Selected MS2 spectrum"));
    html += swatchRow(QColor(214, 90, 190), QStringLiteral("Precursor window"), QStringLiteral("Isolation window"));
    html += swatchRow(QColor(255, 150, 50), QStringLiteral("Identification"), QStringLiteral("Peptide ID marker"));
    html += QStringLiteral("</table>");
    html += QLatin1String(cardClose);

    // --- Supported files ---
    html += cardOpen(QStringLiteral("Supported files"));
    html += QStringLiteral(
      "<table cellspacing='0'>"
      "<tr><td style='padding:2px 12px 2px 0'><b>Spectra</b></td>"
      "<td>.mzML, .mzXML, .mzData, .sqMass, Thermo .raw, Bruker .d</td></tr>"
      "<tr><td style='padding:2px 12px 2px 0'><b>Imaging</b></td>"
      "<td>.imzML (+ .ibd)</td></tr>"
      "<tr><td style='padding:2px 12px 2px 0'><b>Features</b></td>"
      "<td>.featureXML, .consensusXML</td></tr>"
      "<tr><td style='padding:2px 12px 2px 0'><b>Identifications</b></td>"
      "<td>.idXML, .mzid / .mzIdentML</td></tr>"
      "<tr><td style='padding:2px 12px 2px 0'><b>OpenSWATH</b></td>"
      "<td>.osw</td></tr>"
      "</table>"
      "<div style='height:6px'></div>"
      "<div style='color:#808080'>Select several related files in one Open dialog, or drag them onto "
      "the window — mzML, features, and identifications load independently and link automatically. "
      "Feature edits are undoable (Ctrl+Z) and you are prompted to save modified features before "
      "they are replaced or closed.</div>");
    html += QLatin1String(cardClose);

    html += QStringLiteral("</div>");
    return html;
  }

  HelpDialog::HelpDialog(QWidget* parent)
    : QDialog(parent)
  {
    setWindowTitle(tr("Help & Reference"));
    setModal(false);
    resize(560, 680);

    browser_ = new QTextBrowser(this);
    browser_->setOpenExternalLinks(true);
    browser_->setHtml(buildHtml());

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::close);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(browser_);
    layout->addWidget(buttons);
  }

  void HelpDialog::showHelp(QWidget* parent)
  {
    // Reuse a single dialog instance per parent so repeated F1 presses raise
    // rather than stack copies.
    HelpDialog* existing = parent ? parent->findChild<HelpDialog*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    if (!existing)
    {
      existing = new HelpDialog(parent);
      existing->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    existing->show();
    existing->raise();
    existing->activateWindow();
  }
}
