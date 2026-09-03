#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QToolBox>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QPushButton>
#include <bits/stdc++.h>
#include "mujoco/mujoco.h"
#include "Document/Project.h"
#include "Document/Components/ComponentInstance.h"

using namespace std;

class SceneTreePanel : public QWidget{
    Q_OBJECT

    private:
        QTreeWidget *treeWidget, *unattachedTreeWidget;
        QToolBox* toolBox;

        // Breadcrumb UI
        QScrollArea* breadcrumbScroll;
        QWidget* breadcrumbContainer;
        QHBoxLayout* breadcrumbLayout;

        // -2 indicates the absolute home/root view
        int currentDrillUid = -2;

        void updateBreadcrumbs(Project* project);
        void drillTo(int uid);

    public:
        explicit SceneTreePanel(QWidget* parent = nullptr);

        void buildFromProject(Project* project);
        void highlightItem(int uid);

    signals:
        void componentSelected(int uid);
};
