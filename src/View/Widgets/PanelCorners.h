#pragma once

#include <QWidget>



/*
 * Rounds the corners of a panel and draws its 1px outline, so the panel reads
 * as a card on the window background.
 * QSS border-radius does not clip children (a tree or the 3D viewport would
 * paint square corners over it), so this is a mouse-transparent overlay that
 * stays raised above the target's children and only paints the corner wedges
 * (in the window color) and the outline.
 */
class PanelCorners : public QWidget {
    Q_OBJECT

    public:
        static PanelCorners* install(QWidget* target);

    protected:
        void paintEvent(QPaintEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;

    private:
        explicit PanelCorners(QWidget* target);
        void scheduleRaise();

        bool raisePending = false;
};
