#include "MainWindow.h"
#include "widgets/RowStackWidget.h"
#include "widgets/PeakMapWidget.h"
#include "layout/LayoutModel.h"
#include "TestData.h"

#include <OpenMS/FORMAT/MzMLFile.h>

#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTest>

using namespace OpenMSViewer;

namespace
{
  QSplitter* liveRows(QWidget* window)
  {
    auto* stack = window->findChild<RowStackWidget*>(QStringLiteral("rowStack"));
    if (!stack) return nullptr;
    auto* scroll = stack->findChild<QScrollArea*>(QStringLiteral("rowStackScrollArea"));
    return scroll ? qobject_cast<QSplitter*>(scroll->widget()) : nullptr;
  }
}

class LayoutPersistenceTest final : public QObject
{
  Q_OBJECT

private slots:
  void initTestCase() { QSettings().clear(); }
  void cleanupTestCase() { QSettings().clear(); }

  // A real user session: load data, drag a row divider, quit, relaunch, reload.
  void rowHeightsSurviveRealSession()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("probe.mzML"));
    OpenMS::MzMLFile().store(path.toStdString(), TestData::experiment());

    QList<int> arranged;
    {
      MainWindow w;
      w.resize(1280, 1800);
      w.show();
      QVERIFY(QTest::qWaitForWindowExposed(&w));
      auto* pm = w.findChild<PeakMapWidget*>();
      w.loadFile(path);
      QTRY_VERIFY_WITH_TIMEOUT(pm->hasExperiment(), 5000);
      QTest::qWait(100);

      QSplitter* rows = liveRows(&w);
      QVERIFY(rows != nullptr);
      QVERIFY(rows->height() > 0);
      const int n = rows->count();
      QVERIFY(n >= 2);

      // Drag the first divider down: a tall peak-map row, the rest small.
      QList<int> want;
      want << 1000;
      for (int i = 1; i < n; ++i) want << 300;
      rows->setSizes(want);
      QCoreApplication::processEvents();
      emit rows->splitterMoved(0, 1);   // a real divider drag emits this
      arranged = rows->sizes();
      QVERIFY2(arranged.at(0) > 700, qPrintable(QStringLiteral("no slack to drag: %1").arg(arranged.at(0))));

      w.close();   // closeEvent persists
    }

    {
      MainWindow w2;
      w2.resize(1280, 1800);
      w2.show();
      QVERIFY(QTest::qWaitForWindowExposed(&w2));
      auto* pm = w2.findChild<PeakMapWidget*>();
      w2.loadFile(path);
      QTRY_VERIFY_WITH_TIMEOUT(pm->hasExperiment(), 5000);
      QTest::qWait(150);

      QSplitter* rows = liveRows(&w2);
      QVERIFY(rows != nullptr);
      QCOMPARE(rows->count(), arranged.size());
      QVERIFY2(qAbs(rows->sizes().at(0) - arranged.at(0)) < 20,
               qPrintable(QStringLiteral("peak-map row height not restored: got %1, arranged %2")
                            .arg(rows->sizes().at(0)).arg(arranged.at(0))));
      w2.close();
    }
  }

  // Does the movable main toolbar's position survive a session? The old code
  // persisted it inside QMainWindow::saveState() under main/state.
  void toolbarPositionSurvivesSession()
  {
    {
      MainWindow w;
      w.resize(1280, 1800);
      w.show();
      QVERIFY(QTest::qWaitForWindowExposed(&w));
      auto* bar = w.findChild<QToolBar*>(QStringLiteral("fileToolbar"));
      QVERIFY(bar != nullptr);
      QVERIFY2(bar->isMovable(), "toolbar is not user-movable; position cannot be lost");
      // The user drags it to the left edge.
      w.addToolBar(Qt::LeftToolBarArea, bar);
      QCoreApplication::processEvents();
      QCOMPARE(w.toolBarArea(bar), Qt::LeftToolBarArea);
      w.close();
    }
    {
      MainWindow w2;
      w2.resize(1280, 1800);
      w2.show();
      QVERIFY(QTest::qWaitForWindowExposed(&w2));
      auto* bar = w2.findChild<QToolBar*>(QStringLiteral("fileToolbar"));
      QVERIFY(bar != nullptr);
      QCOMPARE(w2.toolBarArea(bar), Qt::LeftToolBarArea);
      w2.close();
    }
  }
};

int runLayoutPersistenceTests(int argc, char** argv)
{
  LayoutPersistenceTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "LayoutPersistenceTest.moc"
