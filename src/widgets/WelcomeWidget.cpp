#include "widgets/WelcomeWidget.h"

#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace OpenMSViewer
{
  WelcomeWidget::WelcomeWidget(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("welcomePage"));
    setAccessibleName(tr("OpenMS Viewer welcome page"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 28, 28, 28);
    outer->addStretch();

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("welcomeCard"));
    card->setFrameShape(QFrame::StyledPanel);
    card->setMaximumWidth(720);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(36, 32, 36, 32);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Explore mass-spectrometry data"), card);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* description = new QLabel(
      tr("Open or drop an mzML run, an imzML imaging dataset, or matching "
         "FeatureXML and idXML overlays."), card);
    description->setWordWrap(true);
    description->setAlignment(Qt::AlignCenter);
    layout->addWidget(description);

    auto* open = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton),
                                 tr("Open data…"), card);
    open->setObjectName(QStringLiteral("welcomeOpenButton"));
    open->setDefault(true);
    open->setMinimumHeight(40);
    open->setAccessibleDescription(tr("Choose mass-spectrometry files to open"));
    layout->addWidget(open, 0, Qt::AlignHCenter);
    connect(open, &QPushButton::clicked, this, &WelcomeWidget::openRequested);

    auto* dropHint = new QLabel(tr("You can also drag files anywhere onto this window"), card);
    dropHint->setAlignment(Qt::AlignCenter);
    dropHint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(dropHint);

    recentLabel_ = new QLabel(tr("Recent files"), card);
    QFont sectionFont = recentLabel_->font();
    sectionFont.setBold(true);
    recentLabel_->setFont(sectionFont);
    layout->addWidget(recentLabel_);

    recentFiles_ = new QListWidget(card);
    recentFiles_->setObjectName(QStringLiteral("recentFiles"));
    recentFiles_->setAlternatingRowColors(true);
    recentFiles_->setMaximumHeight(170);
    recentFiles_->setAccessibleName(tr("Recent files"));
    layout->addWidget(recentFiles_);
    connect(recentFiles_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item)
    {
      if (item) emit recentFileRequested(item->data(Qt::UserRole).toString());
    });

    auto* formats = new QLabel(
      tr("Supported: mzML · imzML/IBD · FeatureXML · idXML"), card);
    formats->setAlignment(Qt::AlignCenter);
    formats->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(formats);

    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(card);
    row->addStretch();
    outer->addLayout(row);
    outer->addStretch();

    setRecentFiles({});
  }

  void WelcomeWidget::setRecentFiles(const QStringList& paths)
  {
    recentFiles_->clear();
    for (const QString& path : paths)
    {
      const QFileInfo info(path);
      auto* item = new QListWidgetItem(info.fileName(), recentFiles_);
      item->setData(Qt::UserRole, info.absoluteFilePath());
      item->setToolTip(info.absoluteFilePath());
      item->setStatusTip(info.absoluteFilePath());
    }
    const bool available = recentFiles_->count() > 0;
    recentLabel_->setVisible(available);
    recentFiles_->setVisible(available);
  }
}
