#include "WiringPage.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>
#include "Application/Application.h"
#include "Commands/WiringCommands.h"
#include "Document/DesignRules.h"
#include "Document/Project.h"
#include "Document/Components/ComponentInstance.h"
#include "SchematicScene.h"
#include "View/Theme.h"


static const QString kHint = "Drag from a pin to another pin to wire them · select a wire and press Delete · "
                             "wheel zooms, right-drag pans";



#pragma region SchematicView

SchematicView::SchematicView(QGraphicsScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSceneRect(-5000, -5000, 10000, 10000);
}

void SchematicView::fitAll() {
    const QRectF r = scene()->itemsBoundingRect();
    if (r.isEmpty()) return;
    fitInView(r.adjusted(-40, -40, 40, 40), Qt::KeepAspectRatio);
    if (transform().m11() > 1.4) setTransform(QTransform::fromScale(1.4, 1.4));   // a lone block: don't blow it up
    centerOn(r.center());
}

void SchematicView::wheelEvent(QWheelEvent* e) {
    const qreal factor = std::pow(1.0015, e->angleDelta().y());
    const qreal next = transform().m11() * factor;
    if (next < 0.2 || next > 4.0) return;
    scale(factor, factor);
}

void SchematicView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        panning = true;
        lastPan = e->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}

