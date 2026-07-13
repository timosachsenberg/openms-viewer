#include "widgets/DataLayersWidget.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace OpenMSViewer
{
  DataLayersWidget::DataLayersWidget(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("dataLayersPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    empty_ = new QLabel(tr("No data loaded"), this);
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(empty_, 1);

    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("dataLayersTable"));
    table_->setAccessibleName(tr("Loaded data and overlay layers"));
    table_->setToolTip(
      tr("Hide a layer without unloading it, or remove it from the session"));
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({tr("Layer"), tr("Source"), tr("Status"), tr("Actions")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    layout->addWidget(table_, 1);
    rebuild();
  }

  void DataLayersWidget::setLayer(Layer layer, const QString& sourcePath, const QString& summary,
                                  bool available, bool visible, bool modified)
  {
    State& state = states_[static_cast<std::size_t>(layer)];
    const State next{sourcePath, summary, available, visible, modified};
    if (state.sourcePath == next.sourcePath && state.summary == next.summary
        && state.available == next.available && state.visible == next.visible
        && state.modified == next.modified)
      return;
    state = next;
    rebuild();
  }

  void DataLayersWidget::clear()
  {
    states_ = {};
    rebuild();
  }

  QString DataLayersWidget::layerName(Layer layer)
  {
    switch (layer)
    {
      case Layer::Primary:         return tr("Primary data");
      case Layer::Features:        return tr("Features");
      case Layer::Identifications: return tr("Identifications");
      case Layer::Consensus:       return tr("Consensus map");
      case Layer::OpenSwath:       return tr("OpenSWATH results");
      case Layer::Count:           break;
    }
    return {};
  }

  void DataLayersWidget::rebuild()
  {
    table_->setRowCount(0);
    for (std::size_t index = 0; index < states_.size(); ++index)
    {
      const State& state = states_[index];
      if (!state.available) continue;
      const Layer layer = static_cast<Layer>(index);
      const int row = table_->rowCount();
      table_->insertRow(row);

      auto* name = new QTableWidgetItem(layerName(layer));
      if (state.modified)
      {
        QFont font = name->font();
        font.setBold(true);
        name->setFont(font);
      }
      table_->setItem(row, 0, name);

      const QString displaySource = state.sourcePath.isEmpty()
        ? tr("Not saved yet") : QFileInfo(state.sourcePath).fileName();
      auto* source = new QTableWidgetItem(displaySource);
      source->setToolTip(state.sourcePath);
      table_->setItem(row, 1, source);

      QString status = state.summary;
      if (state.modified) status += status.isEmpty() ? tr("Modified") : tr(" · Modified");
      table_->setItem(row, 2, new QTableWidgetItem(status));

      auto* actions = new QWidget(table_);
      auto* actionsLayout = new QHBoxLayout(actions);
      actionsLayout->setContentsMargins(2, 0, 2, 0);
      actionsLayout->setSpacing(4);
      if (layer != Layer::Primary)
      {
        auto* visible = new QToolButton(actions);
        visible->setObjectName(QStringLiteral("layerVisibility_%1").arg(index));
        visible->setAutoRaise(true);
        visible->setCheckable(true);
        visible->setChecked(state.visible);
        visible->setIcon(QIcon(state.visible
          ? QStringLiteral(":/icons/material-visibility.svg")
          : QStringLiteral(":/icons/material-visibility-off.svg")));
        visible->setToolTip(tr("Show or hide this layer without unloading it"));
        visible->setAccessibleName(visible->toolTip());
        connect(visible, &QToolButton::toggled, this,
                [this, layer, visible](bool checked)
                {
                  visible->setIcon(QIcon(checked
                    ? QStringLiteral(":/icons/material-visibility.svg")
                    : QStringLiteral(":/icons/material-visibility-off.svg")));
                  emit visibilityChanged(layer, checked);
                });
        actionsLayout->addWidget(visible);
      }
      auto* remove = new QToolButton(actions);
      remove->setObjectName(QStringLiteral("layerRemove_%1").arg(index));
      remove->setAutoRaise(true);
      remove->setIcon(QIcon(QStringLiteral(":/icons/material-delete-outline.svg")));
      remove->setToolTip(layer == Layer::Primary ? tr("Close this data session")
                                                 : tr("Remove this layer from the session"));
      remove->setAccessibleName(remove->toolTip());
      connect(remove, &QToolButton::clicked, this,
              [this, layer] { emit removeRequested(layer); });
      actionsLayout->addWidget(remove);
      table_->setCellWidget(row, 3, actions);
    }
    const bool available = table_->rowCount() > 0;
    table_->setVisible(available);
    empty_->setVisible(!available);
  }
}
