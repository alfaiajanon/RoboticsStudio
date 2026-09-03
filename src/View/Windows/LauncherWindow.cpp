#include "LauncherWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QSettings>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QStyle>

#include "Document/Components/LibraryManager.h"
#include "Version.h"
#include "Application/Application.h"
#include "View/Dialogs/NewProjectDialog.h"
#include "View/Dialogs/ModelDownloaderDialog.h"

LauncherWindow::LauncherWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("RoboticsStudio - Hub");
    resize(1100, 700);
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
    QPushButton* btnOpen = new QPushButton("Open Project");
    QPushButton* btnDelete = new QPushButton("Delete Project");
    QPushButton* btnFetch = new QPushButton("Fetch Component Models");

    btnOpen->setEnabled(false);
    btnDelete->setEnabled(false);

    leftLayout->addWidget(logoLabel);
    leftLayout->addSpacing(30);
    leftLayout->addWidget(btnNew);
    leftLayout->addWidget(btnImport);
    leftLayout->addWidget(btnOpen);
    leftLayout->addWidget(btnDelete);
    leftLayout->addStretch();
    leftLayout->addWidget(btnFetch);

    // === RIGHT PANEL (Recent Projects) ===
    QWidget* rightPanel = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);

    rightLayout->setContentsMargins(20, 20, 20, 20);

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
    m_recentList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recentList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // UI Polish: Make the Qt Item transparent and apply styles strictly to the internal QFrame
    m_recentList->setStyleSheet(
        "QListWidget { background-color: #1a1a1a; border: 1px solid #333333; border-radius: 8px; padding: 10px; outline: none; }"
        "QListWidget::item { background: transparent; border: none; margin-bottom: 4px; padding: 0px; }"
        "QListWidget::item:hover { background: transparent; }"
        "QListWidget::item:selected { background: transparent; }"

        "QFrame#ProjectCard {"
        "    background-color: transparent;"
        "    border: 1px solid #2a2a2a;"
        "    border-radius: 6px;"
        "}"
        "QFrame#ProjectCard:hover {"
        "    background-color: #2a2d31;"
        "}"
        "QFrame#ProjectCard[selected=\"true\"] {"
        "    background-color: #2a2d31;"
        "    border: 1px solid #0e639c;"
        "}"
    );

    rightLayout->addLayout(headerLayout);
    rightLayout->addWidget(m_recentList);

    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(rightPanel);

    // === CONNECTIONS ===
    connect(btnNew, &QPushButton::clicked, this, &LauncherWindow::onNewProjectClicked);
    connect(btnImport, &QPushButton::clicked, this, &LauncherWindow::onImportProjectClicked);
    connect(btnDelete, &QPushButton::clicked, this, &LauncherWindow::onDeleteProjectClicked);
    connect(btnFetch, &QPushButton::clicked, this, &LauncherWindow::onFetchModelsClicked);
    connect(m_recentList, &QListWidget::itemDoubleClicked, this, &LauncherWindow::onProjectDoubleClicked);
    connect(m_autoOpenCheck, &QCheckBox::stateChanged, this, &LauncherWindow::onAutoOpenToggled);

    connect(btnOpen, &QPushButton::clicked, this, [this]() {
        if (QListWidgetItem* item = m_recentList->currentItem()) {
            onProjectDoubleClicked(item);
        }
    });

    // Sync the QListWidget selection state dynamically down to our QFrame
    connect(m_recentList, &QListWidget::itemSelectionChanged, this, [btnOpen, btnDelete, this]() {
        bool hasSelection = m_recentList->selectedItems().count() > 0;
        btnOpen->setEnabled(hasSelection);
        btnDelete->setEnabled(hasSelection);

        for (int i = 0; i < m_recentList->count(); ++i) {
            QListWidgetItem* item = m_recentList->item(i);
            QWidget* widget = m_recentList->itemWidget(item);
            if (widget) {
                bool isSelected = item->isSelected();
                if (widget->property("selected").toBool() != isSelected) {
                    widget->setProperty("selected", isSelected);
                    widget->style()->unpolish(widget);
                    widget->style()->polish(widget);
                }
            }
        }
    });
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
        QString robotName = info.fileName();
        QString iconPath = "";

        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            QJsonObject root = doc.object();
            if (root.contains("meta")) {
                QJsonObject meta = root["meta"].toObject();
                robotName = meta["name"].toString(robotName);
                iconPath = meta["icon"].toString("");
            }
            file.close();
        }

        // Use QFrame to safely support flawless border-radius rendering
        QFrame* cardWidget = new QFrame();
        cardWidget->setObjectName("ProjectCard");
        cardWidget->setProperty("selected", false);

        QHBoxLayout* cardLayout = new QHBoxLayout(cardWidget);
        cardLayout->setContentsMargins(10, 10, 10, 10);
        cardLayout->setSpacing(15);

        QLabel* iconLabel = new QLabel();
        iconLabel->setFixedSize(48, 48);
        iconLabel->setAlignment(Qt::AlignCenter);

        QPixmap pixmap;
        bool iconLoaded = false;
        if (!iconPath.isEmpty()) {
            QDir projDir = info.absoluteDir();
            QString absIconPath = projDir.absoluteFilePath(iconPath);
            if (pixmap.load(absIconPath)) {
                iconLabel->setPixmap(pixmap.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                iconLoaded = true;
            }
        }

        if (!iconLoaded) {
            iconLabel->setText("⊘");
            iconLabel->setStyleSheet("font-size: 24px; color: #555555; background: #2d2d2d; border-radius: 6px; border: none;");
        }
        cardLayout->addWidget(iconLabel);

        QVBoxLayout* textLayout = new QVBoxLayout();
        textLayout->setSpacing(2);

        QLabel* nameLabel = new QLabel(QString("<b>%1</b>").arg(robotName));
        nameLabel->setStyleSheet("font-size: 15px; color: #ffffff; border: none; background: transparent;");

        QLabel* pathLabel = new QLabel(path);
        pathLabel->setStyleSheet("color: #888888; font-size: 11px; border: none; background: transparent;");

        textLayout->addWidget(nameLabel);
        textLayout->addWidget(pathLabel);
        textLayout->addStretch();

        cardLayout->addLayout(textLayout);

        QListWidgetItem* item = new QListWidgetItem(m_recentList);
        item->setData(Qt::UserRole, path);
        item->setSizeHint(QSize(0, 70));
        m_recentList->setItemWidget(item, cardWidget);
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
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "Select Existing Project",
        "",
        "RoboticsStudio Project (*.rsproj);;All Files (*)"
    );

    if (!filePath.isEmpty()) {
        m_recentPaths.removeAll(filePath);
        m_recentPaths.prepend(filePath);
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
    msgBox.setText("Remove project from recent projects?");

    QCheckBox* deleteDiskCheck = new QCheckBox("Also delete project folder from disk (Cannot be undone)");
    msgBox.setCheckBox(deleteDiskCheck);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);

    if (msgBox.exec() == QMessageBox::Yes) {
        m_recentPaths.removeAll(path);

        if (deleteDiskCheck->isChecked()) {
            QFileInfo fileInfo(path);
            QDir projectDir = fileInfo.absoluteDir();

            if (!projectDir.removeRecursively()) {
                QMessageBox::warning(this, "Deletion Failed", "Could not delete all files from disk. They might be in use or lack permissions.");
            }
        }

        refreshProjectList();
        saveRecentProjects();
    }
}

void LauncherWindow::onFetchModelsClicked() {
    Log::info("Fetch models via Launcher....");
    ModelDownloaderDialog dialog(nullptr);
    dialog.startSync();
    dialog.exec();
    Log::info("Models downloaded to: " + LibraryManager::getInstance().getModelsDir());
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
