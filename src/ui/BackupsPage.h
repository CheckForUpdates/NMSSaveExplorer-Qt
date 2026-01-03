#pragma once

#include <QWidget>

#include "core/BackupManager.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

class BackupsPage : public QWidget
{
    Q_OBJECT

public:
    explicit BackupsPage(QWidget *parent = nullptr);

    void setBackupRoot(const QString &path);
    void setCurrentSavePath(const QString &path);
    void setBackups(const QList<BackupEntry> &entries);
    BackupEntry selectedBackup() const;
    bool currentOnlyEnabled() const;

signals:
    void refreshRequested();
    void restoreRequested(const BackupEntry &entry);
    void openFolderRequested(const QString &path);

private:
    void rebuildTable();

    QList<BackupEntry> backups_;
    QString backupRoot_;
    QString currentSavePath_;
    QLabel *rootLabel_ = nullptr;
    QCheckBox *currentOnly_ = nullptr;
    QTableWidget *table_ = nullptr;
    QPushButton *restoreButton_ = nullptr;
    QPushButton *openFolderButton_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
};
