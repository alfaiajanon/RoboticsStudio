#include "SceneTreePanel.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QTreeWidgetItemIterator>

/*
 * Initializes the Scene Tree UI and sets up the selection listener.
 * Binds the tree's selection event to emit the underlying component UID.
 */
SceneTreePanel::SceneTreePanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    treeWidget = new QTreeWidget();
    treeWidget->setAlternatingRowColors(true);
    treeWidget->header()->setVisible(false);
    
    layout->addWidget(treeWidget);

    connect(treeWidget, &QTreeWidget::itemSelectionChanged, this, [this]() {
        QList<QTreeWidgetItem*> selected = treeWidget->selectedItems();
        if (selected.isEmpty()) {
            emit componentSelected(-1); 
            return;
        }

        QTreeWidgetItem* item = selected.first();
        int uid = item->data(0, Qt::UserRole).toInt();
        
        emit componentSelected(uid); 
    });
}





/*
 * Populates the entire scene tree from a given root component.
 * Clears existing entries and automatically expands the tree for visibility.
 */
void SceneTreePanel::buildFromRoot(ComponentInstance* root) {
    treeWidget->clear();
    if (!root) return;
    
    addComponentNode(root, nullptr);
    treeWidget->expandAll();
}





/*
 * Recursively traverses the component hierarchy to build the UI tree.
 * Safely stores the component UID inside the Qt item's UserRole for later retrieval.
 */
void SceneTreePanel::addComponentNode(ComponentInstance* instance, QTreeWidgetItem* parentItem) {
    if (!instance) return;

    QTreeWidgetItem* item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(treeWidget);

    QString label = instance->name.isEmpty() ? QString("Component_%1").arg(instance->uid) : instance->name;
    
    item->setText(0, label);
    item->setData(0, Qt::UserRole, instance->uid);

    for (ComponentInstance* child : instance->children) {
        addComponentNode(child, item);
    }
}





/*
 * Programmatically selects a tree item based on its component UID.
 * Uses an iterator to find the matching node when commanded by the central mediator.
 */
void SceneTreePanel::highlightItem(int uid) {
    QTreeWidgetItemIterator it(treeWidget);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == uid) {
            treeWidget->setCurrentItem(*it);
            return;
        }
        ++it;
    }
    treeWidget->clearSelection();
}