#include "widgets/RowStackWidget.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>
#include <utility>

namespace OpenMSViewer
{
  namespace
  {
    // Bands of a frame that offer a drop, leaving its middle neutral. The row
    // bands are shallower than the side bands: a mis-aimed side drop only
    // reorders a row, while a mis-aimed row drop restructures the stack.
    constexpr double RowBandFraction = 0.25;
    constexpr double SideBandFraction = 0.30;
    constexpr int IndicatorFillAlpha = 90;
    constexpr int IndicatorBorderAlpha = 200;

    QRect globalRect(const QWidget* widget)
    {
      return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
    }
  }

  class PanelFrame;

  /// The drag surface of a panel. A widget of its own rather than a band of
  /// PanelFrame: the press must be accepted *here*, so Qt's implicit grab
  /// delivers the rest of the drag to us wherever the cursor goes, and so the
  /// panel body below keeps the mouse events its plots need.
  ///
  /// Plain child-widget mouse handling: no grab, no window move, no QDrag —
  /// nothing a compositor has to cooperate with. See ADR 0001.
  class PanelHeader final : public QWidget
  {
  public:
    explicit PanelHeader(PanelFrame* frame);

  protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

  private:
    PanelFrame* frame_{nullptr};
  };

  /// Chrome around one panel: a slim header over the panel's own widget.
  ///
  /// Frames outlive rebuilds — a rebuild reparents them into the new tree — so a
  /// panel's widget, and the state in it, is never rebuilt with the splitters.
  ///
  /// The drag lifecycle is RowStackWidget's private business and PanelFrame is
  /// not a friend of it, so the header reports through signals and the stack
  /// connects them to itself.
  class PanelFrame final : public QWidget
  {
    Q_OBJECT

  public:
    PanelFrame(PanelHandle* handle, QWidget* parent);

    [[nodiscard]] PanelId id() const { return handle_->id(); }
    void setTitle(const QString& title);

    void headerPressed(const QPoint& globalPos);
    void headerMoved(const QPoint& globalPos);
    void headerReleased(const QPoint& globalPos);

  signals:
    void dragBegan(PanelFrame* frame, const QPoint& globalPos);
    void dragMoved(const QPoint& globalPos);
    void dragEnded();
    void dragCancelled();

  protected:
    /// Watches the whole application while the header is pressed: Esc must
    /// cancel the drag wherever the keyboard focus happens to sit, and the
    /// header never holds it.
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void stopWatching();

    PanelHandle* handle_{nullptr};
    PanelHeader* header_{nullptr};
    QLabel* titleLabel_{nullptr};
    QToolButton* closeButton_{nullptr};
    bool pressed_{false};
  };

  namespace
  {
    QString signatureOf(const QSplitter* rowSplitter)
    {
      QStringList ids;
      for (int i = 0; i < rowSplitter->count(); ++i)
        if (auto* frame = qobject_cast<PanelFrame*>(rowSplitter->widget(i))) ids << frame->id();
      return ids.join(QLatin1Char('|'));
    }

    // Row heights belong to a particular stack of rows, so they are keyed by one:
    // "rows:peakMap|dataLayers/tic/spectrum". A single unkeyed slot cannot survive
    // the row count changing, and it changes constantly — a panel leaves the
    // layout whenever its data is absent and returns when it loads, which would
    // otherwise discard the user's heights on every launch.
    QString rowsKey(const QSplitter* rows)
    {
      QStringList signatures;
      for (int i = 0; i < rows->count(); ++i)
      {
        QWidget* rowWidget = rows->widget(i);
        if (auto* inner = qobject_cast<QSplitter*>(rowWidget)) signatures << signatureOf(inner);
        else if (auto* frame = qobject_cast<PanelFrame*>(rowWidget)) signatures << frame->id();
      }
      return QStringLiteral("rows:") + signatures.join(QLatin1Char('/'));
    }
  }

  PanelHeader::PanelHeader(PanelFrame* frame) : QWidget(frame), frame_(frame)
  {
    setAutoFillBackground(true);
    QPalette headerPalette = palette();
    headerPalette.setColor(QPalette::Window, headerPalette.color(QPalette::Midlight));
    setPalette(headerPalette);
    setCursor(Qt::OpenHandCursor);
  }

