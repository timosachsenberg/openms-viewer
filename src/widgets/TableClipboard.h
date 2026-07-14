#pragma once

class QTableView;

namespace OpenMSViewer
{
  /**
   * @brief Make @p view copy its current selection to the clipboard as TSV.
   *
   * Installs a widget-scoped Ctrl+C (QKeySequence::Copy) shortcut. The copied
   * text has one line per selected row and tab-separated cells, ordered by row
   * then visual column, skipping hidden columns — the layout a spreadsheet
   * expects. No-op when @p view is null or nothing is selected.
   */
  void enableTsvClipboardCopy(QTableView* view);
}
