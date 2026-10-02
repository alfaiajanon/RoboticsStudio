#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QPoint>
#include <QPushButton>

class DraggableComponentWidget : public QWidget {
    Q_OBJECT

    public:
        explicit DraggableComponentWidget(const QString& modelId, const QString& modelName, const QString& iconPath, QWidget* parent = nullptr);
        QString getModelId() const;
        QString getModelName() const;
        void setSelected(bool selected);

    signals:
        void clicked(const QString& modelId);

    protected:
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;

    private:
        QString modelId;
        QString modelName;
        QPoint dragStartPosition;
        bool dragged = false;
};








class ComponentsPanel : public QWidget {
    Q_OBJECT

    public:
        explicit ComponentsPanel(QWidget* parent = nullptr);

    public slots:
        void loadCatalog();

    private slots:
        void filterComponents(const QString& searchText);

    private:
        QLineEdit* searchBox;
        QPushButton* addButton;
        QPushButton* openButton;
        QString selectedModelId;
        QVBoxLayout* scrollLayout;

        struct CategoryBlock {
            QString name;
            QStringList keys;
            QWidget* container;
            QList<DraggableComponentWidget*> items;
        };

        QList<CategoryBlock> categories;

        void clearPanel();
        void selectComponent(const QString& modelId);
        void openDocs();
};
