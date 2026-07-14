#include "widgets/TableClipboard.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QMap>
#include <QModelIndex>
#include <QModelIndexList>
#include <QShortcut>
#include <QString>
#include <QStringList>
#include <QTableView>

namespace OpenMSViewer
{
  namespace
  {
    QString selectionAsTsv(const QTableView* view)
    {
      const QItemSelectionModel* selection = view->selectionModel();
      if (!selection || !view->model()) return {};
      const QModelIndexList indexes = selection->selectedIndexes();
      if (indexes.isEmpty()) return {};

      const QHeaderView* header = view->horizontalHeader();
      // Ordered logical-row -> (visual-column -> text); QMap keeps both sorted so
      // the output follows on-screen layout regardless of selection order.
      QMap<int, QMap<int, QString>> grid;
      for (const QModelIndex& index : indexes)
      {
        if (view->isColumnHidden(index.column())) continue;
        const int visualColumn = header ? header->visualIndex(index.column()) : index.column();
        grid[index.row()][visualColumn] = index.data(Qt::DisplayRole).toString();
      }

      QStringList lines;
      lines.reserve(grid.size());
      for (auto rowIt = grid.cbegin(); rowIt != grid.cend(); ++rowIt)
      {
        QStringList cells;
        cells.reserve(rowIt.value().size());
        for (auto colIt = rowIt.value().cbegin(); colIt != rowIt.value().cend(); ++colIt)
          cells << colIt.value();
        lines << cells.join(QLatin1Char('\t'));
      }
      return lines.join(QLatin1Char('\n'));
    }
  }

  void enableTsvClipboardCopy(QTableView* view)
  {
    if (!view) return;
    auto* shortcut = new QShortcut(QKeySequence::Copy, view);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(shortcut, &QShortcut::activated, view, [view]
    {
      const QString tsv = selectionAsTsv(view);
      if (!tsv.isEmpty()) QApplication::clipboard()->setText(tsv);
    });
  }
}
