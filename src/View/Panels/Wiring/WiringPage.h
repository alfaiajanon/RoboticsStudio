#pragma once

#include <QGraphicsView>
#include <QWidget>

class SchematicScene;
class QLabel;
class QListWidget;
class QTreeWidget;


// The schematic's canvas: wheel zooms under the cursor, middle or right drag
// pans, a dot grid behind the blocks.
class SchematicView : public QGraphicsView {
    public:
        explicit SchematicView(QGraphicsScene* scene, QWidget* parent = nullptr);
        void fitAll();

    protected:
        void wheelEvent(QWheelEvent* e) override;
        void mousePressEvent(QMouseEvent* e) override;
        void mouseMoveEvent(QMouseEvent* e) override;
        void mouseReleaseEvent(QMouseEvent* e) override;
        void drawBackground(QPainter* p, const QRectF& rect) override;

    private:
        bool panning = false;
        QPoint lastPan;
};


/*
 * The Real level's wiring page (editor centre stack, Ctrl+3; hidden at the
 * Prototype level): a schematic of the boards and the parts with typed pins.
 * Drag from pin to pin to wire, select a wire and press Delete to remove it,
 * drag blocks to arrange them; every edit is an undoable command. Beside it:
 * the design-rule findings and the net list.
 */
class WiringPage : public QWidget {
    Q_OBJECT

    public:
        explicit WiringPage(QWidget* parent = nullptr);

        void refresh();                  // rebuild from the project (after any structural edit)
        void setEditable(bool editable); // off while the simulation runs
        void setSelectedUid(int uid);

    signals:
        void componentSelected(int uid);

    protected:
        void showEvent(QShowEvent* e) override;
        void resizeEvent(QResizeEvent* e) override;

    private:
        void focusOn(int uid, const QString& pin);
        void fitIfNeeded();

        SchematicScene* scene;
        SchematicView* view;
        QLabel* title;
        QLabel* summary;
        QLabel* hint;
        QListWidget* issuesList;
        QTreeWidget* netsTree;
        QString shownProject;   // fit the view when another project is shown
        bool needsFit = true;
};
