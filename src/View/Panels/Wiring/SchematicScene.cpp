#include "SchematicScene.h"

#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <algorithm>
#include <cmath>
#include "Document/Project.h"
#include "Document/PowerNetwork.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"
#include "View/Theme.h"


static constexpr qreal kHeader = 38;    // title + subtitle strip
static constexpr qreal kPitch = 18;     // pin spacing
static constexpr qreal kFooter = 10;
static constexpr qreal kDot = 4;        // pin dot radius
static constexpr qreal kStub = 14;      // wire leaves a pin straight out this far
static constexpr qreal kPartWidth = 170;
static constexpr qreal kBoardWidth = 190;
static constexpr qreal kColumnGap = 150;
static constexpr qreal kRowGap = 30;

static const QColor kError{0xe5, 0x53, 0x4b};
static const QColor kWarning{0xd2, 0x99, 0x22};



QColor SchematicScene::typeColor(const QString& type) {
    if (type == "power" || type == "power_out") return QColor(0xe5, 0x53, 0x4b);
    if (type == "ground") return QColor(0x8b, 0x8d, 0x93);
    if (type == "pwm") return QColor(0xe8, 0xa3, 0x3d);
    if (type == "digital") return QColor(0x4a, 0x8f, 0xe7);
    if (type == "analog") return QColor(0x57, 0xab, 0x5a);
    if (type.startsWith("i2c")) return QColor(0xb0, 0x83, 0xf0);
    if (type.startsWith("spi")) return QColor(0x39, 0xc5, 0xbb);
    if (type.startsWith("uart")) return QColor(0xd4, 0xc5, 0x48);
    if (type == "motor" || type == "motor_out") return QColor(0xb8, 0x7c, 0x4c);   // motor current: brown
    return QColor(0xc9, 0xcb, 0xd1);   // gpio: whatever it is wired to
}



#pragma region PinItem

PinItem::PinItem(PartBlockItem* block, const PinRef& ref, const QString& type, const QString& label, bool wireable)
    : QGraphicsItem(block), ref(ref), type(type), label(label), wireable(wireable) {
    setAcceptHoverEvents(true);
    setCursor(wireable ? Qt::CrossCursor : Qt::ArrowCursor);
    setZValue(2);
}

PartBlockItem* PinItem::block() const { return static_cast<PartBlockItem*>(parentItem()); }

QRectF PinItem::boundingRect() const {
    return QRectF(-kDot - 4, -kDot - 4, 2 * kDot + 8, 2 * kDot + 8);
}

void PinItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    p->setRenderHint(QPainter::Antialiasing);
    const QColor c = SchematicScene::typeColor(type);
    if (severity >= 0) {
        p->setPen(QPen(severity == 0 ? kError : kWarning, 1.6));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(QPointF(0, 0), kDot + 3, kDot + 3);
    }
    p->setPen(QPen(highlighted ? Theme::text : Theme::seam, highlighted ? 1.5 : 1));
    p->setBrush(wireable ? c : Theme::borderStrong);
    p->drawEllipse(QPointF(0, 0), highlighted ? kDot + 1 : kDot, highlighted ? kDot + 1 : kDot);
}

void PinItem::hoverEnterEvent(QGraphicsSceneHoverEvent* e) {
    if (auto* s = qobject_cast<SchematicScene*>(scene())) s->highlightNet(ref, true);
    QGraphicsItem::hoverEnterEvent(e);
}

void PinItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* e) {
    if (auto* s = qobject_cast<SchematicScene*>(scene())) s->highlightNet(ref, false);
    QGraphicsItem::hoverLeaveEvent(e);
}



#pragma region PartBlockItem

PartBlockItem::PartBlockItem(int uid, const QString& title, const QString& subtitle, bool board)
    : uid(uid), board(board), title(title), subtitle(subtitle) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    width = board ? kBoardWidth : kPartWidth;
    setZValue(1);
}

