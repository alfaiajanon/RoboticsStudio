#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <bits/stdc++.h>
#include "mujoco/mujoco.h"
#include "Simulation/Components/ComponentInstance.h"

using namespace std;

class SceneTreePanel : public QWidget{
    Q_OBJECT

    private:
        QTreeWidget* treeWidget;
        
    public:
        explicit SceneTreePanel(QWidget* parent = nullptr);        
        
        void buildFromRoot(ComponentInstance* root);
        void addComponentNode(ComponentInstance* instance, QTreeWidgetItem* parentItem);
        void highlightItem(int uid);
        
    signals:
        void componentSelected(int uid);
};