#include "widgets/LogWidget.h"

#include "logging/ApplicationLog.h"

#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QTextStream>
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
    auto* clear = new QPushButton(tr("Clear"), this);
    auto* copy = new QPushButton(tr("Copy all"), this);
    auto* save = new QPushButton(tr("Save…"), this);
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
    connect(clear, &QPushButton::clicked, this, [this]
    {
      entries_.clear();
      output_->clear();
    });
    connect(copy, &QPushButton::clicked, output_, &QPlainTextEdit::selectAll);
    connect(copy, &QPushButton::clicked, output_, &QPlainTextEdit::copy);
    connect(save, &QPushButton::clicked, this, &LogWidget::saveLog);
  }

  QString LogWidget::text() const { return output_->toPlainText(); }
  int LogWidget::messageCount() const noexcept { return static_cast<int>(entries_.size()); }

  void LogWidget::appendMessage(int severity, const QString& message)
  {
    entries_.push_back({severity, QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                        message});
    updateFilter();
  }

  void LogWidget::updateFilter()
  {
    output_->clear();
    const QString needle = search_->text().trimmed();
    for (const Entry& entry : entries_)
    {
      const QtMsgType type = static_cast<QtMsgType>(entry.severity);
      const bool warningOrWorse = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
      const bool errorOrWorse = type == QtCriticalMsg || type == QtFatalMsg;
      const bool accepted = severity_->currentIndex() == 0
        || (severity_->currentIndex() == 1 && type != QtDebugMsg)
        || (severity_->currentIndex() == 2 && warningOrWorse)
        || (severity_->currentIndex() == 3 && errorOrWorse);
      if (!accepted || (!needle.isEmpty() && !entry.message.contains(needle, Qt::CaseInsensitive))) continue;
      output_->appendPlainText(QStringLiteral("[%1] %2 %3")
        .arg(entry.timestamp, severityName(entry.severity).leftJustified(5), entry.message));
    }
    output_->verticalScrollBar()->setValue(output_->verticalScrollBar()->maximum());
  }

  void LogWidget::saveLog()
  {
    const QString path = QFileDialog::getSaveFileName(
      this, tr("Save application log"), QStringLiteral("openms-viewer.log"),
      tr("Log files (*.log *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream stream(&file);
    stream << output_->toPlainText();
    file.commit();
  }
}