void PartBlockItem::layoutPins(bool pinsOnRight) {
    pinSideRight = pinsOnRight;
    int rows = 0;
    if (board) {
        const int leftCount = (pins.size() + 1) / 2;
        for (int i = 0; i < pins.size(); ++i) {
            const bool right = i >= leftCount;
            const int row = right ? i - leftCount : i;
            pins[i]->onRight = right;
            pins[i]->setPos(right ? width : 0, kHeader + kPitch / 2 + row * kPitch);
        }
        rows = leftCount;
    } else {
        for (int i = 0; i < pins.size(); ++i) {
            pins[i]->onRight = pinsOnRight;
            pins[i]->setPos(pinsOnRight ? width : 0, kHeader + kPitch / 2 + i * kPitch);
        }
        rows = int(pins.size());
    }
    prepareGeometryChange();
    height = kHeader + std::max(1, rows) * kPitch + kFooter;
    update();
}

QRectF PartBlockItem::boundingRect() const {
    return QRectF(-2, -2, width + 4, height + 4);
}

void PartBlockItem::paint(QPainter* p, const QStyleOptionGraphicsItem* option, QWidget*) {
    p->setRenderHint(QPainter::Antialiasing);
    const bool selected = option->state & QStyle::State_Selected;
    const QRectF r(0, 0, width, height);

    p->setPen(QPen(selected ? Theme::accent : Theme::borderStrong, selected ? 2 : 1));
    p->setBrush(Theme::surfaceAlt);
    p->drawRoundedRect(r, 5, 5);
    if (board) {   // a board: an accent strip on top
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::accent);
        p->drawRoundedRect(QRectF(0, 0, width, 4), 2, 2);
    }
    p->setPen(QPen(Theme::border, 1));
    p->drawLine(QPointF(1, kHeader - 4), QPointF(width - 1, kHeader - 4));

    QFont f = p->font();
    f.setPointSizeF(9.5);
    f.setBold(true);
    p->setFont(f);
    p->setPen(Theme::text);
    const qreal badge = severity >= 0 ? 18 : 0;
    p->drawText(QRectF(8, 5, width - 16 - badge, 16), Qt::AlignLeft | Qt::AlignVCenter,
                p->fontMetrics().elidedText(title, Qt::ElideRight, int(width - 16 - badge)));
    f.setBold(false);
    f.setPointSizeF(8);
    p->setFont(f);
    p->setPen(Theme::textFaint);
    p->drawText(QRectF(8, 19, width - 16, 13), Qt::AlignLeft | Qt::AlignVCenter,
                p->fontMetrics().elidedText(subtitle, Qt::ElideRight, int(width - 16)));

    if (severity >= 0) {
        const QPointF c(width - 13, 13);
        p->setPen(Qt::NoPen);
        p->setBrush(severity == 0 ? kError : kWarning);
        p->drawEllipse(c, 7, 7);
        p->setPen(QColor(0x10, 0x11, 0x13));
        f.setBold(true);
        p->setFont(f);
        p->drawText(QRectF(c.x() - 7, c.y() - 7, 14, 14), Qt::AlignCenter, "!");
        f.setBold(false);
    }

    f.setPointSizeF(8.5);
    p->setFont(f);
    for (PinItem* pin : pins) {
        const qreal y = pin->pos().y();
        p->setPen(pin->wireable ? Theme::textMuted : Theme::textFaint);
        if (pin->onRight)
            p->drawText(QRectF(width / 2, y - 8, width / 2 - 10, 16), Qt::AlignRight | Qt::AlignVCenter, pin->label);
        else
            p->drawText(QRectF(10, y - 8, width / 2 - 10, 16), Qt::AlignLeft | Qt::AlignVCenter, pin->label);
    }
}

QVariant PartBlockItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionChange) {   // snap to a 10 px grid
        const QPointF pnt = value.toPointF();
        return QPointF(std::round(pnt.x() / 10) * 10, std::round(pnt.y() / 10) * 10);
    }
    if (change == ItemPositionHasChanged) {
        if (auto* s = qobject_cast<SchematicScene*>(scene())) s->updateWires();
    }
    return QGraphicsItem::itemChange(change, value);
}



#pragma region WireItem

WireItem::WireItem(PinItem* a, PinItem* b, const Wire& wire, const QColor& color)
    : a(a), b(b), wire(wire), color(color) {
    setFlags(ItemIsSelectable);
    setZValue(0);
    updatePath();
}

bool WireItem::needsDetour() const {
    const QPointF pa = a->anchor(), pb = b->anchor();
    const bool aFaces = (pb.x() - pa.x()) * a->outward().x() > 2 * kStub;
    const bool bFaces = (pa.x() - pb.x()) * b->outward().x() > 2 * kStub;
    return !(aFaces && bFaces);
}

