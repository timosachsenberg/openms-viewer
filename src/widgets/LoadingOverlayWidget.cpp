#include "widgets/LoadingOverlayWidget.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace OpenMSViewer
{
  LoadingOverlayWidget::LoadingOverlayWidget(QWidget* parent) : QFrame(parent)
  {
    setObjectName(QStringLiteral("loadingOverlay"));
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    setMaximumWidth(460);
    setMinimumWidth(360);
    setAccessibleName(tr("Background operation progress"));

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(24.0);
    shadow->setOffset(0.0, 5.0);
    shadow->setColor(QColor(0, 0, 0, 130));
    setGraphicsEffect(shadow);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(8);
    title_ = new QLabel(this);
    QFont font = title_->font();
    font.setBold(true);
    title_->setFont(font);
    detail_ = new QLabel(this);
    detail_->setWordWrap(true);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("loadingProgress"));
    progress_->setTextVisible(true);
    elapsed_ = new QLabel(this);
    elapsed_->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    cancel_ = new QPushButton(tr("Cancel"), this);
    cancel_->setObjectName(QStringLiteral("cancelLoadingButton"));

    layout->addWidget(title_);
    layout->addWidget(detail_);
    layout->addWidget(progress_);
    layout->addWidget(elapsed_);
    layout->addWidget(cancel_, 0, Qt::AlignRight);
    connect(cancel_, &QPushButton::clicked, this, [this]
    {
      cancel_->setEnabled(false);
      detail_->setText(tr("Cancelling…"));
      emit cancelRequested();
    });

    if (parent) parent->installEventFilter(this);
    hide();
  }

  void LoadingOverlayWidget::begin(const QString& title, const QString& detail, bool cancellable)
  {
    title_->setText(title);
    detail_->setText(detail);
    elapsed_->setText(tr("Elapsed 0.0 s"));
    progress_->setRange(0, 0);
    progress_->setValue(0);
    cancel_->setVisible(cancellable);
    cancel_->setEnabled(cancellable);
    adjustSize();
    reposition();
    show();
    raise();
  }

  void LoadingOverlayWidget::setProgress(const QString& phase, int percent)
  {
    detail_->setText(phase);
    progress_->setRange(0, 100);
    progress_->setValue(qBound(0, percent, 100));
  }

  void LoadingOverlayWidget::setIndeterminate(const QString& phase)
  {
    detail_->setText(phase);
    progress_->setRange(0, 0);
  }

  void LoadingOverlayWidget::setElapsed(qint64 milliseconds)
  {
    elapsed_->setText(tr("Elapsed %1 s").arg(milliseconds / 1000.0, 0, 'f', 1));
  }

  void LoadingOverlayWidget::finish() { hide(); }

  bool LoadingOverlayWidget::eventFilter(QObject* watched, QEvent* event)
  {
    if (watched == parentWidget()
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) reposition();
    return QFrame::eventFilter(watched, event);
  }

  void LoadingOverlayWidget::reposition()
  {
    if (!parentWidget()) return;
    adjustSize();
    move(std::max(12, (parentWidget()->width() - width()) / 2), 18);
  }
}
