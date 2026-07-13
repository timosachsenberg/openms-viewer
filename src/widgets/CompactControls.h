#pragma once

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QSize>
#include <QString>
#include <QToolButton>
#include <QWidget>
#include <QWidgetAction>

namespace OpenMSViewer::CompactControls
{
  inline QWidgetAction* addLabeledMenuControl(QMenu* menu, const QString& labelText,
                                               QWidget* control, int labelWidth = 118,
                                               int controlMinimumWidth = 130)
  {
    auto* row = new QWidget(menu);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(12);
    auto* label = new QLabel(labelText, row);
    label->setMinimumWidth(labelWidth);
    control->setParent(row);
    control->setMinimumWidth(controlMinimumWidth);
    layout->addWidget(label);
    layout->addWidget(control, 1);
    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(row);
    menu->addAction(action);
    return action;
  }

  inline QWidgetAction* addLabeledMenuControl(QMenu* menu, QLabel* label,
                                               QWidget* control, int labelWidth = 118,
                                               int controlMinimumWidth = 130)
  {
    auto* row = new QWidget(menu);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(12);
    label->setParent(row);
    label->setMinimumWidth(labelWidth);
    control->setParent(row);
    control->setMinimumWidth(controlMinimumWidth);
    layout->addWidget(label);
    layout->addWidget(control, 1);
    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(row);
    menu->addAction(action);
    return action;
  }

  inline QWidgetAction* addMenuControl(QMenu* menu, QWidget* control)
  {
    auto* row = new QWidget(menu);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 4, 10, 4);
    control->setParent(row);
    layout->addWidget(control);
    layout->addStretch();
    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(row);
    menu->addAction(action);
    return action;
  }

  inline QToolButton* makeIconButton(QWidget* parent, const QIcon& icon,
                                      const QString& tooltip,
                                      const QString& objectName = {})
  {
    auto* button = new QToolButton(parent);
    if (!objectName.isEmpty()) button->setObjectName(objectName);
    button->setAutoRaise(true);
    button->setIcon(icon);
    button->setIconSize(QSize(20, 20));
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    return button;
  }
}