void WireItem::updatePath(int lane) {
    const QPointF p0 = a->anchor(), p4 = b->anchor();
    QPainterPath path(p0);
    if (!needsDetour()) {
        // Facing each other: out, across at the midpoint (shifted per lane so
        // parallel wires don't merge into one line), in.
        const QPointF p1 = p0 + a->outward() * kStub, p3 = p4 + b->outward() * kStub;
        const qreal span = std::abs(p3.x() - p1.x());
        const qreal shift = std::clamp(((lane % 9) - 4) * 8.0, -span / 2 + 4, span / 2 - 4);
        const qreal mid = (p1.x() + p3.x()) / 2 + shift;
        path.lineTo(p1);
        path.lineTo(mid, p1.y());
        path.lineTo(mid, p3.y());
        path.lineTo(p3);
    } else {
        // Facing away (e.g. a part left of the board, wired to a pin on the
        // board's right side): out a little further per lane, then around
        // both blocks above or below them, never across a block.
        const qreal out = kStub + 8 + 6 * lane;
        const QPointF p1 = p0 + a->outward() * out, p3 = p4 + b->outward() * out;
        const QRectF ra = a->block()->sceneBoundingRect(), rb = b->block()->sceneBoundingRect();
        const qreal top = std::min(ra.top(), rb.top()) - 16 - 6 * lane;
        const qreal bottom = std::max(ra.bottom(), rb.bottom()) + 16 + 6 * lane;
        const qreal y = (p0.y() - top) + (p4.y() - top) <= (bottom - p0.y()) + (bottom - p4.y()) ? top : bottom;
        path.lineTo(p1);
        path.lineTo(p1.x(), y);
        path.lineTo(p3.x(), y);
        path.lineTo(p3);
    }
    path.lineTo(p4);
    setPath(path);
}

