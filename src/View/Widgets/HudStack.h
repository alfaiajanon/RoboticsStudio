#pragma once

#include <QWidget>
#include <QString>

class QStackedWidget;
class QButtonGroup;
class QVBoxLayout;
class QHBoxLayout;



/*
 * Tab-less page stack for the central editor area.
 * Pages fill the whole rect; a floating "master HUD" (vertical page switch +
 * extra controls such as the PLAY button) stays raised on top of every page,
 * anchored bottom-left. Per-page overlays are simply children of their page.
 */
class HudStack : public QWidget {
    Q_OBJECT

    public:
        explicit HudStack(QWidget* parent = nullptr);

        int addPage(QWidget* page, const QString& label, const QString& tooltip);
        void setCurrentPage(QWidget* page);
        QWidget* currentPage() const;

        // Appends a control to the right of the page switch (bottom-aligned).
        void addHudControl(QWidget* w);

    signals:
        void currentPageChanged(QWidget* page);

    protected:
        void resizeEvent(QResizeEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;

    private:
        void placeHud();

        QStackedWidget* stack;
        QWidget* masterHud;
        QHBoxLayout* hudLayout;
        QVBoxLayout* switchLayout;
        QButtonGroup* switchGroup;
};
