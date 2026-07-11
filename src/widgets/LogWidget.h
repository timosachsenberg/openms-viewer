#pragma once

#include <QWidget>

#include <vector>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;

namespace OpenMSViewer
{
  class LogWidget final : public QWidget
  {
    Q_OBJECT

  public:
    explicit LogWidget(QWidget* parent = nullptr);

    [[nodiscard]] QString text() const;
    [[nodiscard]] int messageCount() const noexcept;

  private slots:
    void appendMessage(int severity, const QString& message);
    void updateFilter();
    void saveLog();

  private:
    struct Entry
    {
      int severity{0};
      QString timestamp;
      QString message;
    };

    std::vector<Entry> entries_;
    QComboBox* severity_{nullptr};
    QLineEdit* search_{nullptr};
    QPlainTextEdit* output_{nullptr};
  };
}
