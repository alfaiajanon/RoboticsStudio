#pragma once

#include <QWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QMap>
#include "qcustomplot/qcustomplot.h"
#include "Core/Project.h"

class PlotPanel : public QWidget {
    Q_OBJECT

    private:
        QCustomPlot* customPlot;
        QTimer* updateTimer;
        
        QScrollArea* targetListArea;
        QWidget* targetListWidget;
        QVBoxLayout* targetListLayout;

        QMap<QString, QCPGraph*> targetGraphs; 
        QMap<QString, int> targetCursors;      
        
        double displayWindowSeconds = 7.0; 
        double memoryWindowSeconds = 21.0;

        // Helper to consistently generate the MuJoCo-style ID
        QString getTargetId(const PlotTarget& target) const {
            return QString("comp_%1_%2").arg(target.compUid).arg(target.ioKey);
        }

    public:
        explicit PlotPanel(QWidget* parent = nullptr);

        void addTarget(const PlotTarget& target);
        void removeTarget(const QString& targetId);
        void clearAllGraphs();

    private slots:
        void onUpdateTimer();
        void buildTargetRowUI(const PlotTarget& target);
        void showAddTargetDialog();
};