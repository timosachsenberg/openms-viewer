#include "model/SelectionController.h"

#include <QSignalSpy>
#include <QTest>

#include <optional>

using OpenMSViewer::SelectionController;

class SelectionControllerTest final : public QObject
{
  Q_OBJECT

private slots:
  void mzRoundTripsAndSignalsValidity()
  {
    SelectionController selection;
    QVERIFY(!selection.mz().has_value());

    QSignalSpy spy(&selection, &SelectionController::mzChanged);
    selection.setMz(445.1203);
    QVERIFY(selection.mz().has_value());
    QCOMPARE(*selection.mz(), 445.1203);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.front().at(0).toDouble(), 445.1203);
    QVERIFY(spy.front().at(1).toBool());  // valid
  }

  void settingSameMzDoesNotResignal()
  {
    SelectionController selection;
    selection.setMz(500.0);
    QSignalSpy spy(&selection, &SelectionController::mzChanged);
    selection.setMz(500.0);          // identical -> no signal
    QCOMPARE(spy.count(), 0);
    selection.setMz(500.5);          // changed -> one signal
    QCOMPARE(spy.count(), 1);
  }

  void clearingEmitsInvalidAndResets()
  {
    SelectionController selection;
    selection.setMz(600.0);
    QSignalSpy spy(&selection, &SelectionController::mzChanged);
    selection.setMz(std::nullopt);
    QVERIFY(!selection.mz().has_value());
    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.front().at(1).toBool());  // invalid == cleared
  }

  void clearResetsMzAlongsideEverythingElse()
  {
    SelectionController selection;
    selection.setSpectrum(std::size_t{3});
    selection.setMz(700.0);
    QSignalSpy spy(&selection, &SelectionController::mzChanged);
    selection.clear();
    QVERIFY(!selection.mz().has_value());
    QVERIFY(!selection.spectrum().has_value());
    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.front().at(1).toBool());
  }
};

int runSelectionControllerTests(int argc, char** argv)
{
  SelectionControllerTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "SelectionControllerTest.moc"
