#include "SceneTreePanel.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QTreeWidgetItemIterator>
#include "Core/Application.h"
#include <QToolBox>
#include "Core/Log.h"

/*
 * Initializes the Scene Tree UI and sets up the selection listener.
 * Binds the tree's selection event to emit the underlying component UID.
 */




SceneTreePanel::SceneTreePanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QToolBox* toolBox = new QToolBox(this);

    treeWidget = new QTreeWidget();
    treeWidget->setAlternatingRowColors(true);
    treeWidget->header()->setVisible(false);

    unattachedTreeWidget = new QTreeWidget();
    unattachedTreeWidget->setAlternatingRowColors(true);
    unattachedTreeWidget->header()->setVisible(false);

    toolBox->addItem(treeWidget, "Robot tree");
    toolBox->addItem(unattachedTreeWidget, "Unattached parts");

    layout->addWidget(toolBox);

    auto handleSelection = [this](QTreeWidget* activeTree, QTreeWidget* otherTree) {
        QList<QTreeWidgetItem*> selected = activeTree->selectedItems();
        if (selected.isEmpty()) {
            int cur= Application::getInstance()->getEditor()->getCurrentSelectedUid();
            if(cur==-1){
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
}




/*
 * Rebuilds both scene trees while preserving expansion states.
 * Automatically expands all nodes if the tree was previously empty.
 */




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

    QTreeWidgetItem* virtualRootItem = new QTreeWidgetItem(treeWidget);
    virtualRootItem->setText(0, project->getProjectData()["meta"].toObject()["name"].toString("Robot Origin")); 
    virtualRootItem->setData(0, Qt::UserRole, 0);
    
    QFont boldFont = virtualRootItem->font(0);
    boldFont.setBold(true);
    virtualRootItem->setFont(0, boldFont);

    QMap<int, QTreeWidgetItem*> itemMap;
    itemMap.insert(0, virtualRootItem);

    for (ComponentInstance* comp : project->getComponentMap().values()) {
        QTreeWidgetItem* item = new QTreeWidgetItem();
        item->setText(0, comp->name);
        item->setData(0, Qt::UserRole, comp->uid);
        itemMap.insert(comp->uid, item);
    }

    for (ComponentInstance* comp : project->getComponentMap().values()) {
        QTreeWidgetItem* item = itemMap.value(comp->uid);

        if (comp->parentUid == -1) {
            unattachedTreeWidget->addTopLevelItem(item);
        } else if (itemMap.contains(comp->parentUid)) {
            itemMap.value(comp->parentUid)->addChild(item);
        } else {
            Log::error("Parent UID not found for component: " + QString::number(comp->uid));
        }
    }

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

    int currentSelectedUid = Application::getInstance()->getEditor()->getCurrentSelectedUid();
    if(currentSelectedUid != -1){
        emit componentSelected(currentSelectedUid);
    }
}




/*
 * Selects a tree item based on its component UID.
 * Searches both the attached robot tree and the unattached parts list.
 */
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