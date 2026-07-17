#pragma once

#include "layout/LayoutModel.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QWidget>

class QAction;
class QScrollArea;
class QSplitter;

namespace OpenMSViewer
{
  class RowStackWidget;
  class PanelFrame;

  /// A panel registered with a RowStackWidget: its identity, title, widget, and
  /// show/hide action. Exposes the slice of the old QDockWidget API the main
  /// window actually used, so panel handling stays declarative there.
  ///
  /// A PanelHandle is a QObject, not a QWidget — it names a panel, it is not the
  /// panel. Its objectName is the panel id, so findChild<PanelHandle*>("tic")
  /// works in tests.
  class PanelHandle final : public QObject
  {
    Q_OBJECT
    friend class RowStackWidget;

  public:
    [[nodiscard]] PanelId id() const { return id_; }
    [[nodiscard]] QWidget* widget() const { return widget_; }
    [[nodiscard]] QString title() const { return title_; }
    void setTitle(const QString& title);

    /// Checkable show/hide action, named after QDockWidget::toggleViewAction()
    /// it replaces. Its *enabled* state means "this panel's data exists"; its
    /// *checked* state means "it is in the layout".
    [[nodiscard]] QAction* toggleViewAction() const { return toggleAction_; }

    /// True when the panel is in the layout (as opposed to hidden). A panel is
    /// in the layout or hidden — with no floating and no tabs, there is no
    /// third state.
    [[nodiscard]] bool isShown() const;
    /// Show (as a new bottom row, scrolled into view) or hide the panel.
    void setShown(bool shown);

    /// Scroll the panel into view. With no tabs there is nothing to raise
    /// above, so this is "make sure the user can see it" — the honest
    /// replacement for QDockWidget::raise().
    void raise();

  signals:
    void shownChanged(bool shown);

  private:
    explicit PanelHandle(RowStackWidget* stack, PanelId id, QString title, QWidget* widget);

    RowStackWidget* stack_{nullptr};
    PanelId id_;
    QString title_;
    QWidget* widget_{nullptr};
    QAction* toggleAction_{nullptr};
    PanelFrame* frame_{nullptr};
  };

  /// Renders a LayoutModel as nested splitters and turns drags into model
  /// operations.
  ///
  /// Structure: an outer vertical QSplitter of rows; a two-panel row is an
  /// inner horizontal QSplitter. Splitter handles give free user resizing —
  /// that is what splitters are. The whole stack sits in a QScrollArea so the
  /// layout scrolls when the rows' minimum heights exceed the viewport rather
  /// than compressing plots past legibility.
  ///
  /// This widget owns pixels only. The structure and its invariant live in
  /// LayoutModel; sizes are remembered per row signature and are view state.
  class RowStackWidget final : public QWidget
  {
    Q_OBJECT
    friend class PanelHandle;
    friend class PanelFrame;

  public:
    explicit RowStackWidget(QWidget* parent = nullptr);
    ~RowStackWidget() override;

    /// Register a panel. Takes ownership of `widget` by reparenting it. The
    /// panel starts hidden; call PanelHandle::setShown(true) or
    /// setLayoutModel() to place it.
    PanelHandle* addPanel(const PanelId& id, const QString& title, QWidget* widget);

    [[nodiscard]] PanelHandle* panel(const PanelId& id) const;
    /// Every registered panel, in registration order.
    [[nodiscard]] QList<PanelHandle*> panels() const;
    /// Ids of every registered panel — the "known" set for LayoutModel::fromJson.
    [[nodiscard]] std::vector<PanelId> knownPanels() const;

    [[nodiscard]] const LayoutModel& model() const { return model_; }
    /// Replace the arrangement wholesale. Panels in `model` are shown in its
    /// order; every other registered panel is hidden. Ignores unknown ids.
    void setLayoutModel(const LayoutModel& model);

    /// Structure (from LayoutModel) plus remembered splitter sizes.
    [[nodiscard]] QJsonObject saveState() const;
    /// Restore a state written by saveState(). Returns false and changes
    /// nothing when the structure is unreadable or violates the invariant, so
    /// the caller can fall back to its default layout.
    bool restoreState(const QJsonObject& state);

  signals:
    /// Emitted whenever the arrangement changes — drag, show, hide, or preset.
    void layoutChanged();

  private:
    // Drag lifecycle, driven by PanelFrame's header.
    void beginDrag(PanelFrame* frame, const QPoint& globalPos);
    void updateDrag(const QPoint& globalPos);
    void endDrag();
    void cancelDrag();
    [[nodiscard]] std::optional<LayoutModel::DropTarget> targetAt(const QPoint& globalPos) const;
    /// Screen rect the drop would occupy, for the indicator overlay.
    [[nodiscard]] QRect targetRect(const LayoutModel::DropTarget& target) const;

    void rebuild();
    void rememberSizes();
    void applyRemembered(QSplitter* splitter, const QString& key);
    void scrollIntoView(const PanelId& id);
    void setShown(const PanelId& id, bool shown);
    [[nodiscard]] bool isShown(const PanelId& id) const;
    void syncToggleActions();

    LayoutModel model_;
    QList<PanelHandle*> order_;
    QHash<PanelId, PanelHandle*> byId_;
    QScrollArea* scroll_{nullptr};
    QSplitter* rowsSplitter_{nullptr};
    QWidget* indicator_{nullptr};
    PanelFrame* dragFrame_{nullptr};
    QPoint dragPressGlobal_;
    bool dragging_{false};
    bool rebuilding_{false};
    /// Splitter sizes keyed by row signature ("tic|spectrum") plus a "rows" key
    /// for the outer splitter. Survives unrelated panels opening and closing.
    QHash<QString, QList<int>> rememberedSizes_;
  };
}
