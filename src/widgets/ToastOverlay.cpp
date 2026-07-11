#include "widgets/ToastOverlay.h"

#include <QAccessible>
#include <QColor>
#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace OpenMSViewer
{
  namespace
  {
    // At most this many toasts are shown at once; older ones are evicted so the
    // stack never grows without bound during a burst of notifications.
    constexpr int kMaxVisibleToasts = 4;

    struct ToastStyle
    {
      QColor accent;
      QString glyph;
    };

    ToastStyle styleFor(ToastLevel level)
    {
      switch (level)
      {
        case ToastLevel::Success: return {QColor(39, 174, 96), QStringLiteral("✓")};
        case ToastLevel::Warning: return {QColor(243, 156, 18), QStringLiteral("⚠")};
        case ToastLevel::Error:   return {QColor(231, 76, 60), QStringLiteral("✕")};
        case ToastLevel::Info:    break;
      }
      return {QColor(52, 152, 219), QStringLiteral("ℹ")};
    }
  }

  ToastOverlay::ToastOverlay(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("toastOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAccessibleName(tr("Notifications"));
    stack_ = new QVBoxLayout(this);
    stack_->setContentsMargins(0, 0, 0, 0);
    stack_->setSpacing(8);
    if (parent) parent->installEventFilter(this);
    hide();
  }

  void ToastOverlay::showToast(const QString& message, ToastLevel level, int durationMs)
  {
    if (message.isEmpty()) return;
    const ToastStyle style = styleFor(level);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("toastCard"));
    card->setAttribute(Qt::WA_TransparentForMouseEvents);
    card->setMaximumWidth(360);
    card->setStyleSheet(QStringLiteral(
      "#toastCard{background:palette(base); border:1px solid palette(mid);"
      " border-left:4px solid %1; border-radius:6px;}").arg(style.accent.name()));

    auto* layout = new QHBoxLayout(card);
    layout->setContentsMargins(12, 8, 14, 8);
    layout->setSpacing(9);
    auto* icon = new QLabel(style.glyph, card);
    icon->setStyleSheet(QStringLiteral("color:%1; font-weight:bold;").arg(style.accent.name()));
    icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    auto* text = new QLabel(message, card);
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::NoTextInteraction);
    layout->addWidget(icon, 0, Qt::AlignTop);
    layout->addWidget(text, 1);

    auto* effect = new QGraphicsOpacityEffect(card);
    effect->setOpacity(0.0);
    card->setGraphicsEffect(effect);
    stack_->addWidget(card);

    // Bound the TOTAL number of cards in the layout — including any still mid
    // fade-out — so a burst (or four toasts expiring just as four more arrive)
    // can never overflow the corner. The oldest card is item 0; hard-remove it
    // (no fade), cutting any in-flight fade short to make room. removeWidget is
    // synchronous, so the loop always terminates.
    while (stack_->count() > kMaxVisibleToasts)
    {
      QLayoutItem* item = stack_->itemAt(0);
      QWidget* oldest = item ? item->widget() : nullptr;
      if (!oldest) break;
      oldest->setProperty("dismissing", true);
      // hide() first: removeWidget alone leaves the card painted at its old
      // geometry until the deferred deleteLater runs (never, within a burst).
      oldest->hide();
      stack_->removeWidget(oldest);
      oldest->deleteLater();
    }

    auto* fadeIn = new QPropertyAnimation(effect, "opacity", card);
    fadeIn->setDuration(150);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);

    QTimer::singleShot(std::max(500, durationMs), card, [this, card] { dismiss(card); });

    // Announce warnings/errors to assistive technology — the toast itself is a
    // transient, mouse-transparent, unfocusable label a screen reader would miss.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (level == ToastLevel::Warning || level == ToastLevel::Error)
    {
      QAccessibleAnnouncementEvent announcement(this, message);
      announcement.setPoliteness(level == ToastLevel::Error
        ? QAccessible::AnnouncementPoliteness::Assertive
        : QAccessible::AnnouncementPoliteness::Polite);
      QAccessible::updateAccessibility(&announcement);
    }
#endif

    adjustSize();
    reposition();
    show();
    raise();
  }

  void ToastOverlay::clearToasts()
  {
    while (QLayoutItem* item = stack_->takeAt(0))
    {
      if (QWidget* widget = item->widget()) widget->deleteLater();
      delete item;
    }
    hide();
  }

  int ToastOverlay::activeToastCount() const
  {
    int count = 0;
    for (int index = 0; index < stack_->count(); ++index)
    {
      const QLayoutItem* item = stack_->itemAt(index);
      if (item && item->widget() && !item->widget()->property("dismissing").toBool()) ++count;
    }
    return count;
  }

  void ToastOverlay::dismiss(QWidget* card)
  {
    if (!card || card->property("dismissing").toBool()) return;
    card->setProperty("dismissing", true);

    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect());
    const auto finalize = [this, card]
    {
      stack_->removeWidget(card);
      card->deleteLater();
      adjustSize();
      reposition();
      // Keep the overlay up while other cards are still fading out; hide only
      // once the layout is truly empty.
      if (stack_->count() == 0) hide();
    };
    if (!effect)
    {
      finalize();
      return;
    }
    auto* fadeOut = new QPropertyAnimation(effect, "opacity", card);
    fadeOut->setDuration(200);
    fadeOut->setStartValue(effect->opacity());
    fadeOut->setEndValue(0.0);
    connect(fadeOut, &QPropertyAnimation::finished, this, finalize);
    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
  }

  bool ToastOverlay::eventFilter(QObject* watched, QEvent* event)
  {
    if (watched == parentWidget()
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
      reposition();
    return QWidget::eventFilter(watched, event);
  }

  void ToastOverlay::reposition()
  {
    if (!parentWidget()) return;
    adjustSize();
    constexpr int margin = 16;
    move(std::max(margin, parentWidget()->width() - width() - margin),
         std::max(margin, parentWidget()->height() - height() - margin));
  }
}
