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

    // 1. Create the .js file template
    QString jsFileName = projectName + ".js";
    QFile jsFile(dir.absoluteFilePath(jsFileName));
    QString jsFileFullPath = jsFile.fileName();
    if (jsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QString jsTemplate = 
            "// RoboticsStudio Controller Script\n\n"
            "function setup() {\n"
            "    // Called once when simulation starts\n"
            "}\n\n"
            "function loop() {\n"
            "    // Called every physics step\n"
            "}\n";
        jsFile.write(jsTemplate.toUtf8());
        jsFile.close();
    }

    // 2. Create the .rsproj file template
    m_finalProjectPath = dir.absoluteFilePath(projectName + ".rsproj");
    QFile projFile(m_finalProjectPath);
    if (projFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QString appVersion = ROBOTICS_STUDIO_VERSION; 
        QString projTemplate = QString(
            "{\n"
            "  \"version\": \"%1\",\n"
            "  \"meta\": {\n"
            "    \"author\": \"\",\n"
            "    \"icon\": \"\",\n"
            "    \"name\": \"%2\",\n"
            "    \"version\": \"1.0.0\"\n"
            "  },\n"
            "  \"script\": {\n"
            "    \"path\": \"%3\"\n"
            "  },\n"
            "  \"assembly\": {\n"
            "    \"components\": []\n"
            "  }\n"
            "}\n"
        ).arg(appVersion, projectName, jsFileFullPath);
        
        projFile.write(projTemplate.toUtf8());
        projFile.close();
    }
}

QString NewProjectDialog::getCreatedProjectPath() const {
    return m_finalProjectPath;
}