#include "ui/BackupsPage.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDesktopServices>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QFileInfo>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kColumnTime = 0;
constexpr int kColumnSave = 1;
constexpr int kColumnSlot = 2;
constexpr int kColumnReason = 3;
constexpr int kColumnSize = 4;
constexpr int kColumnPath = 5;
constexpr int kColumnCount = 6;
}

BackupsPage::BackupsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *headerRow = new QHBoxLayout();
    rootLabel_ = new QLabel(tr("Backup location: -"), this);
    rootLabel_->setWordWrap(true);
    headerRow->addWidget(rootLabel_, 1);

    openFolderButton_ = new QPushButton(tr("Open Folder"), this);
    headerRow->addWidget(openFolderButton_);
    layout->addLayout(headerRow);

    auto *controls = new QHBoxLayout();
    currentOnly_ = new QCheckBox(tr("Only current save"), this);
    controls->addWidget(currentOnly_);
    controls->addStretch(1);

    refreshButton_ = new QPushButton(tr("Refresh"), this);
    controls->addWidget(refreshButton_);
    layout->addLayout(controls);

    table_ = new QTableWidget(this);
    table_->setColumnCount(kColumnCount);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setHorizontalHeaderLabels(QStringList()
        << tr("Time") << tr("Save") << tr("Slot") << tr("Reason") << tr("Size") << tr("Path"));
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(table_, 1);

    auto *actions = new QHBoxLayout();
    actions->addStretch(1);
    restoreButton_ = new QPushButton(tr("Restore"), this);
    restoreButton_->setEnabled(false);
    actions->addWidget(restoreButton_);
    layout->addLayout(actions);

    connect(refreshButton_, &QPushButton::clicked, this, &BackupsPage::refreshRequested);
    connect(currentOnly_, &QCheckBox::toggled, this, &BackupsPage::refreshRequested);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        restoreButton_->setEnabled(table_->currentRow() >= 0);
    });
    connect(openFolderButton_, &QPushButton::clicked, this, [this]() {
        QString path = backupRoot_;
        BackupEntry entry = selectedBackup();
        if (!entry.backupPath.isEmpty()) {
            path = QFileInfo(entry.backupPath).absolutePath();
        }
        if (!path.isEmpty()) {
            emit openFolderRequested(path);
        }
    });
    connect(restoreButton_, &QPushButton::clicked, this, [this]() {
        BackupEntry entry = selectedBackup();
        if (!entry.backupPath.isEmpty()) {
            emit restoreRequested(entry);
        }
    });
}

void BackupsPage::setBackupRoot(const QString &path)
{
    backupRoot_ = path;
    rootLabel_->setText(tr("Backup location: %1").arg(path));
}

void BackupsPage::setCurrentSavePath(const QString &path)
{
    currentSavePath_ = path;
}

void BackupsPage::setBackups(const QList<BackupEntry> &entries)
{
    backups_ = entries;
    rebuildTable();
}

BackupEntry BackupsPage::selectedBackup() const
{
    int row = table_->currentRow();
    if (row < 0 || row >= backups_.size()) {
        return {};
    }
    return backups_.at(row);
}

bool BackupsPage::currentOnlyEnabled() const
{
    return currentOnly_->isChecked();
}

void BackupsPage::rebuildTable()
{
    table_->setRowCount(backups_.size());
    for (int row = 0; row < backups_.size(); ++row) {
        const BackupEntry &entry = backups_.at(row);
        auto *timeItem = new QTableWidgetItem(BackupManager::formatTimestamp(entry.backupTimeMs));
        auto *saveItem = new QTableWidgetItem(entry.saveName);
        auto *slotItem = new QTableWidgetItem(entry.slotId);
        auto *reasonItem = new QTableWidgetItem(entry.reason);
        auto *sizeItem = new QTableWidgetItem(BackupManager::formatSize(entry.sizeBytes));
        auto *pathItem = new QTableWidgetItem(entry.backupPath);

        table_->setItem(row, kColumnTime, timeItem);
        table_->setItem(row, kColumnSave, saveItem);
        table_->setItem(row, kColumnSlot, slotItem);
        table_->setItem(row, kColumnReason, reasonItem);
        table_->setItem(row, kColumnSize, sizeItem);
        table_->setItem(row, kColumnPath, pathItem);
    }
    table_->resizeRowsToContents();
    restoreButton_->setEnabled(table_->currentRow() >= 0);
}
