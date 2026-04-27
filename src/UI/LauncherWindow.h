#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QStringList>
#include <QShowEvent>

class LauncherWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit LauncherWindow(QWidget *parent = nullptr);
    ~LauncherWindow();

signals:
    void projectOpened(const QString& projectPath);
    void fetchModelsRequested();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onNewProjectClicked();
    void onImportProjectClicked();
    void onDeleteProjectClicked();
    void onFetchModelsClicked();
    void onProjectDoubleClicked(QListWidgetItem* item);
    void onAutoOpenToggled(int state);

private:
    void setupUI();
    void loadRecentProjects();
    void saveRecentProjects();
    void refreshProjectList();

    QListWidget* m_recentList;
    QCheckBox* m_autoOpenCheck;
    QComboBox* m_sortCombo;
    QStringList m_recentPaths;
};