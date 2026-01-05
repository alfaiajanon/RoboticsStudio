#include "SceneTreePanel.h"
#include <QVBoxLayout>
#include <QLabel>
#include "Simulation/MujocoContext.h"
#include "mujoco/mujoco.h"
#include "qheaderview.h"

SceneTreePanel::SceneTreePanel(QWidget* parent) : QWidget(parent){
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    treeWidget=new QTreeWidget();
    treeWidget->setAlternatingRowColors(true);
    treeWidget->header()->setVisible(false);
    

    layout->addWidget(treeWidget);
}



void SceneTreePanel::buildFromRoot(ComponentInstance* root) {
    treeWidget->clear();
    if (!root) return;
    addComponentNode(root, nullptr);
    treeWidget->expandAll();
}



void SceneTreePanel::addComponentNode(ComponentInstance* instance, QTreeWidgetItem* parentItem) {
    if (!instance) return;

    QTreeWidgetItem* item;
    if (parentItem) {
        item = new QTreeWidgetItem(parentItem);
    } else {
        item = new QTreeWidgetItem(treeWidget);
    }

    QString label = instance->name;
    if(instance->name.isEmpty()) {
        label = QString("Component_%1").arg(instance->uid);
    }
    
    item->setText(0, label);
    item->setData(0, Qt::UserRole, instance->uid);

    for (ComponentInstance* child : instance->children) {
        addComponentNode(child, item);
    }
}