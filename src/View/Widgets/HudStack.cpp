#include "HudStack.h"

#include <QStackedWidget>
#include <QButtonGroup>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QTimer>

static constexpr int HUD_MARGIN = 15;
static constexpr int SWITCH_BTN_SIZE = 40;   // every switch button is a uniform square
static constexpr int SWITCH_ICON_SIZE = 20;
static constexpr int SWITCH_PAD = 5;         // panel inset == gap between buttons



HudStack::HudStack(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setMinimumSize(1, 1);

    stack = new QStackedWidget(this);

    // Invisible holder: the switch panel and the controls float detached
    // (the global dark.qss paints every QWidget, so the holder and the
    // control containers are explicitly transparent)
    masterHud = new QWidget(this);
    masterHud->setObjectName("masterHud");
    masterHud->setStyleSheet("#masterHud, #hudControl { background: transparent; }");

    QWidget* switchPanel = new QWidget(masterHud);
    switchPanel->setObjectName("switchPanel");
    switchPanel->setAttribute(Qt::WA_StyledBackground);
    switchPanel->setStyleSheet(R"(
        #switchPanel { background-color: #27282b; border: 1px solid #101113; border-radius: 6px; }
        QToolButton { background: transparent; border: none; border-radius: 4px; padding: 0px; }
        QToolButton:hover { background-color: #313236; }
        QToolButton:checked { background-color: #2a3546; }
    )");

    hudLayout = new QHBoxLayout(masterHud);
    hudLayout->setContentsMargins(0, 0, 0, 0);
    hudLayout->setSpacing(10);

    switchLayout = new QVBoxLayout(switchPanel);
    switchLayout->setContentsMargins(SWITCH_PAD, SWITCH_PAD, SWITCH_PAD, SWITCH_PAD);
    switchLayout->setSpacing(SWITCH_PAD);
    hudLayout->addWidget(switchPanel, 0, Qt::AlignBottom);

    switchGroup = new QButtonGroup(this);
    switchGroup->setExclusive(true);

    connect(switchGroup, &QButtonGroup::idClicked, this, [this](int id) {
        setCurrentPage(stack->widget(id));
    });
}



int HudStack::addPage(QWidget* page, const QIcon& icon, const QString& tooltip) {
    int idx = stack->addWidget(page);

    QToolButton* btn = new QToolButton();
    btn->setIcon(icon);
    btn->setIconSize(QSize(SWITCH_ICON_SIZE, SWITCH_ICON_SIZE));
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setFixedSize(SWITCH_BTN_SIZE, SWITCH_BTN_SIZE);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    switchGroup->addButton(btn, idx);
    switchLayout->addWidget(btn);

    if (idx == 0) btn->setChecked(true);

    placeHud();
    return idx;
}



void HudStack::setCurrentPage(QWidget* page) {
    int idx = stack->indexOf(page);
    if (idx < 0) return;

    if (QAbstractButton* btn = switchGroup->button(idx)) btn->setChecked(true);
    if (stack->currentIndex() == idx) return;

    stack->setCurrentIndex(idx);
    masterHud->raise();
    emit currentPageChanged(page);
}



QWidget* HudStack::currentPage() const {
    return stack->currentWidget();
}



void HudStack::addHudControl(QWidget* w) {
    w->setParent(masterHud);
    if (w->objectName().isEmpty()) w->setObjectName("hudControl");
    hudLayout->addWidget(w, 0, Qt::AlignBottom);
    // A control that changes its own size (e.g. a button shown/hidden inside
    // it) must re-anchor and re-mask the HUD.
    w->installEventFilter(this);
    placeHud();
}



bool HudStack::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LayoutRequest)
        QTimer::singleShot(0, this, &HudStack::placeHud);
    return QWidget::eventFilter(watched, event);
}



void HudStack::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    stack->setGeometry(rect());
    placeHud();
}



void HudStack::placeHud() {
    masterHud->adjustSize();
    // Only the visible pieces take input; the gaps pass clicks to the page
    masterHud->setMask(masterHud->childrenRegion());
    masterHud->move(HUD_MARGIN, height() - masterHud->height() - HUD_MARGIN);
    masterHud->raise();
}
