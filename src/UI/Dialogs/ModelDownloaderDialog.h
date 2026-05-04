#pragma once

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringList>
#include <QJsonObject>

class ModelDownloaderDialog : public QDialog {
    Q_OBJECT

public:
    explicit ModelDownloaderDialog(QWidget* parent = nullptr);
    void startSync(); 

private slots:
    void onCatalogDownloaded(QNetworkReply* reply);
    void onRsdefDownloaded(QNetworkReply* reply);
    void updateResourceProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
    void processNextComponent();
    void processNextResource();
    void extractResourcesFromRsdef(const QJsonObject& rsdefObj, const QString& baseLocalPath);
    void finishSync();

    // UI Elements
    QLabel* m_statusLabel;
    QProgressBar* m_topProgressBar;    
    QProgressBar* m_bottomProgressBar; 
    QLabel* m_fileLabel;

    // Networking
    QNetworkAccessManager* m_networkManager;
    QString m_remoteBaseUrl = "https://alfaiajanon.github.io/RoboticsStudio/"; 
    QByteArray m_downloadedCatalogData;

    // State Queues
    QStringList m_componentQueue; 
    
    struct ResourceTask {
        QString remoteUrl;
        QString localPath;
    };
    QList<ResourceTask> m_resourceQueue; 

    int m_totalComponents = 0;
    int m_completedComponents = 0;
    int m_totalResourcesForCurrent = 0;
    int m_completedResourcesForCurrent = 0;
    
    QString m_localBaseDir;          // Absolute path to AppData
    QString m_currentRsdefLocalPath; // Absolute path to current .rsdef
    QString m_currentRelativeDir;    // e.g., "models/servo/servo_sg90"
};