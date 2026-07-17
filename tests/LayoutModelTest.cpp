#include "layout/LayoutModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTest>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <random>
#include <vector>

namespace
{
  using OpenMSViewer::LayoutModel;
  using OpenMSViewer::PanelId;
  using Kind = LayoutModel::DropKind;
  using Position = LayoutModel::Position;
  using Row = LayoutModel::Row;
  using Target = LayoutModel::DropTarget;

  // These tests spell out the two-panel rule in their expectations (three-panel
  // rows, side drops onto full rows); widening the model means rewriting them,
  // not just flipping the constant.
  static_assert(LayoutModel::MaxPanelsPerRow == 2);

  const PanelId PeakMap{QStringLiteral("peakmap")};
  const PanelId Tic{QStringLiteral("tic")};
  const PanelId Spectrum{QStringLiteral("spectrum")};
  const PanelId Features{QStringLiteral("features")};
  const PanelId Log{QStringLiteral("log")};
  const std::vector<PanelId> Known{PeakMap, Tic, Spectrum, Features, Log};

  const std::array<Kind, 4> AllKinds{Kind::NewRowAbove, Kind::NewRowBelow, Kind::LeftOfAnchor,
                                     Kind::RightOfAnchor};

  const QString VersionKey{QStringLiteral("version")};
  const QString RowsKey{QStringLiteral("rows")};

  std::vector<Row> rowsOf(std::initializer_list<std::initializer_list<PanelId>> rows)
  {
    std::vector<Row> out;
    for (const auto& row : rows) out.push_back(Row{std::vector<PanelId>(row)});
    return out;
  }

  // Two-panel rows are reachable only through applyDrop: the model exposes no
  // wide-row constructor, which is the point of it.
  LayoutModel build(std::initializer_list<std::initializer_list<PanelId>> rows)
  {
    LayoutModel model;
    for (const auto& row : rows)
    {
      const std::vector<PanelId> ids(row);
      model.appendRow(ids.front());
      for (std::size_t i = 1; i < ids.size(); ++i)
      {
        model.appendRow(ids[i]);
        model.applyDrop(ids[i], Target{Kind::RightOfAnchor, ids[i - 1]});
      }
    }
    return model;
  }

  // The invariant restated independently of the model's own invariantHolds(),
  // so a checker that lies cannot make the fuzz walk pass.
  bool wellFormed(const LayoutModel& model)
  {
    std::vector<PanelId> seen;
    for (const Row& row : model.rows())
    {
      if (row.panels.empty()) return false;
      if (std::ssize(row.panels) > LayoutModel::MaxPanelsPerRow) return false;
      seen.insert(seen.end(), row.panels.begin(), row.panels.end());
    }
    std::sort(seen.begin(), seen.end());
    return std::adjacent_find(seen.begin(), seen.end()) == seen.end();
  }

  QString describe(const LayoutModel& model)
  {
    QStringList rows;
    for (const Row& row : model.rows())
    {
      const QStringList panels(row.panels.begin(), row.panels.end());
      rows << QStringLiteral("[%1]").arg(panels.join(QLatin1Char('|')));
    }
    return rows.isEmpty() ? QStringLiteral("<empty>") : rows.join(QStringLiteral(" / "));
  }

  bool offers(const std::vector<Target>& targets, const Target& target)
  {
    return std::find(targets.begin(), targets.end(), target) != targets.end();
  }

  // The contract fixes the document's outer keys but not how a Row is encoded,
  // so corruption cases clone the shape of a row toJson() actually emitted
  // rather than guessing at it.
  QJsonValue rowLike(const QJsonValue& sample, const QStringList& panels)
  {
    QJsonArray ids;
    for (const QString& panel : panels) ids.append(panel);
    if (!sample.isObject()) return ids;
    QJsonObject row = sample.toObject();
    const QStringList keys = row.keys();
    for (const QString& key : keys)
    {
      if (row.value(key).isArray())
      {
        row[key] = ids;
        return row;
      }
    }
    return ids;
  }

