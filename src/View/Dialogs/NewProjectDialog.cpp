#include "NewProjectDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include "Version.h"
#include "View/Panels/FirmwareTemplate.h"



NewProjectDialog::NewProjectDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Create New Project");
    setMinimumWidth(400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Project Name
    mainLayout->addWidget(new QLabel("Project Name:"));
    m_nameEdit = new QLineEdit("MyRobot");
    mainLayout->addWidget(m_nameEdit);

    // Location
    mainLayout->addWidget(new QLabel("Location:"));
    QHBoxLayout* locLayout = new QHBoxLayout();
    m_locationEdit = new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    QPushButton* browseBtn = new QPushButton("Browse...");
    locLayout->addWidget(m_locationEdit);
    locLayout->addWidget(browseBtn);
    mainLayout->addLayout(locLayout);

    // Level: Prototype (an ideal board, auto-wired) or Real (add and wire boards yourself).
    mainLayout->addWidget(new QLabel("Level:"));
    m_levelCombo = new QComboBox();
    m_levelCombo->addItem("Prototype: an ideal board, every part wired automatically", "prototype");
    m_levelCombo->addItem("Real: add a board from the library and wire it yourself", "real");
    m_levelCombo->setToolTip("The level stays with the project. A Prototype project can later make a Real copy of itself "
                              "(project settings). A Real project starts with no board.");
    mainLayout->addWidget(m_levelCombo);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* cancelBtn = new QPushButton("Cancel");
    m_createBtn = new QPushButton("Create");
    m_createBtn->setDefault(true);
    
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(m_createBtn);
    mainLayout->addLayout(btnLayout);

    connect(browseBtn, &QPushButton::clicked, this, &NewProjectDialog::onBrowseClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_createBtn, &QPushButton::clicked, this, &NewProjectDialog::onCreateClicked);
}

void NewProjectDialog::onBrowseClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Parent Folder", m_locationEdit->text());
    if (!dir.isEmpty()) {
        m_locationEdit->setText(dir);
    }
}

void NewProjectDialog::onCreateClicked() {
    QString name = m_nameEdit->text().trimmed();
    QString location = m_locationEdit->text().trimmed();

    if (name.isEmpty() || location.isEmpty()) {
        QMessageBox::warning(this, "Error", "Name and Location cannot be empty.");
        return;
    }

    QDir parentDir(location);
    if (!parentDir.exists()) {
        QMessageBox::warning(this, "Error", "Selected location does not exist.");
        return;
    }

    // Create the project folder
    QString projectFolderPath = parentDir.absoluteFilePath(name);
    if (parentDir.exists(name)) {
        QMessageBox::warning(this, "Error", "A folder with this name already exists in that location.");
        return;
    }

    if (!parentDir.mkdir(name)) {
        QMessageBox::warning(this, "Error", "Failed to create project directory.");
        return;
    }

    generateProjectScaffolding(projectFolderPath, name);
    accept(); // Closes the dialog with success status
}

void NewProjectDialog::generateProjectScaffolding(const QString& folderPath, const QString& projectName) {
    QDir dir(folderPath);

    // 1. The firmware sketch (no parts yet: a bare setup()/loop())
    QString sketchFileName = projectName + ".ino";
    QFile sketchFile(dir.absoluteFilePath(sketchFileName));
    if (sketchFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        sketchFile.write(firmwareTemplate(nullptr, m_levelCombo->currentData().toString() == "real").toUtf8());
        sketchFile.close();
    }

    // 2. Create the .rsproj file template
    m_finalProjectPath = dir.absoluteFilePath(projectName + ".rsproj");
    QFile projFile(m_finalProjectPath);
    if (projFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QString appVersion = ROBOTICS_STUDIO_VERSION; 
        QString projTemplate = QString(
            "{\n"
            "  \"schema\": 3,\n"
            "  \"level\": \"%4\",\n"
            "  \"version\": \"%1\",\n"
            "  \"meta\": {\n"
            "    \"author\": \"\",\n"
            "    \"icon\": \"\",\n"
            "    \"name\": \"%2\",\n"
            "    \"version\": \"1.0.0\"\n"
            "  },\n"
            "  \"script\": {\n"
            "    \"current\": 0,\n"
            "    \"paths\": [ \"%3\" ]\n"
            "  },\n"
            "  \"assembly\": {\n"
            "    \"components\": []\n"
            "  }\n"
            "}\n"
        ).arg(appVersion, projectName, sketchFileName, m_levelCombo->currentData().toString());
        
        projFile.write(projTemplate.toUtf8());
        projFile.close();
    }
}

QString NewProjectDialog::getCreatedProjectPath() const {
    return m_finalProjectPath;
}