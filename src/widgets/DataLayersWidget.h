#pragma once

#include <QWidget>

#include <array>

class QLabel;
class QTableWidget;

namespace OpenMSViewer
{
  class DataLayersWidget final : public QWidget
  {
    Q_OBJECT

  public:
    enum class Layer { Primary, Features, Identifications, Consensus, OpenSwath, Count };

    explicit DataLayersWidget(QWidget* parent = nullptr);

    void setLayer(Layer layer, const QString& sourcePath, const QString& summary,
                  bool available, bool visible = true, bool modified = false);
    void clear();

  signals:
    void visibilityChanged(OpenMSViewer::DataLayersWidget::Layer layer, bool visible);
    void removeRequested(OpenMSViewer::DataLayersWidget::Layer layer);

  private:
    struct State
    {
      QString sourcePath;
      QString summary;
      bool available{false};
      bool visible{true};
      bool modified{false};
    };

    void rebuild();
    static QString layerName(Layer layer);

    std::array<State, static_cast<std::size_t>(Layer::Count)> states_;
    QLabel* empty_{nullptr};
    QTableWidget* table_{nullptr};
  };
}
