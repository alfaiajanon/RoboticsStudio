#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <bits/stdc++.h>
#include "mujoco/mujoco.h"
#include "Simulation/Components/Component.h"

using namespace std;

class SceneTreePanel : public QWidget{
    Q_OBJECT

    private:
        QTreeWidget* treeWidget;
        void addComponentNode(ComponentInstance* instance, QTreeWidgetItem* parentItem);

    public:
        explicit SceneTreePanel(QWidget* parent = nullptr);

        void buildFromRoot(ComponentInstance* root);

};