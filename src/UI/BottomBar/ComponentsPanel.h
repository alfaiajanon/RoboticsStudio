#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QPoint>

class DraggableComponentWidget : public QWidget {
    Q_OBJECT

    public:
        explicit DraggableComponentWidget(const QString& modelId, const QString& modelName, const QString& iconPath, QWidget* parent = nullptr);
        QString getModelId() const;
        QString getModelName() const;

    protected:
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;

    private:
        QString modelId;
        QString modelName;
        QPoint dragStartPosition;
};








class ComponentsPanel : public QWidget {
    Q_OBJECT

    public:
        explicit ComponentsPanel(QWidget* parent = nullptr);
        
    public slots:
        void loadCatalog(const QString& catalogPath);

    private slots:
        void filterComponents(const QString& searchText);

    private:
        QLineEdit* searchBox;
        QVBoxLayout* scrollLayout;
        
        struct CategoryBlock {
            QString name;
            QStringList keys;
            QWidget* container;
            QList<DraggableComponentWidget*> items;
        };
        
        QList<CategoryBlock> categories;
        
        void clearPanel();
};