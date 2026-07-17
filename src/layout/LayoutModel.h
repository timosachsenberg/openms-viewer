#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace OpenMSViewer
{
  /// Identity of a panel: stable across sessions, since it keys both the
  /// serialized layout and the panel's settings. Matches the widget objectName.
  using PanelId = QString;

  /// The viewer's panel arrangement: an ordered vertical stack of rows, each
  /// holding one or two panels. See CONTEXT.md for the vocabulary and
  /// docs/adr/0001-row-stack-layout-instead-of-qdockwidget.md for why this
  /// exists instead of QMainWindow docking.
  ///
  /// Deliberately UI-free: no QWidget, no pixels. This owns the structure and
  /// the invariant; RowStackWidget renders it and owns geometry. That split is
  /// what makes "no row has three panels" a headless unit test.
  class LayoutModel final
  {
  public:
    /// A row never holds more than this many panels. Every mutator either
    /// preserves this or refuses; there is no path to a wider row.
    static constexpr int MaxPanelsPerRow = 2;

    struct Row
    {
      /// 1..MaxPanelsPerRow entries, left to right. Never empty: a row exists
      /// only because panels are in it.
      std::vector<PanelId> panels;
      bool operator==(const Row&) const = default;
    };

    /// Where a dragged panel would land, expressed relative to an *anchor
    /// panel* rather than a row index. Anchors survive the dragged panel's
    /// removal from its old row; row indices shift and would silently retarget.
    enum class DropKind
    {
      NewRowAbove,   ///< new full-width row directly above the anchor's row
      NewRowBelow,   ///< new full-width row directly below the anchor's row
      LeftOfAnchor,  ///< join the anchor's row, immediately left of the anchor
      RightOfAnchor  ///< join the anchor's row, immediately right of the anchor
    };

    struct DropTarget
    {
      DropKind kind{DropKind::NewRowBelow};
      PanelId anchor;
      bool operator==(const DropTarget&) const = default;
    };

    struct Position
    {
      int row{0};
      int column{0};
      bool operator==(const Position&) const = default;
    };

    [[nodiscard]] const std::vector<Row>& rows() const { return rows_; }
    [[nodiscard]] int rowCount() const { return static_cast<int>(rows_.size()); }
    [[nodiscard]] bool isEmpty() const { return rows_.empty(); }
    [[nodiscard]] bool contains(const PanelId& panel) const;
    [[nodiscard]] std::optional<Position> locate(const PanelId& panel) const;
    /// Every panel in the layout, top to bottom then left to right.
    [[nodiscard]] std::vector<PanelId> panels() const;

    /// Add `panel` as a new full-width row at the bottom. No-op if already present.
    void appendRow(const PanelId& panel);
    /// Insert `panel` as a new full-width row at `index` (clamped to range).
    /// No-op if already present.
    void insertRow(int index, const PanelId& panel);
    /// Remove `panel` and normalize: a row left with one panel becomes
    /// full-width, a row left with none ceases to exist. No-op if absent.
    void remove(const PanelId& panel);
    void clear();

    /// True when `panel` may legally land on `target`. A row already holding
    /// MaxPanelsPerRow panels offers no side target — unless `panel` is itself
    /// in that row, where a side drop merely reorders the two. `panel` need not
    /// currently be in the layout: dropping a hidden panel inserts it.
    ///
    /// Callers use this to decide whether to *offer* a drop indicator at all,
    /// so an illegal arrangement is never presented rather than refused.
    [[nodiscard]] bool canDrop(const PanelId& panel, const DropTarget& target) const;
    /// Every target `panel` could legally land on. For tests and keyboard moves.
    [[nodiscard]] std::vector<DropTarget> dropTargets(const PanelId& panel) const;
    /// Apply a drop. Returns false and leaves the layout untouched unless
    /// canDrop(panel, target).
    bool applyDrop(const PanelId& panel, const DropTarget& target);

    /// Structure only — no sizes. Sizes are view state and belong to the renderer.
    [[nodiscard]] QJsonObject toJson() const;
    /// Parse, rejecting everything the model cannot represent: unknown panel
    /// ids, duplicates, empty rows, or a row over MaxPanelsPerRow. Returns
    /// nullopt rather than repairing, so a caller falls back to the default
    /// layout instead of restoring a state the invariant forbids.
    [[nodiscard]] static std::optional<LayoutModel> fromJson(
      const QJsonObject& json, const std::vector<PanelId>& known);

    /// Every invariant this model promises: no empty rows, no row wider than
    /// MaxPanelsPerRow, no panel in two places. Cheap — assert on it freely.
    [[nodiscard]] bool invariantHolds() const;

    bool operator==(const LayoutModel&) const = default;

  private:
    std::vector<Row> rows_;
  };
}
