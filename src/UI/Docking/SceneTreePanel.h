#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <bits/stdc++.h>
#include "mujoco/mujoco.h"
#include "Core/Project.h"
#include "Simulation/Components/ComponentInstance.h"

using namespace std;

class SceneTreePanel : public QWidget{
    Q_OBJECT

    private:
        QTreeWidget *treeWidget, *unattachedTreeWidget;

    public:
        explicit SceneTreePanel(QWidget* parent = nullptr);        
        
        void buildFromProject(Project* project);
        void highlightItem(int uid);
        
    signals:
        void componentSelected(int uid);
};