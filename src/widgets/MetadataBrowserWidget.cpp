#include "widgets/MetadataBrowserWidget.h"

#include <OpenMS/METADATA/DataProcessing.h>
#include <OpenMS/METADATA/IonDetector.h>
#include <OpenMS/METADATA/IonSource.h>
#include <OpenMS/METADATA/MassAnalyzer.h>
#include <OpenMS/METADATA/MetaInfoInterface.h>

#include <QHeaderView>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <vector>

namespace OpenMSViewer
{
  namespace
  {
    QString text(const std::string& value) { return QString::fromStdString(value); }

    // Bounds-checked lookup into an OpenMS "NamesOf<enum>" table.
    template <typename Enum, std::size_t N>
    QString enumName(const std::string (&names)[N], Enum value)
    {
      const auto index = static_cast<std::size_t>(value);
      return index < N ? QString::fromStdString(names[index]) : QStringLiteral("?");
    }

    QTreeWidgetItem* add(QTreeWidgetItem* parent, const QString& name, const QString& value)
    {
      auto* item = new QTreeWidgetItem(parent);
      item->setText(0, name);
      item->setText(1, value);
      return item;
    }

    // Add a leaf only when the string is non-empty, to keep the tree free of blanks.
    void addText(QTreeWidgetItem* parent, const QString& name, const std::string& value)
    {
      if (!value.empty()) add(parent, name, text(value));
    }

    void addMetaValues(QTreeWidgetItem* parent, const OpenMS::MetaInfoInterface& meta)
    {
      std::vector<std::string> keys;
      meta.getKeys(keys);
      if (keys.empty()) return;
      auto* node = add(parent, QObject::tr("User params"), QString::number(keys.size()));
      for (const std::string& key : keys)
        add(node, text(key), text(meta.getMetaValue(key).toString()));
    }
  }

