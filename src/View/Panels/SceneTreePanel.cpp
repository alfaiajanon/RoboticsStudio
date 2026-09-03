#include "SceneTreePanel.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QTreeWidgetItemIterator>
#include <QToolBox>
#include "Utils/Log.h"
#include "Application/Application.h"


SceneTreePanel::SceneTreePanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- Breadcrumb UI Setup ---
    breadcrumbScroll = new QScrollArea();
    breadcrumbScroll->setFixedHeight(36);
    breadcrumbScroll->setWidgetResizable(true);
    breadcrumbScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    breadcrumbScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    breadcrumbScroll->setFrameShape(QFrame::NoFrame);
    breadcrumbScroll->setStyleSheet("QScrollArea { background-color: transparent; border-bottom: 1px solid #333; }");
    breadcrumbScroll->hide();

    breadcrumbContainer = new QWidget();
    breadcrumbLayout = new QHBoxLayout(breadcrumbContainer);
    breadcrumbLayout->setContentsMargins(5, 0, 5, 0);
    breadcrumbLayout->setSpacing(4);
    breadcrumbLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    breadcrumbScroll->setWidget(breadcrumbContainer);

    // --- Trees Setup ---
    treeWidget = new QTreeWidget();
    treeWidget->setAlternatingRowColors(true);
    treeWidget->header()->setVisible(false);
    treeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    // UI Polish: StretchLastSection(true) forces the row background to span the full widget width
    treeWidget->header()->setStretchLastSection(true);
    treeWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    treeWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    unattachedTreeWidget = new QTreeWidget();
    unattachedTreeWidget->setAlternatingRowColors(true);
    unattachedTreeWidget->header()->setVisible(false);
    unattachedTreeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    unattachedTreeWidget->header()->setStretchLastSection(true);
    unattachedTreeWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    unattachedTreeWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget* robotTreeContainer = new QWidget();
    QVBoxLayout* rtLayout = new QVBoxLayout(robotTreeContainer);
    rtLayout->setContentsMargins(0, 0, 0, 0);
    rtLayout->setSpacing(0);
    rtLayout->addWidget(breadcrumbScroll);
    rtLayout->addWidget(treeWidget);

    toolBox = new QToolBox(this);
    toolBox->addItem(robotTreeContainer, "Robot tree");
    toolBox->addItem(unattachedTreeWidget, "Unattached parts");

    layout->addWidget(toolBox);

    // --- Signals ---
    auto handleSelection = [this](QTreeWidget* activeTree, QTreeWidget* otherTree) {
        QList<QTreeWidgetItem*> selected = activeTree->selectedItems();
        if (selected.isEmpty()) {
            int cur = Application::getInstance()->getEditor()->getCurrentSelectedUid();
            if(cur == -1){
                emit componentSelected(-1);
            }
            return;
        }

        otherTree->blockSignals(true);
        otherTree->clearSelection();
        otherTree->blockSignals(false);

        QTreeWidgetItem* item = selected.first();
        int uid = item->data(0, Qt::UserRole).toInt();

        emit componentSelected(uid);
    };

    connect(treeWidget, &QTreeWidget::itemSelectionChanged, this, [this, handleSelection]() {
        handleSelection(treeWidget, unattachedTreeWidget);
    });

    connect(unattachedTreeWidget, &QTreeWidget::itemSelectionChanged, this, [this, handleSelection]() {
        handleSelection(unattachedTreeWidget, treeWidget);
    });

    auto handleDoubleClick = [this](QTreeWidgetItem* item, int column) {
        if (!item) return;
        int uid = item->data(0, Qt::UserRole).toInt();
        this->drillTo(uid);
    };
    connect(treeWidget, &QTreeWidget::itemDoubleClicked, this, handleDoubleClick);
    connect(unattachedTreeWidget, &QTreeWidget::itemDoubleClicked, this, handleDoubleClick);
}


void SceneTreePanel::drillTo(int uid) {
    currentDrillUid = uid;
    buildFromProject(Application::getInstance()->getProject());
}


void SceneTreePanel::updateBreadcrumbs(Project* project) {
    QLayoutItem* child;
    while ((child = breadcrumbLayout->takeAt(0)) != nullptr) {
        if (child->widget()) delete child->widget();
        delete child;
    }

    if (!project) return;

    if (currentDrillUid == -2) {
        breadcrumbScroll->hide();
        return;
    }

    breadcrumbScroll->show();

    auto addCrumb = [this, project](const QString& text, int uid, bool isLast) {
        QPushButton* btn = new QPushButton(text);
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet("QPushButton { padding: 2px 4px; border-radius: 3px; background: transparent; }"
                           "QPushButton:hover { background: rgba(255,255,255,0.1); }");

        if (isLast) {
            QFont f = btn->font();
            f.setBold(true);
            btn->setFont(f);
            btn->setEnabled(false);
        } else {
            connect(btn, &QPushButton::clicked, this, [this, uid, project]() {
                this->drillTo(uid);
                emit componentSelected(uid == -2 ? -1 : uid);
            });
        }

        breadcrumbLayout->addWidget(btn);

        if (!isLast) {
            QLabel* arrow = new QLabel(">");
            arrow->setStyleSheet("color: gray;");
            breadcrumbLayout->addWidget(arrow);
        }
    };

    addCrumb("Home", -2, false);

    QList<ComponentInstance*> path;
    int curr = currentDrillUid;
    while (curr > 0) {
        ComponentInstance* c = project->getComponentByUid(curr);
        if (!c) break;
        path.prepend(c);
        curr = c->parentUid;
    }

    if (currentDrillUid == 0 || (path.size() > 0 && path.first()->parentUid == 0)) {
        QString rootName = project->getProjectData()["meta"].toObject()["name"].toString("Robot Origin");
        addCrumb(rootName, 0, currentDrillUid == 0);
    }

    for (int i = 0; i < path.size(); ++i) {
        addCrumb(path[i]->name, path[i]->uid, i == path.size() - 1);
    }

    breadcrumbLayout->addStretch();
}


