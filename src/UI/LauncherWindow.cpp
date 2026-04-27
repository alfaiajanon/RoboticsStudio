#include "LauncherWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QSettings>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include "Dialogs/NewProjectDialog.h"
#include "Version.h"


LauncherWindow::LauncherWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("RoboticsStudio - Hub");
    resize(900, 600);
    setupUI();
}



LauncherWindow::~LauncherWindow() {
    saveRecentProjects();
}



void LauncherWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    loadRecentProjects(); 
}



void LauncherWindow::setupUI() {
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);

    // === LEFT PANEL (Controls) ===
    QWidget* leftPanel = new QWidget();
    leftPanel->setFixedWidth(250);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);

    QLabel* logoLabel = new QLabel("<b>RoboticsStudio</b><br>v"+QString(ROBOTICS_STUDIO_VERSION));
    logoLabel->setAlignment(Qt::AlignCenter);
    logoLabel->setStyleSheet("font-size: 20px; padding: 20px;");
    
    QPushButton* btnNew = new QPushButton("New Project");
    QPushButton* btnImport = new QPushButton("Import Project");
    QPushButton* btnDelete = new QPushButton("Delete Project");
    QPushButton* btnFetch = new QPushButton("Fetch Component Models");

    leftLayout->addWidget(logoLabel);
    leftLayout->addSpacing(30);
    leftLayout->addWidget(btnNew);
    leftLayout->addWidget(btnImport);
    leftLayout->addWidget(btnDelete);
    leftLayout->addStretch(); // Pushes fetch button to the bottom
    leftLayout->addWidget(btnFetch);

    // === RIGHT PANEL (Recent Projects) ===
    QWidget* rightPanel = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    m_sortCombo = new QComboBox();
    m_sortCombo->addItems({"Last Opened", "Alphabetical"});
    
    m_autoOpenCheck = new QCheckBox("Auto open last project");
    
    QSettings settings("RoboticsStudio", "RoboticsStudio");
    m_autoOpenCheck->setChecked(settings.value("autoOpenEnabled", false).toBool());

    headerLayout->addWidget(new QLabel("Recent Projects:"));
    headerLayout->addStretch();
    headerLayout->addWidget(m_sortCombo);
    headerLayout->addWidget(m_autoOpenCheck);

    m_recentList = new QListWidget();
    m_recentList->setStyleSheet("background-color: #1a1a1a; padding: 5px;");
    
    rightLayout->addLayout(headerLayout);
    rightLayout->addWidget(m_recentList);

    // Add panels to main layout
    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(rightPanel);

    // === CONNECTIONS ===
    connect(btnNew, &QPushButton::clicked, this, &LauncherWindow::onNewProjectClicked);
    connect(btnImport, &QPushButton::clicked, this, &LauncherWindow::onImportProjectClicked);
    connect(btnDelete, &QPushButton::clicked, this, &LauncherWindow::onDeleteProjectClicked);
    connect(btnFetch, &QPushButton::clicked, this, &LauncherWindow::onFetchModelsClicked);
    connect(m_recentList, &QListWidget::itemDoubleClicked, this, &LauncherWindow::onProjectDoubleClicked);
    connect(m_autoOpenCheck, &QCheckBox::stateChanged, this, &LauncherWindow::onAutoOpenToggled);
}



void LauncherWindow::loadRecentProjects() {
    QSettings settings("RoboticsStudio", "RoboticsStudio");
    m_recentPaths = settings.value("recentProjects").toStringList();
    refreshProjectList();
}




void LauncherWindow::saveRecentProjects() {
    QSettings settings("RoboticsStudio", "RoboticsStudio");
    settings.setValue("recentProjects", m_recentPaths);
}




void LauncherWindow::refreshProjectList() {
    m_recentList->clear();
    for (const QString& path : m_recentPaths) {
        QFileInfo info(path);
        QListWidgetItem* item = new QListWidgetItem(info.fileName());
        item->setData(Qt::UserRole, path);
        m_recentList->addItem(item);
    }
}



void LauncherWindow::onNewProjectClicked() {
    NewProjectDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        QString path = dialog.getCreatedProjectPath();
        
        m_recentPaths.removeAll(path);
        m_recentPaths.prepend(path);
        saveRecentProjects();
        
        emit projectOpened(path); 
    }
}



void LauncherWindow::onImportProjectClicked() {
    // Open a file dialog filtered specifically for .rsproj files
    QString filePath = QFileDialog::getOpenFileName(
        this, 
        "Select Existing Project", 
        "", 
        "RoboticsStudio Project (*.rsproj);;All Files (*)"
    );

    if (!filePath.isEmpty()) {
        m_recentPaths.removeAll(filePath);
        m_recentPaths.prepend(filePath); // Now storing the exact file path
        saveRecentProjects();
        emit projectOpened(filePath);
    }
}




void LauncherWindow::onDeleteProjectClicked() {
    QListWidgetItem* currentItem = m_recentList->currentItem();
    if (!currentItem) return;

    QString path = currentItem->data(Qt::UserRole).toString();

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Remove Project");
    msgBox.setText("Remove '" + currentItem->text() + "' from recent projects?");
    
    QCheckBox* deleteDiskCheck = new QCheckBox("Also delete files from disk (Cannot be undone)");
    msgBox.setCheckBox(deleteDiskCheck);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);

    if (msgBox.exec() == QMessageBox::Yes) {
        m_recentPaths.removeAll(path);
        
        if (deleteDiskCheck->isChecked()) {
            QDir dir(path);
            dir.removeRecursively();    
            // [critical]: doesnt work
        }
        
        refreshProjectList();
        saveRecentProjects();
    }
}



void LauncherWindow::onFetchModelsClicked() {
    emit fetchModelsRequested(); 
}



void LauncherWindow::onProjectDoubleClicked(QListWidgetItem* item) {
    QString path = item->data(Qt::UserRole).toString();
    m_recentPaths.removeAll(path);
    m_recentPaths.prepend(path);
    saveRecentProjects();
    emit projectOpened(path);
}


void LauncherWindow::onAutoOpenToggled(int state) {
    QSettings settings("RoboticsStudio", "RoboticsStudio");
    settings.setValue("autoOpenEnabled", state == Qt::Checked);
}