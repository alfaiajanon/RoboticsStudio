#pragma once

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHash>
#include <QPointF>
#include <QStringList>
#include "Document/Netlist.h"
#include "Document/DesignRules.h"

class Project;
class PartBlockItem;
class WireItem;


// One pin on a schematic block: a dot coloured by the pin's type, with its
// label drawn beside it by the block. Dragging from it draws a wire.
class PinItem : public QGraphicsItem {
    public:
        PinItem(PartBlockItem* block, const PinRef& ref, const QString& type, const QString& label, bool wireable);

        PinRef ref;
        QString type;
        QString label;
        bool wireable;          // false for a board's internal (hard-wired) pins
        bool onRight = true;    // label side: the dot sits on the block's left or right edge
        int severity = -1;      // -1 none, 0 error, 1 warning (design-rule badge)
        bool highlighted = false;

        PartBlockItem* block() const;
        QPointF anchor() const { return scenePos(); }   // where wires attach
        QPointF outward() const { return QPointF(onRight ? 1 : -1, 0); }

        QRectF boundingRect() const override;
        void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override;

    protected:
        void hoverEnterEvent(QGraphicsSceneHoverEvent* e) override;
        void hoverLeaveEvent(QGraphicsSceneHoverEvent* e) override;
};


// A part (or board) on the schematic: a titled box with its typed pins.
// Boards list their pins down both sides in header order; other parts put
// their pins on the side facing the board.
class PartBlockItem : public QGraphicsItem {
    public:
        PartBlockItem(int uid, const QString& title, const QString& subtitle, bool board);

        int uid;
        bool board;
        QString title, subtitle;
        QList<PinItem*> pins;
        QStringList issues;     // design-rule messages for this part
        int severity = -1;      // worst issue: 0 error, 1 warning, -1 none

        void addPin(PinItem* pin) { pins.append(pin); }
        // Places the pins: both sides for a board, else the side given.
        void layoutPins(bool pinsOnRight);
        bool pinsOnRight() const { return pinSideRight; }

        QRectF boundingRect() const override;
        void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override;

    protected:
        QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

    private:
        qreal width = 170, height = 60;
        bool pinSideRight = true;
};


// A wire between two pins, routed orthogonally; coloured by its net's kind.
class WireItem : public QGraphicsPathItem {
    public:
        WireItem(PinItem* a, PinItem* b, const Wire& wire, const QColor& color);

        PinItem* a;
        PinItem* b;
        Wire wire;
        QColor color;
        bool highlighted = false;

        // lane: this wire's slot among the wires that go around blocks (keeps them apart)
        void updatePath(int lane = 0);
        bool needsDetour() const;   // the pins don't face each other: route around the blocks
        void paint(QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* w) override;
        QPainterPath shape() const override;
};


/*
 * The wiring page's scene, rebuilt from the project on every refresh.
 * Interaction: drag pin -> pin to add a wire (wireRequested), select wires
 * and press Delete (wiresDeleteRequested), drag blocks (blocksMoved when the
 * drag ends), hover a pin to light up its net. Edits go out as signals: the
 * page turns them into undoable commands.
 */
class SchematicScene : public QGraphicsScene {
    Q_OBJECT

    public:
        explicit SchematicScene(QObject* parent = nullptr);

        void rebuild(Project* project, const QList<RuleIssue>& issues);
        void setEditable(bool on);   // off while the simulation runs
        bool isEditable() const { return editable; }

        PartBlockItem* blockOf(int uid) const { return blocks.value(uid, nullptr); }
        PinItem* pinOf(const PinRef& r) const { return pinItems.value(r, nullptr); }
        void highlightNet(const PinRef& pin, bool on);
        void highlightNetIndex(int net);   // -1 clears
        void selectBlock(int uid);
        void updateWires();
        void deleteSelectedWires();

        // Pin side of free-standing parts follows where they are relative to the board column.
        void relayoutPinSides();
        QString netName(int net) const { return netNames.value(net); }
        const QList<Net>& currentNets() const { return nets; }

        static QColor typeColor(const QString& type);

    signals:
        void wireRequested(const Wire& w);
        void wiresDeleteRequested(const QList<Wire>& wires);
        void blocksMoved(const QHash<int, QPointF>& from, const QHash<int, QPointF>& to);
        void blockClicked(int uid);
        void status(const QString& message);

    protected:
        void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
        void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
        void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
        void keyPressEvent(QKeyEvent* e) override;

    private:
        PinItem* pinAt(const QPointF& scenePos) const;
        QColor netColor(const Net& net) const;
        void clearHighlights();

        bool editable = true;
        QHash<int, PartBlockItem*> blocks;
        QHash<PinRef, PinItem*> pinItems;
        QList<WireItem*> wires;
        QList<Net> nets;
        QHash<PinRef, int> netOf;
        QHash<int, QString> netNames;
        qreal boardColumnX = 0;

        // pin drag
        PinItem* dragFrom = nullptr;
        QGraphicsPathItem* dragLine = nullptr;
        PinItem* dragTarget = nullptr;
        // block drag
        QHash<int, QPointF> pressPositions;
};
