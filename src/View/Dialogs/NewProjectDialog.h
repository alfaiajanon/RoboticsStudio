#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QString>

class NewProjectDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewProjectDialog(QWidget* parent = nullptr);
    
    // Call this after the dialog is accepted to get the new .rsproj path
    QString getCreatedProjectPath() const;

private slots:
    void onBrowseClicked();
    void onCreateClicked();

private:
    void generateProjectScaffolding(const QString& folderPath, const QString& projectName);

    QLineEdit* m_nameEdit;
    QLineEdit* m_locationEdit;
    QPushButton* m_createBtn;
    QString m_finalProjectPath;
};