void SceneTreePanel::buildFromProject(Project* project) {
    bool wasEmpty = (treeWidget->topLevelItemCount() == 0 && unattachedTreeWidget->topLevelItemCount() == 0);
    QSet<int> expandedUids;

    QTreeWidgetItemIterator itAttached(treeWidget);
    while (*itAttached) {
        if ((*itAttached)->isExpanded()) expandedUids.insert((*itAttached)->data(0, Qt::UserRole).toInt());
        ++itAttached;
    }
    QTreeWidgetItemIterator itUnattached(unattachedTreeWidget);
    while (*itUnattached) {
        if ((*itUnattached)->isExpanded()) expandedUids.insert((*itUnattached)->data(0, Qt::UserRole).toInt());
        ++itUnattached;
    }

    treeWidget->clear();
    unattachedTreeWidget->clear();

    if (!project) return;

    QMap<int, QTreeWidgetItem*> itemMap;

    QTreeWidgetItem* virtualRootItem = new QTreeWidgetItem();
    virtualRootItem->setText(0, project->getProjectData()["meta"].toObject()["name"].toString("Robot Origin"));
    virtualRootItem->setData(0, Qt::UserRole, 0);
    QFont boldFont = virtualRootItem->font(0);
    boldFont.setBold(true);
    virtualRootItem->setFont(0, boldFont);
    itemMap.insert(0, virtualRootItem);

    for (ComponentInstance* comp : project->getComponentMap().values()) {
        QTreeWidgetItem* item = new QTreeWidgetItem();
        item->setText(0, comp->name);
        item->setData(0, Qt::UserRole, comp->uid);
        itemMap.insert(comp->uid, item);
    }

    QTreeWidgetItem* memoryRoot = new QTreeWidgetItem();

    for (ComponentInstance* comp : project->getComponentMap().values()) {
        QTreeWidgetItem* item = itemMap.value(comp->uid);
        if (comp->parentUid == -1) {
            memoryRoot->addChild(item);
        } else if (itemMap.contains(comp->parentUid)) {
            itemMap.value(comp->parentUid)->addChild(item);
        } else {
            Log::error("Parent UID not found for component: " + QString::number(comp->uid));
            memoryRoot->addChild(item);
        }
    }
    memoryRoot->addChild(virtualRootItem);

    // Populate unattached parts regardless of drill state
    for (ComponentInstance* comp : project->getComponentMap().values()) {
        if (comp->parentUid == -1) {
            QTreeWidgetItem* item = itemMap.value(comp->uid);
            item = memoryRoot->takeChild(memoryRoot->indexOfChild(item));
            if (item) unattachedTreeWidget->addTopLevelItem(item);
        }
    }

    if (currentDrillUid == -2) {
        virtualRootItem = memoryRoot->takeChild(memoryRoot->indexOfChild(virtualRootItem));
        if (virtualRootItem) treeWidget->addTopLevelItem(virtualRootItem);
    } else {
        QTreeWidgetItem* drilledNode = itemMap.value(currentDrillUid);
        if (drilledNode) {
            while (drilledNode->childCount() > 0) {
                treeWidget->addTopLevelItem(drilledNode->takeChild(0));
            }
        }
    }

    delete memoryRoot;

    QTreeWidgetItemIterator restoreIt(treeWidget);
    while (*restoreIt) {
        if (wasEmpty || expandedUids.contains((*restoreIt)->data(0, Qt::UserRole).toInt()) || (*restoreIt)->data(0, Qt::UserRole).toInt() == 0) {
            (*restoreIt)->setExpanded(true);
        }
        ++restoreIt;
    }
    QTreeWidgetItemIterator restoreUnattachedIt(unattachedTreeWidget);
    while (*restoreUnattachedIt) {
        if (wasEmpty || expandedUids.contains((*restoreUnattachedIt)->data(0, Qt::UserRole).toInt())) {
            (*restoreUnattachedIt)->setExpanded(true);
        }
        ++restoreUnattachedIt;
    }

    updateBreadcrumbs(project);

    int selectedUid = Application::getInstance()->getEditor()->getCurrentSelectedUid();
    if(selectedUid != -1){
        highlightItem(selectedUid);
    }
}


void SceneTreePanel::highlightItem(int uid) {
    QTreeWidgetItemIterator itAttached(treeWidget);
    while (*itAttached) {
        if ((*itAttached)->data(0, Qt::UserRole).toInt() == uid) {
            treeWidget->setCurrentItem(*itAttached);
            return;
        }
        ++itAttached;
    }

    QTreeWidgetItemIterator itUnattached(unattachedTreeWidget);
    while (*itUnattached) {
        if ((*itUnattached)->data(0, Qt::UserRole).toInt() == uid) {
            unattachedTreeWidget->setCurrentItem(*itUnattached);
            return;
        }
        ++itUnattached;
    }

    treeWidget->clearSelection();
    unattachedTreeWidget->clearSelection();
}
