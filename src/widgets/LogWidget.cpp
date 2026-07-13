#include "widgets/LogWidget.h"
#include "widgets/CompactControls.h"

#include "logging/ApplicationLog.h"

#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QScrollBar>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

namespace OpenMSViewer
{
  namespace
  {
    QString severityName(int severity)
    {
      switch (static_cast<QtMsgType>(severity))
      {
        case QtDebugMsg: return QStringLiteral("DEBUG");
        case QtInfoMsg: return QStringLiteral("INFO");
        case QtWarningMsg: return QStringLiteral("WARN");
        case QtCriticalMsg: return QStringLiteral("ERROR");
        case QtFatalMsg: return QStringLiteral("FATAL");
      }
      return QStringLiteral("INFO");
    }
  }

  LogWidget::LogWidget(QWidget* parent) : QWidget(parent)
  {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    auto* controls = new QHBoxLayout;
    severity_ = new QComboBox(this);
    severity_->setObjectName(QStringLiteral("logSeverity"));
    severity_->addItems({tr("All messages"), tr("Info and above"), tr("Warnings and errors"), tr("Errors only")});
    controls->addWidget(severity_);
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("logSearch"));
    search_->setPlaceholderText(tr("Filter log text…"));
    controls->addWidget(search_, 1);
    auto* clear = CompactControls::makeIconButton(
      this, QIcon(QStringLiteral(":/icons/material-clear-all.svg")),
      tr("Clear log"), QStringLiteral("logClear"));
    auto* copy = CompactControls::makeIconButton(
      this, QIcon(QStringLiteral(":/icons/material-content-copy.svg")),
      tr("Copy all log messages"), QStringLiteral("logCopyAll"));
    auto* save = CompactControls::makeIconButton(
      this, QIcon(QStringLiteral(":/icons/material-save.svg")),
      tr("Save log"), QStringLiteral("logSave"));
    controls->addWidget(clear);
    controls->addWidget(copy);
    controls->addWidget(save);
    layout->addLayout(controls);
    output_ = new QPlainTextEdit(this);
    output_->setObjectName(QStringLiteral("applicationLog"));
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(20000);
    output_->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(output_);

    connect(&ApplicationLog::instance(), &ApplicationLog::messageLogged,
            this, &LogWidget::appendMessage);
    connect(severity_, qOverload<int>(&QComboBox::currentIndexChanged), this, &LogWidget::updateFilter);
    connect(search_, &QLineEdit::textChanged, this, &LogWidget::updateFilter);
    connect(clear, &QToolButton::clicked, this, [this]
    {
      entries_.clear();
      output_->clear();
    });
    connect(copy, &QToolButton::clicked, this, &LogWidget::copyAll);
    connect(save, &QToolButton::clicked, this, &LogWidget::saveLog);
  }

  QString LogWidget::text() const { return output_->toPlainText(); }
  int LogWidget::messageCount() const noexcept { return static_cast<int>(entries_.size()); }

  void LogWidget::appendMessage(int severity, const QString& message)
  {
    entries_.push_back({severity, QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                        message});
    // Bound the backing store to match the visible block cap so a long, heavy
    // logging session does not accumulate unbounded memory.
    constexpr std::size_t maxEntries = 20000;
    while (entries_.size() > maxEntries) entries_.pop_front();

    // Append only the new line rather than rebuilding the whole view, and keep
    // the user's scroll position unless they were already at the bottom, so a
    // streaming load does not yank them away from a line they scrolled up to read.
    const Entry& entry = entries_.back();
    if (!accepted(entry)) return;
    QScrollBar* bar = output_->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum();
    output_->appendPlainText(formatEntry(entry));
    if (atBottom) bar->setValue(bar->maximum());
  }

  void LogWidget::updateFilter()
  {
    output_->clear();
    for (const Entry& entry : entries_)
    {
      if (accepted(entry)) output_->appendPlainText(formatEntry(entry));
    }
    output_->verticalScrollBar()->setValue(output_->verticalScrollBar()->maximum());
  }

  bool LogWidget::accepted(const Entry& entry) const
  {
    const QtMsgType type = static_cast<QtMsgType>(entry.severity);
    const bool warningOrWorse = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
    const bool errorOrWorse = type == QtCriticalMsg || type == QtFatalMsg;
    const bool passesSeverity = severity_->currentIndex() == 0
      || (severity_->currentIndex() == 1 && type != QtDebugMsg)
      || (severity_->currentIndex() == 2 && warningOrWorse)
      || (severity_->currentIndex() == 3 && errorOrWorse);
    if (!passesSeverity) return false;
    const QString needle = search_->text().trimmed();
    return needle.isEmpty() || entry.message.contains(needle, Qt::CaseInsensitive);
  }

  QString LogWidget::formatEntry(const Entry& entry)
  {
    return QStringLiteral("[%1] %2 %3")
      .arg(entry.timestamp, severityName(entry.severity).leftJustified(5), entry.message);
  }

  QString LogWidget::allText() const
  {
    QString text;
    for (const Entry& entry : entries_)
    {
      text += formatEntry(entry);
      text += QLatin1Char('\n');
    }
    return text;
  }

  void LogWidget::copyAll()
  {
    QGuiApplication::clipboard()->setText(allText());
  }

  void LogWidget::saveLog()
  {
    const QString path = QFileDialog::getSaveFileName(
      this, tr("Save application log"), QStringLiteral("openms-viewer.log"),
      tr("Log files (*.log *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
      QMessageBox::warning(this, tr("Save application log"),
        tr("Could not open %1 for writing:\n%2").arg(path, file.errorString()));
      return;
    }
    QTextStream stream(&file);
    stream << allText();
    stream.flush();
    if (!file.commit())
    {
      QMessageBox::warning(this, tr("Save application log"),
        tr("Could not write %1:\n%2").arg(path, file.errorString()));
    }
  }
}
