#include <QApplication>
#include <QStandardPaths>

int runPeakMapRasterizerTests(int argc, char** argv);
int runFormatRegistryTests(int argc, char** argv);
int runRichFormatsTests(int argc, char** argv);
int runOswStoreTests(int argc, char** argv);
int runChromatogramSourceTests(int argc, char** argv);
int runOswPanelTests(int argc, char** argv);
int runConsensusDocumentTests(int argc, char** argv);
int runFeatureDocumentTests(int argc, char** argv);
int runMainWindowFeatureWorkflowTests(int argc, char** argv);
int runIdentificationDocumentTests(int argc, char** argv);
int runMainWindowIdentificationWorkflowTests(int argc, char** argv);
int runSpectrumAnnotationTests(int argc, char** argv);
int runViewerDocumentTests(int argc, char** argv);
int runDataPanelsWorkflowTests(int argc, char** argv);
int runMobilityFaimsWorkflowTests(int argc, char** argv);
int runExportWorkflowTests(int argc, char** argv);
int runImagingWorkflowTests(int argc, char** argv);
int runSpectrumInteractionTests(int argc, char** argv);
int runLogWidgetTests(int argc, char** argv);
int runUxWorkflowTests(int argc, char** argv);

int main(int argc, char** argv)
{
  QStandardPaths::setTestModeEnabled(true);
  QApplication application(argc, argv);
  QApplication::setOrganizationName(QStringLiteral("OpenMSViewerTests"));
  QApplication::setApplicationName(QStringLiteral("OpenMSViewerTests"));
  int status = runPeakMapRasterizerTests(argc, argv);
  status |= runFormatRegistryTests(argc, argv);
  status |= runRichFormatsTests(argc, argv);
  status |= runOswStoreTests(argc, argv);
  status |= runChromatogramSourceTests(argc, argv);
  status |= runOswPanelTests(argc, argv);
  status |= runConsensusDocumentTests(argc, argv);
  status |= runFeatureDocumentTests(argc, argv);
  status |= runMainWindowFeatureWorkflowTests(argc, argv);
  status |= runIdentificationDocumentTests(argc, argv);
  status |= runSpectrumAnnotationTests(argc, argv);
  status |= runMainWindowIdentificationWorkflowTests(argc, argv);
  status |= runViewerDocumentTests(argc, argv);
  status |= runDataPanelsWorkflowTests(argc, argv);
  status |= runMobilityFaimsWorkflowTests(argc, argv);
  status |= runExportWorkflowTests(argc, argv);
  status |= runImagingWorkflowTests(argc, argv);
  status |= runSpectrumInteractionTests(argc, argv);
  status |= runLogWidgetTests(argc, argv);
  status |= runUxWorkflowTests(argc, argv);
  return status;
}
