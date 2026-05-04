#include "ModelDownloaderDialog.h"
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>

ModelDownloaderDialog::ModelDownloaderDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Syncing RoboticsStudio Models");
    setMinimumWidth(500);

    QVBoxLayout* layout = new QVBoxLayout(this);

    m_statusLabel = new QLabel("Checking for updates...", this);
    layout->addWidget(m_statusLabel);

    m_topProgressBar = new QProgressBar(this);
    m_topProgressBar->setFormat("%v / %m Components");
    layout->addWidget(m_topProgressBar);

    m_fileLabel = new QLabel("Waiting...", this);
    layout->addWidget(m_fileLabel);

    m_bottomProgressBar = new QProgressBar(this);
    m_bottomProgressBar->setFormat("%p% - %v / %m Files");
    layout->addWidget(m_bottomProgressBar);

    m_networkManager = new QNetworkAccessManager(this);
    
    // Set our safe, cross-platform AppData directory
    m_localBaseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

void ModelDownloaderDialog::startSync() {
    // 1. Fetch remote Catalog.json
    QUrl catalogUrl(m_remoteBaseUrl + "models/Catalog.json");
    QNetworkRequest request(catalogUrl);
    
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onCatalogDownloaded(reply); });
}

void ModelDownloaderDialog::onCatalogDownloaded(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::critical(this, "Network Error", "Failed to fetch Catalog.json");
        reject();
        return;
    }

    m_downloadedCatalogData = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(m_downloadedCatalogData);
    QJsonObject remoteCatalog = doc.object();
    QString remoteVersion = remoteCatalog["version"].toString();

    // --- VERSION CHECK LOGIC HERE ---
    // Read your local Catalog.json. If local version == remoteVersion, return early.
    // For this example, we assume an update is needed and proceed.
    
    m_statusLabel->setText("Preparing update block to v" + remoteVersion + "...");
    
    // Parse the Catalog to fill the component queue
    m_componentQueue.clear();
    QJsonArray categories = remoteCatalog["Categories"].toArray();
    for (const QJsonValue& catVal : categories) {
        QJsonArray items = catVal.toObject()["items"].toArray();
        for (const QJsonValue& itemVal : items) {
            m_componentQueue.append(itemVal.toString());
        }
    }

    m_totalComponents = m_componentQueue.size();
    m_completedComponents = 0;
    
    m_topProgressBar->setMaximum(m_totalComponents);
    m_topProgressBar->setValue(0);

    processNextComponent();
}

void ModelDownloaderDialog::processNextComponent() {
    if (m_componentQueue.isEmpty()) {
        finishSync();
        return;
    }

    QString rsdefPath = m_componentQueue.takeFirst();
    
    // Clean the path by removing "./" if it exists so URLs and local dirs map perfectly
    QString cleanPath = rsdefPath;
    if (cleanPath.startsWith("./")) {
        cleanPath = cleanPath.mid(2);
    }
    
    // Compute paths using the AppData directory
    m_currentRsdefLocalPath = m_localBaseDir + "/" + cleanPath; 
    m_currentRelativeDir = QFileInfo(cleanPath).path(); // e.g., "models/servo/servo_sg90"
    
    m_statusLabel->setText("Processing Component: " + QFileInfo(rsdefPath).fileName());
    m_fileLabel->setText("Fetching definition file...");

    // Download the .rsdef file first
    QUrl url(m_remoteBaseUrl + cleanPath);
    QNetworkReply* reply = m_networkManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onRsdefDownloaded(reply); });
}

void ModelDownloaderDialog::onRsdefDownloaded(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        processNextComponent();
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    // Save the .rsdef to disk in AppData
    QFileInfo fileInfo(m_currentRsdefLocalPath);
    fileInfo.absoluteDir().mkpath("."); 
    QFile file(m_currentRsdefLocalPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    extractResourcesFromRsdef(doc.object(), fileInfo.absolutePath());
}

void ModelDownloaderDialog::extractResourcesFromRsdef(const QJsonObject& rsdefObj, const QString& baseLocalPath) {
    m_resourceQueue.clear();
    
    // 1. Icon
    if (rsdefObj.contains("meta")) {
        QString iconName = rsdefObj["meta"].toObject()["icon_path"].toString();
        if (!iconName.isEmpty()) {
            m_resourceQueue.append({ 
                m_remoteBaseUrl + m_currentRelativeDir + "/" + iconName, 
                baseLocalPath + "/" + iconName 
            });
        }
    }

    // 2. Meshes & Materials
    if (rsdefObj.contains("resources")) {
        QJsonObject resObj = rsdefObj["resources"].toObject();
        
        QJsonObject meshes = resObj["meshes"].toObject();
        for (const QString& key : meshes.keys()) {
            QString meshPath = meshes[key].toString();
            m_resourceQueue.append({ 
                m_remoteBaseUrl + m_currentRelativeDir + "/" + meshPath, 
                baseLocalPath + "/" + meshPath 
            });
        }

        QJsonObject materials = resObj["materials"].toObject();
        for (const QString& key : materials.keys()) {
            QString matPath = materials[key].toString();
            m_resourceQueue.append({ 
                m_remoteBaseUrl + m_currentRelativeDir + "/" + matPath, 
                baseLocalPath + "/" + matPath 
            });
        }
    }

    m_totalResourcesForCurrent = m_resourceQueue.size();
    m_completedResourcesForCurrent = 0;
    
    m_bottomProgressBar->setMaximum(m_totalResourcesForCurrent);
    m_bottomProgressBar->setValue(0);

    processNextResource();
}

void ModelDownloaderDialog::processNextResource() {
    if (m_resourceQueue.isEmpty()) {
        m_completedComponents++;
        m_topProgressBar->setValue(m_completedComponents);
        processNextComponent();
        return;
    }

    ResourceTask task = m_resourceQueue.takeFirst();
    m_fileLabel->setText("Downloading: " + QFileInfo(task.localPath).fileName());
    
    m_bottomProgressBar->setValue(m_completedResourcesForCurrent);

    QNetworkReply* reply = m_networkManager->get(QNetworkRequest(QUrl(task.remoteUrl)));
    connect(reply, &QNetworkReply::downloadProgress, this, &ModelDownloaderDialog::updateResourceProgress);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, localPath = task.localPath]() {
        if (reply->error() == QNetworkReply::NoError) {
            QFileInfo fi(localPath);
            fi.absoluteDir().mkpath(".");
            QFile file(localPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(reply->readAll());
                file.close();
            }
        }
        reply->deleteLater();
        m_completedResourcesForCurrent++;
        processNextResource(); 
    });
}

void ModelDownloaderDialog::updateResourceProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (bytesTotal > 0) {
        // Optional tracking
    }
}

void ModelDownloaderDialog::finishSync() {
    m_statusLabel->setText("Sync Complete!");
    m_fileLabel->setText("All files up to date.");
    m_bottomProgressBar->setValue(m_bottomProgressBar->maximum());
    
    // Save the new Catalog.json to local disk so LibraryManager can read it
    QString localCatalogPath = m_localBaseDir + "/models/Catalog.json";
    QFileInfo fi(localCatalogPath);
    fi.absoluteDir().mkpath(".");
    QFile catalogFile(localCatalogPath);
    if (catalogFile.open(QIODevice::WriteOnly)) {
        catalogFile.write(m_downloadedCatalogData);
        catalogFile.close();
    }
    
    QMessageBox::information(this, "Success", "Models updated successfully!");
    accept(); 
}