void WireItem::paint(QPainter* p, const QStyleOptionGraphicsItem* option, QWidget*) {
    p->setRenderHint(QPainter::Antialiasing);
    const bool selected = option->state & QStyle::State_Selected;
    if (selected) {
        p->setPen(QPen(QColor(Theme::accent.red(), Theme::accent.green(), Theme::accent.blue(), 110), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPath(path());
    }
    p->setPen(QPen(highlighted ? color.lighter(140) : color, highlighted ? 2.6 : 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path());
}

QPainterPath WireItem::shape() const {
    QPainterPathStroker s;
    s.setWidth(8);
    return s.createStroke(path());
}



#pragma region SchematicScene

SchematicScene::SchematicScene(QObject* parent) : QGraphicsScene(parent) {}



void SchematicScene::rebuild(Project* project, const QList<RuleIssue>& issues) {
    dragFrom = nullptr;
    dragTarget = nullptr;
    dragLine = nullptr;
    clear();
    blocks.clear();
    pinItems.clear();
    wires.clear();
    netNames.clear();
    if (!project) return;

    nets = project->netlist().nets();
    netOf = Netlist::indexOf(nets);
    // Rail voltages at rest, for the power pins' tooltips.
    const PowerNetwork power = PowerNetwork::build(*project);
    const PowerNetwork::Solution atRest = power.solveStatic();

    // Design-rule findings per part and per pin (worst severity wins).
    QHash<int, QStringList> partIssues;
    QHash<int, int> partSeverity;
    QHash<PinRef, int> pinSeverity;
    for (const RuleIssue& i : issues) {
        if (i.uid < 0) continue;
        const int sev = i.isError() ? 0 : 1;
        partIssues[i.uid].append(i.message);
        partSeverity[i.uid] = std::min(partSeverity.value(i.uid, 1), sev);
        if (!i.pin.isEmpty()) {
            const PinRef r{i.uid, i.pin};
            pinSeverity[r] = std::min(pinSeverity.value(r, 1), sev);
        }
    }

    // Blocks: every part with typed pins; boards first in the centre column.
    QList<PartBlockItem*> boardBlocks, partBlocks;
    for (ComponentInstance* comp : project->getComponentMap()) {
        ComponentBlueprint* bp = comp->getBlueprint();
        if (!bp) continue;
        const bool isBoard = bp->board.present;
        QList<const PinDef*> shown;
        int internalCount = 0;
        for (const PinDef& p : bp->pins) {
            if (p.type.isEmpty()) continue;
            if (p.internal) { ++internalCount; continue; }
            shown.append(&p);
        }
        if (shown.isEmpty()) continue;

        QString subtitle = bp->meta.name;
        if (internalCount > 0) subtitle += QString(" · %1 pins on board").arg(internalCount);
        auto* block = new PartBlockItem(comp->uid, comp->name, subtitle, isBoard);
        block->issues = partIssues.value(comp->uid);
        block->severity = partIssues.contains(comp->uid) ? partSeverity.value(comp->uid) : -1;
        if (!block->issues.isEmpty()) block->setToolTip(block->issues.join("\n"));

        // "~" marks PWM pins on a board where not every pin has PWM (as on an Uno's silkscreen).
        bool allPwm = true;
        for (const PinDef* p : shown) if (p->type == "gpio" && !p->hasCap("pwm")) allPwm = false;

        for (const PinDef* p : shown) {
            QString label = p->id.toUpper();
            if (isBoard && p->type == "gpio" && p->hasCap("pwm") && !allPwm) label += " ~";
            auto* pin = new PinItem(block, PinRef{comp->uid, p->id}, p->type, label, true);
            pin->severity = pinSeverity.contains(pin->ref) ? pinSeverity.value(pin->ref) : -1;

            QStringList tip;
            tip << QString("%1 · %2").arg(comp->name, p->id.toUpper());
            if (!p->description.isEmpty()) tip << p->description;
            if (p->type == "gpio") tip << QString("Board pin %1 (%2)").arg(p->number).arg(p->caps.join(", "));
            else tip << "Type: " + p->type;
            if (p->voltageRange.second > 0) tip << QString("%1-%2 V").arg(p->voltageRange.first).arg(p->voltageRange.second);
            if (p->isPower()) {
                const PowerDef& pw = bp->power;
                for (const SupplyDef& s : pw.supplies) {
                    if (s.pin != p->id) continue;
                    if (s.kind == "usb") tip << QString("Source: the board's USB feed, %1 V (holds %2 A); dropped when something else feeds this rail").arg(s.voltage).arg(s.holdCurrent);
                    else if (s.kind == "battery") tip << "Source: battery (voltage from its charge, see the Inspector)";
                    else tip << "Source: bench supply (voltage and current limit in the Inspector)";
                }
                for (const RegulatorDef& r : pw.regulators) {
                    if (r.out == p->id) tip << QString("Source: %1 regulator from %2, %3 V, up to %4 A").arg(r.kind, r.in.toUpper()).arg(r.vOut).arg(r.iMax);
                    if (r.in == p->id) tip << QString("Feeds the %1 regulator (needs %2 V or more)").arg(r.out.toUpper()).arg(r.vOut + r.dropout);
                }
                if (const PowerLoadDef* l = pw.loadOn(p->id))
                    tip << QString("Draws %1 mA%2").arg(l->current * 1000.0, 0, 'g', 3).arg(l->minV > 0 ? QString(", works from %1 V").arg(l->minV) : QString());
                const int node = power.nodeOf(pin->ref);
                if (node >= 0 && project->getLevel() == ProjectLevel::Real)
                    tip << (power.nodes[node].live ? QString("Rail at rest: %1 V").arg(atRest.voltage[node], 0, 'f', 2) : QString("Rail: no source"));
            }
            const int net = netOf.value(pin->ref, -1);
            tip << (net >= 0 ? QString("Net with %1 pin(s)").arg(nets[net].pins.size()) : QString("Not wired"));
            for (const RuleIssue& i : issues) if (i.uid == comp->uid && i.pin == p->id) tip << "! " + i.message;
            pin->setToolTip(tip.join("\n"));

            block->addPin(pin);
            pinItems.insert(pin->ref, pin);
        }
        block->setFlag(QGraphicsItem::ItemIsMovable, editable);
        addItem(block);
        blocks.insert(comp->uid, block);
        (isBoard ? boardBlocks : partBlocks).append(block);
    }

    // Positions: the stored layout; blocks without one are placed below what
    // is already in their column (boards down the centre, parts alternating
    // left and right of it, by uid).
    const QHash<int, QPointF>& layout = project->layout();
    const qreal leftX = -kPartWidth - kColumnGap, rightX = kBoardWidth + kColumnGap;
    qreal yBoard = 0, yLeft = 0, yRight = 0;   // next free y per column
    auto claim = [&](PartBlockItem* b) {
        const qreal bottom = b->pos().y() + b->boundingRect().height() + kRowGap;
        const qreal cx = b->pos().x() + b->boundingRect().width() / 2;
        qreal& col = cx < 0 ? yLeft : cx > kBoardWidth ? yRight : yBoard;
        col = std::max(col, bottom);
    };
    for (PartBlockItem* b : boardBlocks) b->layoutPins(true);
    for (int i = 0; i < partBlocks.size(); ++i) partBlocks[i]->layoutPins(boardBlocks.isEmpty() || i % 2 == 0);
    QList<PartBlockItem*> unplaced;
    for (PartBlockItem* b : boardBlocks + partBlocks) {
        if (layout.contains(b->uid)) { b->setPos(layout.value(b->uid)); claim(b); }
        else unplaced.append(b);
    }
    int sideToggle = 0;
    for (PartBlockItem* b : unplaced) {
        if (b->board) {
            b->setPos(0, yBoard);
        } else if (boardBlocks.isEmpty()) {
            const int i = int(partBlocks.indexOf(b));
            b->setPos((i % 3) * (kPartWidth + kColumnGap), (i / 3) * 160.0);
        } else {
            const bool left = sideToggle++ % 2 == 0 ? yLeft <= yRight : yLeft < yRight;
            b->setPos(left ? leftX : rightX, left ? yLeft : yRight);
        }
        claim(b);
    }
    boardColumnX = boardBlocks.isEmpty() ? 1e9 : boardBlocks.first()->pos().x() + kBoardWidth / 2;
    relayoutPinSides();

    // Net names: the board pin on it ("D9", "5V", "GND"), else a number.
    for (const Net& n : nets) {
        QString name;
        for (const PinRef& r : n.pins) {
            if (PartBlockItem* b = blocks.value(r.uid, nullptr); b && b->board) { name = r.pin.toUpper(); break; }
        }
        netNames.insert(n.id, name.isEmpty() ? QString("Net %1").arg(n.id + 1) : name);
    }

    // Wires.
    for (const Wire& w : project->netlist().wires()) {
        PinItem* a = pinItems.value(w.a, nullptr);
        PinItem* b = pinItems.value(w.b, nullptr);
        if (!a || !b) continue;
        const int net = netOf.value(w.a, -1);
        auto* item = new WireItem(a, b, w, net >= 0 ? netColor(nets[net]) : typeColor(a->type));
        addItem(item);
        wires.append(item);
    }
    updateWires();
}



QColor SchematicScene::netColor(const Net& net) const {
    QString best;
    for (const PinRef& r : net.pins) {
        PinItem* p = pinItems.value(r, nullptr);
        if (!p) continue;
        if (p->type == "power" || p->type == "power_out") return typeColor("power");
        if (p->type == "ground") return typeColor("ground");
        if (best.isEmpty() && p->type != "gpio") best = p->type;
    }
    return typeColor(best.isEmpty() ? "gpio" : best);
}



void SchematicScene::relayoutPinSides() {
    for (PartBlockItem* b : blocks) {
        if (b->board || boardColumnX > 1e8) continue;
        const bool right = b->pos().x() + b->boundingRect().width() / 2 < boardColumnX;
        if (right != b->pinsOnRight()) b->layoutPins(right);
    }
    updateWires();
}



void SchematicScene::setEditable(bool on) {
    editable = on;
    for (PartBlockItem* b : blocks) b->setFlag(QGraphicsItem::ItemIsMovable, on);
}



void SchematicScene::updateWires() {
    int around = 0, across = 0;
    for (WireItem* w : wires) w->updatePath(w->needsDetour() ? around++ : across++);
}



void SchematicScene::clearHighlights() {
    for (PinItem* p : pinItems) if (p->highlighted) { p->highlighted = false; p->update(); }
    for (WireItem* w : wires) if (w->highlighted) { w->highlighted = false; w->update(); }
}



void SchematicScene::highlightNet(const PinRef& pin, bool on) {
    clearHighlights();
    if (!on) return;
    const int net = netOf.value(pin, -1);
    if (net < 0) {
        if (PinItem* p = pinItems.value(pin, nullptr)) { p->highlighted = true; p->update(); }
        return;
    }
    highlightNetIndex(net);
}



void SchematicScene::highlightNetIndex(int net) {
    clearHighlights();
    if (net < 0 || net >= nets.size()) return;
    for (const PinRef& r : nets[net].pins) {
        if (PinItem* p = pinItems.value(r, nullptr)) { p->highlighted = true; p->update(); }
    }
    for (WireItem* w : wires) {
        if (netOf.value(w->wire.a, -1) == net) { w->highlighted = true; w->update(); }
    }
}



void SchematicScene::selectBlock(int uid) {
    const QSignalBlocker blocker(this);
    clearSelection();
    if (PartBlockItem* b = blocks.value(uid, nullptr)) b->setSelected(true);
}



void SchematicScene::deleteSelectedWires() {
    if (!editable) return;
    QList<Wire> out;
    for (WireItem* w : wires) if (w->isSelected()) out.append(w->wire);
    if (!out.isEmpty()) emit wiresDeleteRequested(out);
}



PinItem* SchematicScene::pinAt(const QPointF& scenePos) const {
    // The closest pin within a few pixels (dots are small targets).
    PinItem* best = nullptr;
    qreal bestD = 9.0;
    for (PinItem* p : pinItems) {
        const qreal d = QLineF(p->anchor(), scenePos).length();
        if (d < bestD) { bestD = d; best = p; }
    }
    return best;
}



void SchematicScene::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        PinItem* pin = pinAt(e->scenePos());
        if (pin && editable && pin->wireable) {
            dragFrom = pin;
            dragLine = new QGraphicsPathItem();
            dragLine->setPen(QPen(typeColor(pin->type), 1.6, Qt::DashLine, Qt::RoundCap));
            dragLine->setZValue(3);
            addItem(dragLine);
            e->accept();
            return;
        }
        pressPositions.clear();
        for (PartBlockItem* b : blocks) pressPositions.insert(b->uid, b->pos());
        QGraphicsScene::mousePressEvent(e);
        QGraphicsItem* item = itemAt(e->scenePos(), QTransform());
        while (item && item->parentItem()) item = item->parentItem();
        if (auto* block = dynamic_cast<PartBlockItem*>(item)) emit blockClicked(block->uid);
        return;
    }
    QGraphicsScene::mousePressEvent(e);
}



void SchematicScene::mouseMoveEvent(QGraphicsSceneMouseEvent* e) {
    if (dragFrom) {
        PinItem* target = pinAt(e->scenePos());
        if (target == dragFrom || (target && !target->wireable)) target = nullptr;
        if (target != dragTarget) {
            if (dragTarget) { dragTarget->highlighted = false; dragTarget->update(); }
            dragTarget = target;
            if (dragTarget) { dragTarget->highlighted = true; dragTarget->update(); }
        }
        const QPointF a = dragFrom->anchor();
        const QPointF b = dragTarget ? dragTarget->anchor() : e->scenePos();
        QPainterPath path(a);
        const QPointF a1 = a + dragFrom->outward() * kStub;
        path.lineTo(a1);
        path.lineTo((a1.x() + b.x()) / 2, a1.y());
        path.lineTo((a1.x() + b.x()) / 2, b.y());
        path.lineTo(b);
        dragLine->setPath(path);
        e->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(e);
}



void SchematicScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* e) {
    if (dragFrom) {
        PinItem* from = dragFrom;
        PinItem* to = dragTarget;
        dragFrom = nullptr;
        dragTarget = nullptr;
        removeItem(dragLine);
        delete dragLine;
        dragLine = nullptr;
        if (to) {
            to->highlighted = false;
            to->update();
            emit wireRequested(Wire{from->ref, to->ref});
        } else {
            emit status("Drop the wire on another pin");
        }
        e->accept();
        return;
    }
    QGraphicsScene::mouseReleaseEvent(e);
    if (e->button() != Qt::LeftButton || pressPositions.isEmpty()) return;
    QHash<int, QPointF> from, to;
    for (PartBlockItem* b : blocks) {
        const QPointF before = pressPositions.value(b->uid, b->pos());
        if (before != b->pos()) { from.insert(b->uid, before); to.insert(b->uid, b->pos()); }
    }
    pressPositions.clear();
    if (!to.isEmpty()) {
        relayoutPinSides();
        if (editable) emit blocksMoved(from, to);
    }
}



void SchematicScene::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelectedWires();
        e->accept();
        return;
    }
    QGraphicsScene::keyPressEvent(e);
}