  QJsonObject withRows(const LayoutModel& model, const std::vector<QStringList>& rows)
  {
    QJsonObject json = model.toJson();
    const QJsonArray sample = json.value(RowsKey).toArray();
    QJsonArray out;
    for (const QStringList& row : rows) out.append(rowLike(sample.first(), row));
    json[RowsKey] = out;
    return json;
  }
}

class LayoutModelTest final : public QObject
{
  Q_OBJECT

private slots:
  void defaultModelIsEmptyAndOffersNothing()
  {
    const LayoutModel model;
    QVERIFY(model.isEmpty());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.rows().empty());
    QVERIFY(model.panels().empty());
    QVERIFY(!model.contains(Tic));
    QVERIFY(!model.locate(Tic).has_value());
    QVERIFY(model.invariantHolds());

    // No anchors exist, so there is nowhere to land.
    QVERIFY(model.dropTargets(Tic).empty());
    for (const Kind kind : AllKinds) QVERIFY(!model.canDrop(Tic, Target{kind, Spectrum}));
  }

  void builderProducesTheRequestedShape()
  {
    const LayoutModel model = build({{Tic, Spectrum}, {PeakMap}, {Features, Log}});
    QCOMPARE(model.rows(), rowsOf({{Tic, Spectrum}, {PeakMap}, {Features, Log}}));
    QVERIFY(model.invariantHolds());
    QVERIFY(wellFormed(model));
  }

  void appendAndInsertRowOrder()
  {
    LayoutModel model;
    model.appendRow(PeakMap);
    model.appendRow(Tic);
    QCOMPARE(model.rows(), rowsOf({{PeakMap}, {Tic}}));

    model.insertRow(0, Spectrum);
    QCOMPARE(model.rows(), rowsOf({{Spectrum}, {PeakMap}, {Tic}}));

    model.insertRow(2, Features);
    QCOMPARE(model.rows(), rowsOf({{Spectrum}, {PeakMap}, {Features}, {Tic}}));

    QCOMPARE(model.rowCount(), 4);
    QVERIFY(!model.isEmpty());
    QVERIFY(model.invariantHolds());
  }

  void insertRowClampsIndex()
  {
    LayoutModel model;
    model.insertRow(7, PeakMap);
    QCOMPARE(model.rows(), rowsOf({{PeakMap}}));

    model.insertRow(-3, Tic);
    QCOMPARE(model.rows(), rowsOf({{Tic}, {PeakMap}}));

    model.insertRow(99, Spectrum);
    QCOMPARE(model.rows(), rowsOf({{Tic}, {PeakMap}, {Spectrum}}));

    model.insertRow(model.rowCount(), Features);
    QCOMPARE(model.rows(), rowsOf({{Tic}, {PeakMap}, {Spectrum}, {Features}}));
    QVERIFY(model.invariantHolds());
  }

  void addingAPresentPanelIsANoOp()
  {
    LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});
    const LayoutModel before = model;

    model.appendRow(Tic);
    QCOMPARE(model, before);
    model.appendRow(PeakMap);
    QCOMPARE(model, before);
    model.insertRow(0, Spectrum);
    QCOMPARE(model, before);
    model.insertRow(1, PeakMap);
    QCOMPARE(model, before);

    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.invariantHolds());
  }

  void removeNormalizesRows()
  {
    LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});
    QCOMPARE(model.rowCount(), 2);

    // A row left with one panel survives as a full-width row.
    model.remove(Spectrum);
    QCOMPARE(model.rows(), rowsOf({{Tic}, {PeakMap}}));
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(!model.contains(Spectrum));
    QVERIFY(!model.locate(Spectrum).has_value());

    // A row left with none ceases to exist.
    model.remove(PeakMap);
    QCOMPARE(model.rows(), rowsOf({{Tic}}));
    QCOMPARE(model.rowCount(), 1);

    // Removing a panel that is not in the layout changes nothing.
    const LayoutModel before = model;
    model.remove(Features);
    QCOMPARE(model, before);
    model.remove(QStringLiteral("nope"));
    QCOMPARE(model, before);

    model.remove(Tic);
    QVERIFY(model.isEmpty());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.invariantHolds());
  }

  void clearRemovesEverything()
  {
    LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});
    model.clear();
    QCOMPARE(model, LayoutModel{});
    QVERIFY(model.isEmpty());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.panels().empty());
    QVERIFY(model.invariantHolds());
  }

  void locateContainsAndPanelOrder()
  {
    const LayoutModel model = build({{Tic, Spectrum}, {PeakMap}, {Features, Log}});

    // Top to bottom, then left to right.
    QCOMPARE(model.panels(), std::vector<PanelId>({Tic, Spectrum, PeakMap, Features, Log}));

    QCOMPARE(model.locate(Tic), std::make_optional(Position{0, 0}));
    QCOMPARE(model.locate(Spectrum), std::make_optional(Position{0, 1}));
    QCOMPARE(model.locate(PeakMap), std::make_optional(Position{1, 0}));
    QCOMPARE(model.locate(Features), std::make_optional(Position{2, 0}));
    QCOMPARE(model.locate(Log), std::make_optional(Position{2, 1}));
    QVERIFY(!model.locate(QStringLiteral("nope")).has_value());

    for (const PanelId& panel : model.panels()) QVERIFY(model.contains(panel));
    QVERIFY(!model.contains(QStringLiteral("nope")));
    QVERIFY(!model.contains(QString{}));

    // locate() and contains() are two views of one fact.
    for (const PanelId& panel : Known) QCOMPARE(model.contains(panel), model.locate(panel).has_value());
  }

  void canDropRejectsSelfAnchorAndUnknownAnchor()
  {
    const LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});
    for (const Kind kind : AllKinds)
    {
      QVERIFY(!model.canDrop(Tic, Target{kind, Tic}));
      QVERIFY(!model.canDrop(PeakMap, Target{kind, PeakMap}));
      QVERIFY(!model.canDrop(Tic, Target{kind, QStringLiteral("nope")}));
      // A hidden panel is not an anchor either — anchors come from the layout.
      QVERIFY(!model.canDrop(Tic, Target{kind, Features}));
      QVERIFY(!model.canDrop(Features, Target{kind, Log}));
    }
  }

  void newRowTargetsAreAlwaysLegal()
  {
    const LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});
    for (const PanelId& anchor : model.panels())
    {
      for (const PanelId& panel : Known)
      {
        if (panel == anchor) continue;
        // A new row is always available: it takes nothing from the anchor's row.
        // Holds whether `panel` is in the layout (Tic, Spectrum, PeakMap) or
        // hidden (Features, Log).
        QVERIFY(model.canDrop(panel, Target{Kind::NewRowAbove, anchor}));
        QVERIFY(model.canDrop(panel, Target{Kind::NewRowBelow, anchor}));
      }
    }
  }

  void aFullRowOffersNoSideTarget()
  {
    const LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});

    // The headline rule: row 0 already holds MaxPanelsPerRow panels, so nothing
    // from outside it may join — a third panel has nowhere legal to land.
    QVERIFY(!model.canDrop(PeakMap, Target{Kind::LeftOfAnchor, Tic}));
    QVERIFY(!model.canDrop(PeakMap, Target{Kind::RightOfAnchor, Tic}));
    QVERIFY(!model.canDrop(PeakMap, Target{Kind::LeftOfAnchor, Spectrum}));
    QVERIFY(!model.canDrop(PeakMap, Target{Kind::RightOfAnchor, Spectrum}));
    QVERIFY(!model.canDrop(Features, Target{Kind::LeftOfAnchor, Tic}));
    QVERIFY(!model.canDrop(Log, Target{Kind::RightOfAnchor, Spectrum}));

    // ...but a side drop by a panel already in that row only reorders the two.
    QVERIFY(model.canDrop(Spectrum, Target{Kind::LeftOfAnchor, Tic}));
    QVERIFY(model.canDrop(Spectrum, Target{Kind::RightOfAnchor, Tic}));
    QVERIFY(model.canDrop(Tic, Target{Kind::LeftOfAnchor, Spectrum}));
    QVERIFY(model.canDrop(Tic, Target{Kind::RightOfAnchor, Spectrum}));

    // A one-panel row has room for anyone, placed or hidden.
    QVERIFY(model.canDrop(Features, Target{Kind::LeftOfAnchor, PeakMap}));
    QVERIFY(model.canDrop(Log, Target{Kind::RightOfAnchor, PeakMap}));
    QVERIFY(model.canDrop(Tic, Target{Kind::RightOfAnchor, PeakMap}));
    QVERIFY(model.canDrop(Spectrum, Target{Kind::LeftOfAnchor, PeakMap}));
  }

  void dropTargetsAgreeWithCanDrop()
  {
    const LayoutModel model = build({{Tic, Spectrum}, {PeakMap}, {Features}});
    for (const PanelId& panel : Known)
    {
      const auto targets = model.dropTargets(panel);
      for (const PanelId& anchor : model.panels())
      {
        for (const Kind kind : AllKinds)
        {
          const Target target{kind, anchor};
          QCOMPARE(offers(targets, target), model.canDrop(panel, target));
        }
      }
      // Nothing outside that grid, and each target offered once.
      for (const Target& target : targets)
      {
        QVERIFY(model.canDrop(panel, target));
        QVERIFY(model.contains(target.anchor));
        QVERIFY(target.anchor != panel);
        QCOMPARE(std::count(targets.begin(), targets.end(), target), std::ptrdiff_t{1});
      }
    }
  }

  void dropTargetsForAnOutsidePanelSkipFullRows()
  {
    const LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});
    const auto targets = model.dropTargets(Log);

    QVERIFY(!offers(targets, Target{Kind::LeftOfAnchor, Tic}));
    QVERIFY(!offers(targets, Target{Kind::RightOfAnchor, Tic}));
    QVERIFY(!offers(targets, Target{Kind::LeftOfAnchor, Spectrum}));
    QVERIFY(!offers(targets, Target{Kind::RightOfAnchor, Spectrum}));

    QVERIFY(offers(targets, Target{Kind::NewRowAbove, Tic}));
    QVERIFY(offers(targets, Target{Kind::NewRowBelow, Tic}));
    QVERIFY(offers(targets, Target{Kind::NewRowAbove, Spectrum}));
    QVERIFY(offers(targets, Target{Kind::NewRowBelow, Spectrum}));
    QVERIFY(offers(targets, Target{Kind::LeftOfAnchor, PeakMap}));
    QVERIFY(offers(targets, Target{Kind::RightOfAnchor, PeakMap}));

    // Two new-row targets per anchor, plus the sides of the one-panel row.
    QCOMPARE(targets.size(), std::size_t{8});

    // A panel inside the full row does get its two side targets back.
    QCOMPARE(model.dropTargets(Tic).size(), std::size_t{8});
    QVERIFY(offers(model.dropTargets(Tic), Target{Kind::LeftOfAnchor, Spectrum}));
  }

  void applyDropSwapsWithinARow()
  {
    LayoutModel model = build({{Tic, Spectrum}});
    QVERIFY(model.applyDrop(Spectrum, Target{Kind::LeftOfAnchor, Tic}));
    QCOMPARE(model.rows(), rowsOf({{Spectrum, Tic}}));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.invariantHolds());

    QVERIFY(model.applyDrop(Spectrum, Target{Kind::RightOfAnchor, Tic}));
    QCOMPARE(model.rows(), rowsOf({{Tic, Spectrum}}));

    // Asking for the order it already has is legal and settles nothing.
    QVERIFY(model.applyDrop(Spectrum, Target{Kind::RightOfAnchor, Tic}));
    QCOMPARE(model.rows(), rowsOf({{Tic, Spectrum}}));
    QVERIFY(model.applyDrop(Tic, Target{Kind::LeftOfAnchor, Spectrum}));
    QCOMPARE(model.rows(), rowsOf({{Tic, Spectrum}}));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.invariantHolds());
  }

  void applyDropSplitsAFullRow()
  {
    LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});
    QVERIFY(model.applyDrop(Spectrum, Target{Kind::NewRowBelow, PeakMap}));
    QCOMPARE(model.rows(), rowsOf({{Tic}, {PeakMap}, {Spectrum}}));
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.invariantHolds());
  }

  void applyDropRetargetsByAnchorNotRowIndex()
  {
    // Tic sits alone in row 0, so moving it out erases that row and shifts
    // every row below up by one. An implementation that resolved the anchor to
    // a row index before the removal would insert against the wrong row — the
    // anchor-based API exists so that cannot happen.
    LayoutModel above = build({{Tic}, {Spectrum}, {PeakMap}});
    QVERIFY(above.applyDrop(Tic, Target{Kind::NewRowAbove, PeakMap}));
    QCOMPARE(above.rows(), rowsOf({{Spectrum}, {Tic}, {PeakMap}}));
    QCOMPARE(above.rowCount(), 3);
    QVERIFY(above.invariantHolds());

    LayoutModel below = build({{Tic}, {Spectrum}, {PeakMap}});
    QVERIFY(below.applyDrop(Tic, Target{Kind::NewRowBelow, Spectrum}));
    QCOMPARE(below.rows(), rowsOf({{Spectrum}, {Tic}, {PeakMap}}));
    QVERIFY(below.invariantHolds());

    // The same shift under a side drop: Tic joins PeakMap's row wherever that
    // row ended up, and the row Tic left is gone.
    LayoutModel side = build({{Tic}, {Spectrum}, {PeakMap}});
    QVERIFY(side.applyDrop(Tic, Target{Kind::RightOfAnchor, PeakMap}));
    QCOMPARE(side.rows(), rowsOf({{Spectrum}, {PeakMap, Tic}}));
    QCOMPARE(side.rowCount(), 2);
    QCOMPARE(side.locate(Tic), std::make_optional(Position{1, 1}));
    QVERIFY(side.invariantHolds());

    LayoutModel left = build({{Tic}, {Spectrum}, {PeakMap}});
    QVERIFY(left.applyDrop(Tic, Target{Kind::LeftOfAnchor, PeakMap}));
    QCOMPARE(left.rows(), rowsOf({{Spectrum}, {Tic, PeakMap}}));
    QVERIFY(left.invariantHolds());

    // When the dragged panel's row survives, nothing shifts — same anchor, same
    // relative result.
    LayoutModel wide = build({{Tic, Features}, {Spectrum}, {PeakMap}});
    QVERIFY(wide.applyDrop(Tic, Target{Kind::NewRowAbove, PeakMap}));
    QCOMPARE(wide.rows(), rowsOf({{Features}, {Spectrum}, {Tic}, {PeakMap}}));
    QVERIFY(wide.invariantHolds());

    // Moving a lone panel to a new row directly above the row below it is a
    // legal no-op rather than a drift.
    LayoutModel stay = build({{Tic}, {Spectrum}});
    QVERIFY(stay.applyDrop(Tic, Target{Kind::NewRowAbove, Spectrum}));
    QCOMPARE(stay.rows(), rowsOf({{Tic}, {Spectrum}}));
    QVERIFY(stay.invariantHolds());
  }

  void applyDropOnAnIllegalTargetChangesNothing()
  {
    LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});
    const LayoutModel before = model;

    QVERIFY(!model.applyDrop(PeakMap, Target{Kind::LeftOfAnchor, Tic}));
    QCOMPARE(model, before);
    QVERIFY(!model.applyDrop(PeakMap, Target{Kind::RightOfAnchor, Spectrum}));
    QCOMPARE(model, before);
    QVERIFY(!model.applyDrop(Log, Target{Kind::RightOfAnchor, Spectrum}));
    QCOMPARE(model, before);
    QVERIFY(!model.applyDrop(Tic, Target{Kind::NewRowAbove, Tic}));
    QCOMPARE(model, before);
    QVERIFY(!model.applyDrop(Tic, Target{Kind::NewRowBelow, QStringLiteral("nope")}));
    QCOMPARE(model, before);
    QVERIFY(!model.applyDrop(Tic, Target{Kind::LeftOfAnchor, Features}));
    QCOMPARE(model, before);

    // A refusal never even half-applies: the panel is still where it was.
    QCOMPARE(model.locate(PeakMap), std::make_optional(Position{1, 0}));
    QVERIFY(!model.contains(Log));
    QVERIFY(model.invariantHolds());
  }

  void applyDropPlacesAHiddenPanel()
  {
    LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});
    QVERIFY(!model.contains(Features));

    QVERIFY(model.applyDrop(Features, Target{Kind::RightOfAnchor, PeakMap}));
    QCOMPARE(model.rows(), rowsOf({{Tic, Spectrum}, {PeakMap, Features}}));

    QVERIFY(model.applyDrop(Log, Target{Kind::NewRowAbove, Tic}));
    QCOMPARE(model.rows(), rowsOf({{Log}, {Tic, Spectrum}, {PeakMap, Features}}));
    QCOMPARE(model.panels(), std::vector<PanelId>({Log, Tic, Spectrum, PeakMap, Features}));

    // Both rows are full now, so the next hidden panel has only new-row targets.
    model.remove(Log);
    const auto targets = model.dropTargets(Log);
    QCOMPARE(targets.size(), std::size_t{8});
    for (const Target& target : targets)
      QVERIFY(target.kind == Kind::NewRowAbove || target.kind == Kind::NewRowBelow);
    QVERIFY(model.invariantHolds());
  }

  // The invariant is the whole reason this model exists, so hammer it: a
  // fixed-seed walk over every public mutator, asserting after each call that
  // no row grew past MaxPanelsPerRow, went empty, or listed a panel twice.
  void noSequenceOfPublicCallsWidensARow()
  {
    std::mt19937 random(20240517u);
    std::uniform_int_distribution<std::size_t> panelDist(0, Known.size() - 1);
    std::uniform_int_distribution<std::size_t> kindDist(0, AllKinds.size() - 1);
    std::uniform_int_distribution<int> indexDist(-2, 7);
    std::uniform_int_distribution<int> actionDist(0, 9);
    const auto panel = [&] { return Known[panelDist(random)]; };

    LayoutModel model;
    for (int step = 0; step < 20000; ++step)
    {
      if (step % 500 == 499) model.clear();

      const int action = actionDist(random);
      if (action <= 1)
      {
        model.appendRow(panel());
      }
      else if (action <= 3)
      {
        model.insertRow(indexDist(random), panel());
      }
      else if (action <= 5)
      {
        model.remove(panel());
      }
      else
      {
        const PanelId dragged = panel();
        const Target target{AllKinds[kindDist(random)], panel()};
        const LayoutModel before = model;
        const bool applied = model.applyDrop(dragged, target);
        QCOMPARE(applied, before.canDrop(dragged, target));
        if (!applied) QCOMPARE(model, before);
      }

      QVERIFY2(model.invariantHolds(), qPrintable(describe(model)));
      QVERIFY2(wellFormed(model), qPrintable(describe(model)));
      QVERIFY2(model.rowCount() <= static_cast<int>(Known.size()), qPrintable(describe(model)));

      std::size_t placed = 0;
      for (const Row& row : model.rows()) placed += row.panels.size();
      QCOMPARE(model.panels().size(), placed);
      for (const PanelId& id : Known) QCOMPARE(model.contains(id), model.locate(id).has_value());
    }
  }

  void jsonRoundTripsStructure()
  {
    const LayoutModel model = build({{Tic, Spectrum}, {PeakMap}, {Features, Log}});
    const auto restored = LayoutModel::fromJson(model.toJson(), Known);
    QVERIFY(restored.has_value());
    QCOMPARE(*restored, model);
    QCOMPARE(restored->rows(), model.rows());
    QCOMPARE(restored->panels(), model.panels());
    QVERIFY(restored->invariantHolds());

    // An empty layout is representable, so it survives the trip as itself.
    const LayoutModel empty;
    const auto restoredEmpty = LayoutModel::fromJson(empty.toJson(), Known);
    QVERIFY(restoredEmpty.has_value());
    QVERIFY(restoredEmpty->isEmpty());
    QCOMPARE(*restoredEmpty, empty);

    // Row order and column order are both structure, not incidental.
    const LayoutModel swapped = build({{Spectrum, Tic}, {PeakMap}, {Features, Log}});
    QVERIFY(!(swapped == model));
    const auto restoredSwapped = LayoutModel::fromJson(swapped.toJson(), Known);
    QVERIFY(restoredSwapped.has_value());
    QCOMPARE(*restoredSwapped, swapped);
  }

  void fromJsonRejectsStructureTheModelCannotRepresent()
  {
    const LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});
    QVERIFY(LayoutModel::fromJson(model.toJson(), Known).has_value());  // the baseline is valid

    // A row exists only because panels are in it.
    QVERIFY(!LayoutModel::fromJson(withRows(model, {{Tic}, {}}), Known).has_value());
    // The invariant, arriving from disk.
    QVERIFY(!LayoutModel::fromJson(withRows(model, {{Tic, Spectrum, PeakMap}}), Known).has_value());
    // One panel in two places, across rows and within a row.
    QVERIFY(!LayoutModel::fromJson(withRows(model, {{Tic, Spectrum}, {Tic}}), Known).has_value());
    QVERIFY(!LayoutModel::fromJson(withRows(model, {{Tic, Tic}}), Known).has_value());
    // A panel id this build does not have.
    QVERIFY(!LayoutModel::fromJson(withRows(model, {{Tic}, {QStringLiteral("ghost")}}), Known)
               .has_value());
    QVERIFY(!LayoutModel::fromJson(withRows(model, {{QString{}}}), Known).has_value());
    // Every id is real, but one is not offered to *this* call: still rejected
    // rather than dropped, so the caller falls back to its default layout.
    QVERIFY(!LayoutModel::fromJson(model.toJson(), {Tic, PeakMap}).has_value());
    QVERIFY(!LayoutModel::fromJson(model.toJson(), {}).has_value());
  }

  void fromJsonRejectsBadVersionAndBadRows()
  {
    const LayoutModel model = build({{Tic, Spectrum}, {PeakMap}});
    const QJsonObject valid = model.toJson();
    QVERIFY(valid.value(VersionKey).isDouble());
    const int version = valid.value(VersionKey).toInt();

    // Any version but this build's: fall back rather than guess at a schema we
    // do not know. The ADR's "saved layouts reset once" depends on this.
    for (const QJsonValue& bad : {QJsonValue(version + 1), QJsonValue(version - 1),
                                  QJsonValue(QStringLiteral("1")), QJsonValue(QJsonValue::Null)})
    {
      QJsonObject json = valid;
      json[VersionKey] = bad;
      QVERIFY(!LayoutModel::fromJson(json, Known).has_value());
    }
    QJsonObject missingVersion = valid;
    missingVersion.remove(VersionKey);
    QVERIFY(!LayoutModel::fromJson(missingVersion, Known).has_value());

    for (const QJsonValue& notAnArray : {QJsonValue(QStringLiteral("rows")), QJsonValue(3),
                                         QJsonValue(QJsonObject{}), QJsonValue(QJsonValue::Null),
                                         QJsonValue(true)})
    {
      QJsonObject json = valid;
      json[RowsKey] = notAnArray;
      QVERIFY(!LayoutModel::fromJson(json, Known).has_value());
    }

    // A rows entry that is not a row in any encoding.
    QJsonObject notRows = valid;
    notRows[RowsKey] = QJsonArray{42};
    QVERIFY(!LayoutModel::fromJson(notRows, Known).has_value());

    QVERIFY(!LayoutModel::fromJson(QJsonObject{}, Known).has_value());
  }

  // Nothing fromJson accepts may be worse-formed than what the mutators build.
  void fromJsonNeverYieldsABrokenModel()
  {
    const std::vector<LayoutModel> models{
      LayoutModel{}, build({{Tic}}), build({{Tic, Spectrum}}),
      build({{Tic, Spectrum}, {PeakMap}, {Features, Log}}), build({{Log}, {Features}, {PeakMap}})};
    for (const LayoutModel& model : models)
    {
      const auto restored = LayoutModel::fromJson(model.toJson(), Known);
      QVERIFY2(restored.has_value(), qPrintable(describe(model)));
      QVERIFY2(restored->invariantHolds(), qPrintable(describe(*restored)));
      QVERIFY2(wellFormed(*restored), qPrintable(describe(*restored)));
      QCOMPARE(*restored, model);
    }
  }
};

int runLayoutModelTests(int argc, char** argv)
{
  LayoutModelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "LayoutModelTest.moc"