  void PanelHeader::mousePressEvent(QMouseEvent* event)
  {
    if (event->button() != Qt::LeftButton)
    {
      QWidget::mousePressEvent(event);
      return;
    }
    event->accept();
    setCursor(Qt::ClosedHandCursor);
    frame_->headerPressed(event->globalPosition().toPoint());
  }

  void PanelHeader::mouseMoveEvent(QMouseEvent* event)
  {
    if (!(event->buttons() & Qt::LeftButton))
    {
      QWidget::mouseMoveEvent(event);
      return;
    }
    event->accept();
    frame_->headerMoved(event->globalPosition().toPoint());
  }

  void PanelHeader::mouseReleaseEvent(QMouseEvent* event)
  {
    if (event->button() != Qt::LeftButton)
    {
      QWidget::mouseReleaseEvent(event);
      return;
    }
    event->accept();
    setCursor(Qt::OpenHandCursor);
    frame_->headerReleased(event->globalPosition().toPoint());
  }

  PanelFrame::PanelFrame(PanelHandle* handle, QWidget* parent) : QWidget(parent), handle_(handle)
  {
    setObjectName(handle->id() + QStringLiteral("Frame"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = new PanelHeader(this);
    header_->setObjectName(handle->id() + QStringLiteral("Header"));
    auto* headerLayout = new QHBoxLayout(header_);
    headerLayout->setContentsMargins(8, 1, 1, 1);
    headerLayout->setSpacing(4);

    titleLabel_ = new QLabel(header_);
    // The whole bar is the drag handle; the label must not swallow the press.
    titleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont titleFont = titleLabel_->font();
    titleFont.setBold(true);
    titleLabel_->setFont(titleFont);

    closeButton_ = new QToolButton(header_);
    closeButton_->setObjectName(handle->id() + QStringLiteral("CloseButton"));
    closeButton_->setAutoRaise(true);
    closeButton_->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    closeButton_->setIconSize(QSize(12, 12));
    connect(closeButton_, &QToolButton::clicked, this, [handle] { handle->setShown(false); });

    headerLayout->addWidget(titleLabel_, 1);
    headerLayout->addWidget(closeButton_);

    layout->addWidget(header_);
    if (QWidget* body = handle->widget()) layout->addWidget(body, 1);
    setTitle(handle->title());
  }

  void PanelFrame::setTitle(const QString& title)
  {
    titleLabel_->setText(title);
    header_->setAccessibleName(tr("Drag to move %1").arg(title));
    closeButton_->setToolTip(tr("Hide %1").arg(title));
    closeButton_->setAccessibleName(tr("Hide %1").arg(title));
  }

  void PanelFrame::headerPressed(const QPoint& globalPos)
  {
    pressed_ = true;
    qApp->installEventFilter(this);
    emit dragBegan(this, globalPos);
  }

  void PanelFrame::headerMoved(const QPoint& globalPos)
  {
    if (!pressed_) return;
    emit dragMoved(globalPos);
  }

  void PanelFrame::headerReleased(const QPoint& globalPos)
  {
    if (!pressed_) return;
    stopWatching();
    // A release carries a position of its own, and the drop resolves where the
    // mouse was let go rather than where it last moved.
    emit dragMoved(globalPos);
    emit dragEnded();
  }

  bool PanelFrame::eventFilter(QObject* watched, QEvent* event)
  {
    if (pressed_ && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
    {
      stopWatching();
      emit dragCancelled();
      return true;
    }
    return QWidget::eventFilter(watched, event);
  }

  void PanelFrame::stopWatching()
  {
    pressed_ = false;
    qApp->removeEventFilter(this);
  }

  PanelHandle::PanelHandle(RowStackWidget* stack, PanelId id, QString title, QWidget* widget)
    : QObject(stack), stack_(stack), id_(std::move(id)), title_(std::move(title)), widget_(widget)
  {
    setObjectName(id_);
    toggleAction_ = new QAction(title_, this);
    toggleAction_->setObjectName(id_ + QStringLiteral("Toggle"));
    toggleAction_->setCheckable(true);
    connect(toggleAction_, &QAction::toggled, this, [this](bool checked) { setShown(checked); });
  }

  void PanelHandle::setTitle(const QString& title)
  {
    if (title_ == title) return;
    title_ = title;
    toggleAction_->setText(title_);
    if (frame_) frame_->setTitle(title_);
  }

  bool PanelHandle::isShown() const { return stack_->isShown(id_); }

  void PanelHandle::setShown(bool shown) { stack_->setShown(id_, shown); }

  void PanelHandle::raise() { stack_->scrollIntoView(id_); }

  RowStackWidget::RowStackWidget(QWidget* parent) : QWidget(parent)
  {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("rowStackScrollArea"));
    scroll_->setFrameShape(QFrame::NoFrame);
    // The rows keep their minimum heights and the stack scrolls past them rather
    // than compressing plots past legibility.
    scroll_->setWidgetResizable(true);
    layout->addWidget(scroll_);

    indicator_ = new QWidget(this);
    indicator_->setObjectName(QStringLiteral("rowStackDropIndicator"));
    indicator_->setAttribute(Qt::WA_TransparentForMouseEvents);
    indicator_->setAttribute(Qt::WA_StyledBackground);
    indicator_->hide();

    rebuild();
  }

  RowStackWidget::~RowStackWidget() = default;

  PanelHandle* RowStackWidget::addPanel(const PanelId& id, const QString& title, QWidget* widget)
  {
    if (PanelHandle* existing = panel(id)) return existing;

    auto* handle = new PanelHandle(this, id, title, widget);
    handle->frame_ = new PanelFrame(handle, this);
    // Registered but unplaced. Without an explicit hide the frame would ride
    // along the next show() of this widget as a stray child of the stack.
    handle->frame_->hide();
    order_.append(handle);
    byId_.insert(id, handle);

    // Lambdas because the drag lifecycle is private to this class and PanelFrame
    // is not a friend of it.
    connect(handle->frame_, &PanelFrame::dragBegan, this,
            [this](PanelFrame* frame, const QPoint& globalPos) { beginDrag(frame, globalPos); });
    connect(handle->frame_, &PanelFrame::dragMoved, this,
            [this](const QPoint& globalPos) { updateDrag(globalPos); });
    connect(handle->frame_, &PanelFrame::dragEnded, this, [this] { endDrag(); });
    connect(handle->frame_, &PanelFrame::dragCancelled, this, [this] { cancelDrag(); });
    return handle;
  }

  PanelHandle* RowStackWidget::panel(const PanelId& id) const { return byId_.value(id, nullptr); }

  QList<PanelHandle*> RowStackWidget::panels() const { return order_; }

  std::vector<PanelId> RowStackWidget::knownPanels() const
  {
    std::vector<PanelId> ids;
    ids.reserve(static_cast<std::size_t>(order_.size()));
    for (const PanelHandle* handle : order_) ids.push_back(handle->id());
    return ids;
  }

  void RowStackWidget::setLayoutModel(const LayoutModel& model)
  {
    // Rebuilt through the model's own API rather than copied: an id we never
    // registered has no widget to render, and canDrop stays the only thing that
    // decides what a row will hold.
    LayoutModel next;
    for (const LayoutModel::Row& row : model.rows())
    {
      PanelId anchor;
      for (const PanelId& id : row.panels)
      {
        if (!byId_.contains(id)) continue;
        if (anchor.isEmpty())
          next.appendRow(id);
        else
          next.applyDrop(id, {LayoutModel::DropKind::RightOfAnchor, anchor});
        anchor = id;
      }
    }

    const std::vector<PanelId> before = model_.panels();
    model_ = next;
    rebuild();

    for (PanelHandle* handle : std::as_const(order_))
    {
      const bool shown = model_.contains(handle->id());
      if (shown != (std::find(before.begin(), before.end(), handle->id()) != before.end()))
        emit handle->shownChanged(shown);
    }
    emit layoutChanged();
  }

  QJsonObject RowStackWidget::saveState() const
  {
    QJsonObject sizes;
    for (auto it = rememberedSizes_.constBegin(); it != rememberedSizes_.constEnd(); ++it)
    {
      QJsonArray values;
      for (int size : it.value()) values.append(size);
      sizes.insert(it.key(), values);
    }

    QJsonObject state;
    state.insert(QStringLiteral("layout"), model_.toJson());
    state.insert(QStringLiteral("sizes"), sizes);
    return state;
  }

  bool RowStackWidget::restoreState(const QJsonObject& state)
  {
    const std::optional<LayoutModel> restored =
      LayoutModel::fromJson(state.value(QStringLiteral("layout")).toObject(), knownPanels());
    if (!restored) return false;

    QHash<QString, QList<int>> sizes;
    const QJsonObject sizesJson = state.value(QStringLiteral("sizes")).toObject();
    for (auto it = sizesJson.constBegin(); it != sizesJson.constEnd(); ++it)
    {
      QList<int> values;
      bool usable = true;
      const QJsonArray array = it.value().toArray();
      for (const QJsonValue& value : array)
      {
        if (!value.isDouble())
        {
          usable = false;
          break;
        }
        values.append(std::max(0, value.toInt()));
      }
      // Sizes are view state: a broken entry costs default proportions, never
      // the restore.
      if (usable && !values.isEmpty()) sizes.insert(it.key(), values);
    }
    rememberedSizes_ = sizes;

    setLayoutModel(*restored);
    return true;
  }

  void RowStackWidget::beginDrag(PanelFrame* frame, const QPoint& globalPos)
  {
    dragFrame_ = frame;
    dragPressGlobal_ = globalPos;
    dragging_ = false;

    const QColor highlight = palette().color(QPalette::Highlight);
    indicator_->setStyleSheet(
      QStringLiteral("background-color: rgba(%1, %2, %3, %4);"
                     "border: 1px solid rgba(%1, %2, %3, %5);")
        .arg(highlight.red())
        .arg(highlight.green())
        .arg(highlight.blue())
        .arg(IndicatorFillAlpha)
        .arg(IndicatorBorderAlpha));
  }

  void RowStackWidget::updateDrag(const QPoint& globalPos)
  {
    if (!dragFrame_) return;
    if (!dragging_
        && (globalPos - dragPressGlobal_).manhattanLength() < QApplication::startDragDistance())
      return;
    dragging_ = true;
    // Past the threshold dragPressGlobal_ follows the cursor: endDrag() takes no
    // position of its own and must resolve the drop where the mouse last was.
    dragPressGlobal_ = globalPos;

    const std::optional<LayoutModel::DropTarget> target = targetAt(globalPos);
    const QRect rect = target ? targetRect(*target) : QRect();
    if (rect.isEmpty())
    {
      indicator_->hide();
      return;
    }
    indicator_->setGeometry(QRect(mapFromGlobal(rect.topLeft()), rect.size()));
    indicator_->raise();
    indicator_->show();
  }

  void RowStackWidget::endDrag()
  {
    PanelFrame* frame = dragFrame_;
    const std::optional<LayoutModel::DropTarget> target =
      dragging_ ? targetAt(dragPressGlobal_) : std::nullopt;
    cancelDrag();

    if (!frame || !target) return;
    if (!model_.applyDrop(frame->id(), *target)) return;
    rebuild();
    emit layoutChanged();
  }

  void RowStackWidget::cancelDrag()
  {
    dragFrame_ = nullptr;
    dragging_ = false;
    indicator_->hide();
  }

  std::optional<LayoutModel::DropTarget> RowStackWidget::targetAt(const QPoint& globalPos) const
  {
    if (!dragFrame_ || !scroll_) return std::nullopt;
    const QRect viewport = globalRect(scroll_->viewport());

    for (const PanelHandle* handle : order_)
    {
      // Only the frames in the layout: a hidden panel's frame is parked outside
      // the tree, where it keeps a stale geometry that would still hit-test.
      const PanelFrame* frame = handle->frame_;
      if (!frame || !model_.contains(handle->id())) continue;
      const QRect rect = globalRect(frame);
      // A row scrolled past the viewport is not on screen, so it is not a
      // target, however much of the cursor's screen its geometry covers.
      if (rect.isEmpty() || !(rect & viewport).contains(globalPos)) continue;

      const double x = globalPos.x() - rect.left();
      const double y = globalPos.y() - rect.top();
      std::optional<LayoutModel::DropTarget> target;
      // Corners resolve to the row bands: splitting the stack is the coarser
      // gesture and deserves the more forgiving aim.
      if (y < rect.height() * RowBandFraction)
        target = {LayoutModel::DropKind::NewRowAbove, handle->id()};
      else if (y >= rect.height() * (1.0 - RowBandFraction))
        target = {LayoutModel::DropKind::NewRowBelow, handle->id()};
      else if (x < rect.width() * SideBandFraction)
        target = {LayoutModel::DropKind::LeftOfAnchor, handle->id()};
      else if (x >= rect.width() * (1.0 - SideBandFraction))
        target = {LayoutModel::DropKind::RightOfAnchor, handle->id()};

      // A drop the model would refuse is never offered: the affordance teaches
      // the invariant instead of the refusal.
      if (!target || !model_.canDrop(dragFrame_->id(), *target)) return std::nullopt;
      return target;
    }
    return std::nullopt;
  }

  QRect RowStackWidget::targetRect(const LayoutModel::DropTarget& target) const
  {
    const PanelHandle* handle = panel(target.anchor);
    if (!handle || !handle->frame_ || !rowsSplitter_) return {};
    const QRect frameRect = globalRect(handle->frame_);
    if (frameRect.isEmpty()) return {};

    switch (target.kind)
    {
      case LayoutModel::DropKind::NewRowAbove:
      case LayoutModel::DropKind::NewRowBelow:
      {
        // A new row spans the full width whatever the anchor's share of its own
        // row, and the indicator promises exactly that.
        const QWidget* rowWidget = handle->frame_;
        while (rowWidget && rowWidget->parentWidget() != rowsSplitter_)
          rowWidget = rowWidget->parentWidget();
        if (!rowWidget) return {};

        const QRect rowRect = globalRect(rowWidget);
        const int band = std::max(2, static_cast<int>(rowRect.height() * RowBandFraction));
        return target.kind == LayoutModel::DropKind::NewRowAbove
                 ? QRect(rowRect.left(), rowRect.top(), rowRect.width(), band)
                 : QRect(rowRect.left(), rowRect.bottom() + 1 - band, rowRect.width(), band);
      }
      case LayoutModel::DropKind::LeftOfAnchor:
        return QRect(frameRect.topLeft(), QSize(frameRect.width() / 2, frameRect.height()));
      case LayoutModel::DropKind::RightOfAnchor:
        return QRect(QPoint(frameRect.left() + frameRect.width() / 2, frameRect.top()),
                     QSize(frameRect.width() - frameRect.width() / 2, frameRect.height()));
    }
    return {};
  }

  void RowStackWidget::rebuild()
  {
    if (rebuilding_) return;
    rebuilding_ = true;

    // Deliberately does NOT remember sizes here. Only splitterMoved does — a
    // divider the user dragged is the only thing that expresses an arrangement.
    // A rebuild's sizes are whatever Qt last distributed, and capturing those
    // would overwrite the real ones: rebuilds run freely (a panel's data
    // arriving or leaving triggers one) on trees that may never have been laid
    // out or shown.

    // Frames outlive the tree that shows them: detaching every one before the
    // old splitters die is what keeps a rebuild from destroying a panel's widget
    // or the state in it.
    for (PanelHandle* handle : std::as_const(order_))
    {
      handle->frame_->setParent(this);
      handle->frame_->hide();
    }

    // The old tree can be torn down from inside its own event handling — a close
    // button click arrives here — so retire it rather than delete it outright.
    // It must be hidden on the way out: a retired tree is still a child of this
    // widget until the event loop reaps it, and a visible one paints over the
    // tree that replaced it.
    if (QWidget* old = scroll_->takeWidget())
    {
      old->setParent(this);
      old->hide();
      old->deleteLater();
    }

    rowsSplitter_ = new QSplitter(Qt::Vertical);
    rowsSplitter_->setObjectName(QStringLiteral("rowStackRows"));
    // A row has a minimum height for a reason; a divider must not collapse it.
    rowsSplitter_->setChildrenCollapsible(false);
    connect(rowsSplitter_, &QSplitter::splitterMoved, this, [this] { rememberSizes(); });

    for (const LayoutModel::Row& row : model_.rows())
    {
      QList<PanelFrame*> frames;
      for (const PanelId& id : row.panels)
        if (const PanelHandle* handle = panel(id); handle && handle->frame_)
          frames.append(handle->frame_);
      if (frames.isEmpty()) continue;

      if (frames.size() == 1)
      {
        rowsSplitter_->addWidget(frames.front());
        continue;
      }

      auto* rowSplitter = new QSplitter(Qt::Horizontal);
      rowSplitter->setChildrenCollapsible(false);
      connect(rowSplitter, &QSplitter::splitterMoved, this, [this] { rememberSizes(); });
      for (PanelFrame* frame : std::as_const(frames)) rowSplitter->addWidget(frame);
      rowsSplitter_->addWidget(rowSplitter);
      rowSplitter->setObjectName(signatureOf(rowSplitter) + QStringLiteral("Row"));
    }

    scroll_->setWidget(rowsSplitter_);

    // Detaching hid the frames explicitly, so the splitters leave them hidden.
    for (const PanelId& id : model_.panels())
      if (const PanelHandle* handle = panel(id); handle && handle->frame_) handle->frame_->show();

    // setSizes() scales what it is given to the splitter's *current* size, and a
    // splitter the scroll area has only just been handed has not been laid out
    // yet — it is nearly zero tall, so remembered heights would collapse to each
    // row's minimum. Sizing it to the viewport first also gives the row splitters
    // the widths their own sizes scale against.
    rowsSplitter_->resize(scroll_->viewport()->size());
    applyRemembered(rowsSplitter_, rowsKey(rowsSplitter_));
    for (int i = 0; i < rowsSplitter_->count(); ++i)
      if (auto* rowSplitter = qobject_cast<QSplitter*>(rowsSplitter_->widget(i)))
        applyRemembered(rowSplitter, signatureOf(rowSplitter));

    syncToggleActions();
    rebuilding_ = false;
  }

  void RowStackWidget::rememberSizes()
  {
    // A tree with no rows has no sizes worth keeping, and recording its empty
    // list would clobber what restoreState just loaded: rebuild() remembers
    // before it tears the old tree down, and the first rebuild after a restore
    // is still holding the empty tree the constructor built.
    if (!rowsSplitter_ || rowsSplitter_->count() == 0) return;
    // A splitter that has not been laid out yet reports all-zero sizes. Those are
    // not the user's arrangement, and storing them would later collapse every row
    // to its minimum — rebuilds happen freely before the tree has any geometry.
    const auto usable = [](const QSplitter* splitter)
    {
      const QList<int> sizes = splitter->sizes();
      return std::accumulate(sizes.begin(), sizes.end(), 0) > 0;
    };
    if (usable(rowsSplitter_))
      rememberedSizes_.insert(rowsKey(rowsSplitter_), rowsSplitter_->sizes());
    // Keyed off the tree rather than the model: rebuild() remembers after the
    // model has already changed, and these sizes belong to the rows on screen.
    for (int i = 0; i < rowsSplitter_->count(); ++i)
      if (const auto* rowSplitter = qobject_cast<QSplitter*>(rowsSplitter_->widget(i));
          rowSplitter && usable(rowSplitter))
        rememberedSizes_.insert(signatureOf(rowSplitter), rowSplitter->sizes());
  }

  void RowStackWidget::applyRemembered(QSplitter* splitter, const QString& key)
  {
    if (!splitter) return;
    const auto remembered = rememberedSizes_.constFind(key);
    // Sizes from a different shape would land on the wrong widgets, and Qt's own
    // defaults beat that.
    if (remembered == rememberedSizes_.constEnd() || remembered->size() != splitter->count()) return;
    splitter->setSizes(*remembered);
  }

  void RowStackWidget::scrollIntoView(const PanelId& id)
  {
    const PanelHandle* handle = panel(id);
    if (!handle || !handle->frame_ || !scroll_) return;
    const QWidget* root = scroll_->widget();
    if (!root || !root->isAncestorOf(handle->frame_)) return;
    scroll_->ensureWidgetVisible(handle->frame_);
  }

  void RowStackWidget::setShown(const PanelId& id, bool shown)
  {
    PanelHandle* handle = panel(id);
    if (!handle) return;
    if (model_.contains(id) == shown)
    {
      if (shown) scrollIntoView(id);
      return;
    }

    if (shown)
      model_.appendRow(id);
    else
      model_.remove(id);
    rebuild();
    if (shown) scrollIntoView(id);

    emit handle->shownChanged(shown);
    emit layoutChanged();
  }

  bool RowStackWidget::isShown(const PanelId& id) const { return model_.contains(id); }

  void RowStackWidget::syncToggleActions()
  {
    for (PanelHandle* handle : std::as_const(order_))
    {
      QAction* action = handle->toggleViewAction();
      const bool shown = model_.contains(handle->id());
      if (action->isChecked() == shown) continue;
      // The action's own toggle is what asked for this in the first place; it
      // must not be told to ask again.
      const QSignalBlocker blocker(action);
      action->setChecked(shown);
    }
  }
}

#include "RowStackWidget.moc"
