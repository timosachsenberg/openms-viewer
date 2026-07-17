#include "layout/LayoutModel.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <utility>

namespace OpenMSViewer
{
  namespace
  {
    constexpr int JsonVersion = 1;

    constexpr LayoutModel::DropKind AllDropKinds[] = {
      LayoutModel::DropKind::NewRowAbove, LayoutModel::DropKind::NewRowBelow,
      LayoutModel::DropKind::LeftOfAnchor, LayoutModel::DropKind::RightOfAnchor};

    bool holds(const std::vector<PanelId>& panels, const PanelId& panel)
    {
      return std::find(panels.begin(), panels.end(), panel) != panels.end();
    }
  }

  bool LayoutModel::contains(const PanelId& panel) const { return locate(panel).has_value(); }

  std::optional<LayoutModel::Position> LayoutModel::locate(const PanelId& panel) const
  {
    for (std::size_t row = 0; row < rows_.size(); ++row)
    {
      const std::vector<PanelId>& panels = rows_[row].panels;
      const auto found = std::find(panels.begin(), panels.end(), panel);
      if (found != panels.end())
        return Position{static_cast<int>(row),
                        static_cast<int>(std::distance(panels.begin(), found))};
    }
    return std::nullopt;
  }

  std::vector<PanelId> LayoutModel::panels() const
  {
    std::vector<PanelId> result;
    for (const Row& row : rows_) result.insert(result.end(), row.panels.begin(), row.panels.end());
    return result;
  }

  void LayoutModel::appendRow(const PanelId& panel) { insertRow(rowCount(), panel); }

  void LayoutModel::insertRow(int index, const PanelId& panel)
  {
    if (contains(panel)) return;
    const int clamped = std::clamp(index, 0, rowCount());
    rows_.insert(rows_.begin() + clamped, Row{{panel}});
  }

  void LayoutModel::remove(const PanelId& panel)
  {
    const std::optional<Position> position = locate(panel);
    if (!position) return;
    std::vector<PanelId>& panels = rows_[static_cast<std::size_t>(position->row)].panels;
    panels.erase(panels.begin() + position->column);
    // A row exists only for the panels in it; the survivor of a two-panel row
    // simply becomes full-width, which needs no further bookkeeping here.
    if (panels.empty()) rows_.erase(rows_.begin() + position->row);
  }

  void LayoutModel::clear() { rows_.clear(); }

  bool LayoutModel::canDrop(const PanelId& panel, const DropTarget& target) const
  {
    if (target.anchor == panel) return false;
    const std::optional<Position> anchor = locate(target.anchor);
    if (!anchor) return false;

    switch (target.kind)
    {
      case DropKind::NewRowAbove:
      case DropKind::NewRowBelow:
        return true;
      case DropKind::LeftOfAnchor:
      case DropKind::RightOfAnchor:
      {
        const std::vector<PanelId>& panels = rows_[static_cast<std::size_t>(anchor->row)].panels;
        // A full row still takes a panel it already holds: that drop reorders
        // the two rather than widening the row.
        return static_cast<int>(panels.size()) < MaxPanelsPerRow || holds(panels, panel);
      }
    }
    return false;
  }

  std::vector<LayoutModel::DropTarget> LayoutModel::dropTargets(const PanelId& panel) const
  {
    std::vector<DropTarget> targets;
    for (const Row& row : rows_)
      for (const PanelId& anchor : row.panels)
        for (const DropKind kind : AllDropKinds)
        {
          const DropTarget target{kind, anchor};
          if (canDrop(panel, target)) targets.push_back(target);
        }
    return targets;
  }

  bool LayoutModel::applyDrop(const PanelId& panel, const DropTarget& target)
  {
    if (!canDrop(panel, target)) return false;

    remove(panel);
    // The anchor always outlives the removal — it is never `panel` — but its row
    // index can shift, so it must be located after removing, never before.
    const Position anchor = locate(target.anchor).value();

    switch (target.kind)
    {
      case DropKind::NewRowAbove:
        insertRow(anchor.row, panel);
        break;
      case DropKind::NewRowBelow:
        insertRow(anchor.row + 1, panel);
        break;
      case DropKind::LeftOfAnchor:
      case DropKind::RightOfAnchor:
      {
        std::vector<PanelId>& panels = rows_[static_cast<std::size_t>(anchor.row)].panels;
        const int column = target.kind == DropKind::LeftOfAnchor ? anchor.column : anchor.column + 1;
        panels.insert(panels.begin() + column, panel);
        break;
      }
    }
    return true;
  }

  QJsonObject LayoutModel::toJson() const
  {
    QJsonArray rows;
    for (const Row& row : rows_)
    {
      QJsonArray panels;
      for (const PanelId& panel : row.panels) panels.append(panel);
      rows.append(panels);
    }
    return QJsonObject{{QStringLiteral("version"), JsonVersion}, {QStringLiteral("rows"), rows}};
  }

  std::optional<LayoutModel> LayoutModel::fromJson(const QJsonObject& json,
                                                   const std::vector<PanelId>& known)
  {
    if (json.value(QStringLiteral("version")).toInt(-1) != JsonVersion) return std::nullopt;
    const QJsonValue rowsValue = json.value(QStringLiteral("rows"));
    if (!rowsValue.isArray()) return std::nullopt;

    LayoutModel model;
    for (const QJsonValue& rowValue : rowsValue.toArray())
    {
      if (!rowValue.isArray()) return std::nullopt;
      const QJsonArray panelValues = rowValue.toArray();
      if (panelValues.isEmpty() || panelValues.size() > MaxPanelsPerRow) return std::nullopt;

      Row row;
      for (const QJsonValue& panelValue : panelValues)
      {
        if (!panelValue.isString()) return std::nullopt;
        const PanelId panel = panelValue.toString();
        if (!holds(known, panel)) return std::nullopt;
        if (model.contains(panel) || holds(row.panels, panel)) return std::nullopt;
        row.panels.push_back(panel);
      }
      model.rows_.push_back(std::move(row));
    }
    return model;
  }

  bool LayoutModel::invariantHolds() const
  {
    std::vector<PanelId> seen;
    for (const Row& row : rows_)
    {
      if (row.panels.empty() || static_cast<int>(row.panels.size()) > MaxPanelsPerRow) return false;
      for (const PanelId& panel : row.panels)
      {
        if (holds(seen, panel)) return false;
        seen.push_back(panel);
      }
    }
    return true;
  }
}