  MetadataBrowserWidget::MetadataBrowserWidget(QWidget* parent) : QWidget(parent)
  {
    setObjectName(QStringLiteral("metadataBrowser"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("metadataTree"));
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("Property"), tr("Value")});
    tree_->setAlternatingRowColors(true);
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setStretchLastSection(true);
    layout->addWidget(tree_);
  }

  void MetadataBrowserWidget::setExperiment(std::shared_ptr<const OpenMS::MSExperiment> experiment)
  {
    experiment_ = std::move(experiment);
    spectrumIndex_.reset();
    rebuild();
  }

  void MetadataBrowserWidget::setSpectrumIndex(std::optional<std::size_t> index)
  {
    if (spectrumIndex_ == index) return;
    spectrumIndex_ = index;
    rebuild();
  }

  void MetadataBrowserWidget::clear()
  {
    experiment_.reset();
    spectrumIndex_.reset();
    rebuild();
  }

  void MetadataBrowserWidget::rebuild()
  {
    tree_->clear();
    if (!experiment_)
    {
      tree_->addTopLevelItem(new QTreeWidgetItem({tr("No data loaded")}));
      return;
    }

    // ---- Experiment-level metadata ----
    auto* fileItem = new QTreeWidgetItem(tree_, {tr("File")});
    addText(fileItem, tr("Loaded date/time"), experiment_->getDateTime().toString());
    addText(fileItem, tr("Comment"), experiment_->getComment());
    add(fileItem, tr("Spectra"), QString::number(experiment_->size()));
    add(fileItem, tr("Chromatograms"), QString::number(experiment_->getChromatograms().size()));
    for (const OpenMS::SourceFile& source : experiment_->getSourceFiles())
    {
      auto* node = add(fileItem, tr("Source file"), text(source.getNameOfFile()));
      addText(node, tr("Path"), source.getPathToFile());
      addText(node, tr("File type"), source.getFileType());
      addText(node, tr("Native ID type"), source.getNativeIDType());
      addText(node, tr("Checksum"), source.getChecksum());
    }

    const OpenMS::Instrument& instrument = experiment_->getInstrument();
    auto* instrumentItem = new QTreeWidgetItem(
      tree_, {tr("Instrument"), text(instrument.getName())});
    addText(instrumentItem, tr("Vendor"), instrument.getVendor());
    addText(instrumentItem, tr("Model"), instrument.getModel());
    addText(instrumentItem, tr("Customizations"), instrument.getCustomizations());
    for (const OpenMS::IonSource& source : instrument.getIonSources())
    {
      auto* node = add(instrumentItem, tr("Ion source"),
                       enumName(OpenMS::IonSource::NamesOfIonizationMethod, source.getIonizationMethod()));
      add(node, tr("Inlet"), enumName(OpenMS::IonSource::NamesOfInletType, source.getInletType()));
    }
    for (const OpenMS::MassAnalyzer& analyzer : instrument.getMassAnalyzers())
    {
      auto* node = add(instrumentItem, tr("Mass analyzer"),
                       enumName(OpenMS::MassAnalyzer::NamesOfAnalyzerType, analyzer.getType()));
      if (analyzer.getResolution() > 0.0)
        add(node, tr("Resolution"), QString::number(analyzer.getResolution(), 'g', 6));
    }
    for (const OpenMS::IonDetector& detector : instrument.getIonDetectors())
      add(instrumentItem, tr("Ion detector"),
          enumName(OpenMS::IonDetector::NamesOfType, detector.getType()));
    if (!instrument.getSoftware().getName().empty())
      add(instrumentItem, tr("Software"),
          text(instrument.getSoftware().getName() + " " + instrument.getSoftware().getVersion()));

    const OpenMS::Sample& sample = experiment_->getSample();
    if (!sample.getName().empty() || !sample.getOrganism().empty() || !sample.getNumber().empty())
    {
      auto* sampleItem = new QTreeWidgetItem(tree_, {tr("Sample"), text(sample.getName())});
      addText(sampleItem, tr("Number"), sample.getNumber());
      addText(sampleItem, tr("Organism"), sample.getOrganism());
      addText(sampleItem, tr("Comment"), sample.getComment());
    }

    addMetaValues(new QTreeWidgetItem(tree_, {tr("Experiment user params")}), *experiment_);

    // ---- Selected-scan metadata ----
    if (spectrumIndex_ && *spectrumIndex_ < experiment_->size())
    {
      const OpenMS::MSSpectrum& spectrum = (*experiment_)[*spectrumIndex_];
      auto* scanItem = new QTreeWidgetItem(
        tree_, {tr("Scan #%1").arg(*spectrumIndex_ + 1), text(spectrum.getNativeID())});
      scanItem->setExpanded(true);
      add(scanItem, tr("RT"), QStringLiteral("%1 s").arg(spectrum.getRT(), 0, 'f', 3));
      add(scanItem, tr("MS level"), QString::number(spectrum.getMSLevel()));
      add(scanItem, tr("Peaks"), QString::number(spectrum.size()));
      add(scanItem, tr("Type"),
          enumName(OpenMS::SpectrumSettings::NamesOfSpectrumType, spectrum.getType()));
      for (const OpenMS::Precursor& precursor : spectrum.getPrecursors())
      {
        auto* node = add(scanItem, tr("Precursor"),
                         QStringLiteral("m/z %1").arg(precursor.getMZ(), 0, 'f', 4));
        add(node, tr("Charge"), QString::number(precursor.getCharge()));
        if (precursor.getIsolationWindowLowerOffset() > 0.0
            || precursor.getIsolationWindowUpperOffset() > 0.0)
          add(node, tr("Isolation window"),
              QStringLiteral("%1 – %2")
                .arg(precursor.getMZ() - precursor.getIsolationWindowLowerOffset(), 0, 'f', 4)
                .arg(precursor.getMZ() + precursor.getIsolationWindowUpperOffset(), 0, 'f', 4));
        const QStringList activation = [&]
        {
          QStringList methods;
          for (const auto& method : precursor.getActivationMethodsAsString())
            methods << text(method);
          return methods;
        }();
        if (!activation.isEmpty()) add(node, tr("Activation"), activation.join(QStringLiteral(", ")));
        if (precursor.getIntensity() > 0.0)
          add(node, tr("Intensity"), QString::number(precursor.getIntensity(), 'g', 4));
      }
      for (const auto& processing : spectrum.getDataProcessing())
      {
        if (!processing) continue;
        auto* node = add(scanItem, tr("Data processing"),
                         text(processing->getSoftware().getName() + " "
                              + processing->getSoftware().getVersion()));
        QStringList actions;
        for (const auto action : processing->getProcessingActions())
          actions << enumName(OpenMS::DataProcessing::NamesOfProcessingAction, action);
        if (!actions.isEmpty()) add(node, tr("Actions"), actions.join(QStringLiteral(", ")));
        addText(node, tr("Completed"), processing->getCompletionTime().toString());
      }
      addMetaValues(scanItem, spectrum);
    }

    tree_->expandItem(tree_->topLevelItem(0));
  }
}