void SchematicView::mouseMoveEvent(QMouseEvent* e) {
    if (panning) {
        const QPoint d = e->pos() - lastPan;
        lastPan = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void SchematicView::mouseReleaseEvent(QMouseEvent* e) {
    if (panning && (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton)) {
        panning = false;
        viewport()->unsetCursor();
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void SchematicView::drawBackground(QPainter* p, const QRectF& rect) {
    p->fillRect(rect, Theme::surface);
    const qreal step = 20;
    if (transform().m11() * step < 6) return;   // too dense to help
    p->setPen(QPen(Theme::border, 0));
    const qreal x0 = std::floor(rect.left() / step) * step, y0 = std::floor(rect.top() / step) * step;
    QVector<QPointF> dots;
    for (qreal x = x0; x < rect.right(); x += step)
        for (qreal y = y0; y < rect.bottom(); y += step) dots.append(QPointF(x, y));
    p->drawPoints(dots.data(), int(dots.size()));
}



#pragma region WiringPage

WiringPage::WiringPage(QWidget* parent) : QWidget(parent) {
    scene = new SchematicScene(this);
    view = new SchematicView(scene, this);

    // Header strip: what this is, a summary of the checks, Fit.
    QWidget* header = new QWidget(this);
    header->setObjectName("wiringHeader");
    header->setAttribute(Qt::WA_StyledBackground);
    header->setStyleSheet(QString("#wiringHeader { background: %1; border-bottom: 1px solid %2; }")
                              .arg(Theme::surfaceAlt.name(), Theme::seam.name()));
    QHBoxLayout* hl = new QHBoxLayout(header);
    hl->setContentsMargins(10, 4, 6, 4);
    title = new QLabel("Wiring", header);
    title->setStyleSheet("font-weight: bold;");
    summary = new QLabel(header);
    QPushButton* fitBtn = new QPushButton("Fit", header);
    fitBtn->setToolTip("Show the whole schematic");
    hl->addWidget(title);
    hl->addSpacing(12);
    hl->addWidget(summary);
    hl->addStretch();
    hl->addWidget(fitBtn);

    hint = new QLabel(kHint, this);
    hint->setStyleSheet(QString("color: %1; padding: 3px 10px;").arg(Theme::textFaint.name()));

    QWidget* canvas = new QWidget(this);
    QVBoxLayout* cl = new QVBoxLayout(canvas);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    cl->addWidget(header);
    cl->addWidget(view, 1);
    cl->addWidget(hint);

    // Side: checks above, nets below.
    QWidget* side = new QWidget(this);
    QVBoxLayout* sl = new QVBoxLayout(side);
    sl->setContentsMargins(6, 6, 6, 6);
    sl->setSpacing(4);
    QLabel* checksLabel = new QLabel("Checks", side);
    checksLabel->setStyleSheet("font-weight: bold;");
    issuesList = new QListWidget(side);
    issuesList->setWordWrap(true);
    issuesList->setToolTip("What a real board would do wrong with this wiring. Click to show the part.");
    QLabel* netsLabel = new QLabel("Nets", side);
    netsLabel->setStyleSheet("font-weight: bold;");
    netsTree = new QTreeWidget(side);
    netsTree->setHeaderHidden(true);
    netsTree->setToolTip("Pins joined by wires. Click a net to light it up.");
    sl->addWidget(checksLabel);
    sl->addWidget(issuesList, 3);
    sl->addWidget(netsLabel);
    sl->addWidget(netsTree, 2);

    QSplitter* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(canvas);
    split->addWidget(side);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({9999, 300});
    split->setCollapsible(0, false);

    QHBoxLayout* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(split);

    // Edits become undoable commands; the refresh after each rebuilds the page.
    connect(scene, &SchematicScene::wireRequested, this, [](const Wire& w) { Commands::addWire(w); });
    connect(scene, &SchematicScene::wiresDeleteRequested, this, [](const QList<Wire>& ws) { Commands::removeWires(ws); });
    connect(scene, &SchematicScene::blocksMoved, this, [](const QHash<int, QPointF>& from, const QHash<int, QPointF>& to) {
        Commands::moveSchematicBlocks(from, to);
    });
    connect(scene, &SchematicScene::blockClicked, this, &WiringPage::componentSelected);
    connect(scene, &SchematicScene::status, this, [this](const QString& m) { hint->setText(m + "  ·  " + kHint); });
    connect(fitBtn, &QPushButton::clicked, view, &SchematicView::fitAll);

    connect(issuesList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const int uid = item->data(Qt::UserRole).toInt();
        if (uid < 0) return;
        emit componentSelected(uid);
        focusOn(uid, item->data(Qt::UserRole + 1).toString());
    });
    connect(netsTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        while (item->parent()) item = item->parent();
        scene->highlightNetIndex(item->data(0, Qt::UserRole).toInt());
    });
}



void WiringPage::refresh() {
    Project* project = Application::getInstance()->getProject();
    const QList<RuleIssue> issues = project ? DesignRules::check(*project) : QList<RuleIssue>();
    scene->rebuild(project, issues);

    // Header: the board(s) and a count of the findings.
    int errors = 0, warnings = 0;
    for (const RuleIssue& i : issues) (i.isError() ? errors : warnings)++;
    QStringList boards;
    if (project) for (ComponentInstance* b : project->boards()) boards << b->name;
    title->setText(boards.isEmpty() ? "Wiring · no board" : "Wiring · " + boards.join(", "));
    summary->setText(errors + warnings == 0 ? QString("✓ no problems found")
                                            : QString("%1 error(s), %2 warning(s)").arg(errors).arg(warnings));
    summary->setStyleSheet(QString("color: %1;").arg(errors ? "#e5534b" : warnings ? "#d29922" : Theme::textMuted.name()));

    issuesList->clear();
    for (const RuleIssue& i : issues) {
        auto* item = new QListWidgetItem((i.isError() ? "●  " : "▲  ") + i.message, issuesList);
        item->setForeground(i.isError() ? QColor(0xe5, 0x53, 0x4b) : QColor(0xd2, 0x99, 0x22));
        item->setData(Qt::UserRole, i.uid);
        item->setData(Qt::UserRole + 1, i.pin);
    }
    if (issues.isEmpty()) {
        auto* item = new QListWidgetItem("No problems found.", issuesList);
        item->setForeground(Theme::textMuted);
        item->setData(Qt::UserRole, -1);
    }

    netsTree->clear();
    if (project) {
        for (const Net& n : scene->currentNets()) {
            auto* top = new QTreeWidgetItem(netsTree, {QString("%1  (%2 pins)").arg(scene->netName(n.id)).arg(n.pins.size())});
            top->setData(0, Qt::UserRole, n.id);
            for (const PinRef& r : n.pins) {
                ComponentInstance* c = project->getComponentByUid(r.uid);
                new QTreeWidgetItem(top, {QString("%1 · %2").arg(c ? c->name : QString::number(r.uid), r.pin.toUpper())});
            }
        }
    }

    const QString path = project ? project->getProjectPath() : QString();
    if (path != shownProject) {
        shownProject = path;
        needsFit = true;
    }
    fitIfNeeded();
}



// Fitting needs the view's real size: done once the page is shown.
void WiringPage::fitIfNeeded() {
    if (!needsFit || !isVisible() || view->width() < 50) return;
    needsFit = false;
    view->fitAll();
}

void WiringPage::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    fitIfNeeded();
}

void WiringPage::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    fitIfNeeded();
}



void WiringPage::setEditable(bool editable) {
    scene->setEditable(editable);
    hint->setText(editable ? kHint : "Read-only while the simulation runs");
}



void WiringPage::setSelectedUid(int uid) {
    scene->selectBlock(uid);
}



void WiringPage::focusOn(int uid, const QString& pin) {
    if (PartBlockItem* b = scene->blockOf(uid)) {
        view->centerOn(b);
        scene->selectBlock(uid);
    }
    if (!pin.isEmpty()) scene->highlightNet(PinRef{uid, pin}, true);
}
