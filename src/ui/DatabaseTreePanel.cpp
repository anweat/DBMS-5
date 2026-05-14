#include "DatabaseTreePanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
constexpr int RoleKind = Qt::UserRole + 1;
constexpr int RoleDatabase = Qt::UserRole + 2;
constexpr int RoleTable = Qt::UserRole + 3;
constexpr int RoleLines = Qt::UserRole + 4;
constexpr int KindDatabase = 1;
constexpr int KindTable = 2;
constexpr int KindColumn = 3;
constexpr int KindIndex = 4;
constexpr int KindUser = 5;
}

DatabaseTreePanel::DatabaseTreePanel(QWidget *parent)
    : QWidget(parent),
      tree_(new QTreeWidget(this))
{
    tree_->setObjectName(QStringLiteral("databaseTree"));
    tree_->setHeaderLabel(tr("Objects"));
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tree_->setTextElideMode(Qt::ElideNone);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    refreshButton->setObjectName(QStringLiteral("catalogRefreshButton"));

    connect(refreshButton, &QPushButton::clicked, this, &DatabaseTreePanel::refreshRequested);
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        selectCurrentItem(item);
    });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
        selectCurrentItem(current);
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        openCurrentItem(item);
    });

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(refreshButton);
    buttons->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(buttons);
    layout->addWidget(tree_, 1);
}

void DatabaseTreePanel::setCatalog(const CatalogSnapshot &catalog)
{
    tree_->clear();

    auto *dbRoot = new QTreeWidgetItem(tree_, {tr("Databases")});
    for (const auto &database : catalog.databases)
    {
        auto *dbItem = new QTreeWidgetItem(dbRoot, {database.name});
        dbItem->setData(0, RoleKind, KindDatabase);
        dbItem->setData(0, RoleDatabase, database.name);
        dbItem->setData(0, RoleLines, QStringList{
            tr("Database: %1").arg(database.name),
            tr("Visible tables: %1").arg(database.tables.size()),
            tr("Clicking this database switches the active session database."),
        });

        auto *tablesItem = new QTreeWidgetItem(dbItem, {tr("Tables")});
        for (const auto &table : database.tables)
        {
            auto *tableItem = new QTreeWidgetItem(tablesItem, {table.name});
            tableItem->setData(0, RoleKind, KindTable);
            tableItem->setData(0, RoleDatabase, database.name);
            tableItem->setData(0, RoleTable, table.name);
            tableItem->setData(0, RoleLines, QStringList{
                tr("Database: %1").arg(database.name),
                tr("Table: %1").arg(table.name),
                tr("Columns: %1").arg(table.columns.size()),
                tr("Indexes: %1").arg(table.indexes.size()),
                tr("Clicking this table loads its rows in the center editor."),
            });

            auto *columnsItem = new QTreeWidgetItem(tableItem, {tr("Columns")});
            for (const auto &column : table.columns)
            {
                QString label = column.name + QStringLiteral("  ") + column.type;
                if (!column.key.isEmpty())
                    label += QStringLiteral("  [") + column.key + QStringLiteral("]");
                auto *columnItem = new QTreeWidgetItem(columnsItem, {label});
                columnItem->setData(0, RoleKind, KindColumn);
                columnItem->setData(0, RoleDatabase, database.name);
                columnItem->setData(0, RoleTable, table.name);
                columnItem->setData(0, RoleLines, QStringList{
                    tr("Database: %1").arg(database.name),
                    tr("Table: %1").arg(table.name),
                    tr("Column: %1").arg(column.name),
                    tr("Type: %1").arg(column.type),
                    tr("Nullable: %1").arg(column.nullable),
                    tr("Key: %1").arg(column.key.isEmpty() ? tr("none") : column.key),
                });
            }
            auto *indexesItem = new QTreeWidgetItem(tableItem, {tr("Indexes")});
            for (const auto &index : table.indexes)
            {
                auto *indexItem = new QTreeWidgetItem(indexesItem, {index});
                indexItem->setData(0, RoleKind, KindIndex);
                indexItem->setData(0, RoleDatabase, database.name);
                indexItem->setData(0, RoleTable, table.name);
                indexItem->setData(0, RoleLines, QStringList{
                    tr("Database: %1").arg(database.name),
                    tr("Table: %1").arg(table.name),
                    tr("Index: %1").arg(index),
                });
            }
        }
    }

    if (!catalog.users.isEmpty())
    {
        auto *usersRoot = new QTreeWidgetItem(tree_, {tr("Users / Privileges")});
        for (const auto &user : catalog.users)
        {
            QString label = user.name;
            if (!user.privileges.isEmpty())
                label += QStringLiteral("  ") + user.privileges;
            auto *userItem = new QTreeWidgetItem(usersRoot, {label});
            userItem->setData(0, RoleKind, KindUser);
            userItem->setData(0, RoleLines, QStringList{
                tr("User: %1").arg(user.name),
                tr("Privileges: %1").arg(user.privileges.isEmpty() ? tr("none") : user.privileges),
            });
        }
        tree_->expandItem(usersRoot);
    }

    tree_->expandItem(dbRoot);
    tree_->expandAll();
    tree_->resizeColumnToContents(0);
}

void DatabaseTreePanel::openCurrentItem(QTreeWidgetItem *item)
{
    if (!item || item->data(0, RoleKind).toInt() != KindTable)
        return;
    emit tableOpenRequested(item->data(0, RoleDatabase).toString(),
                            item->data(0, RoleTable).toString());
}

void DatabaseTreePanel::selectCurrentItem(QTreeWidgetItem *item)
{
    if (!item)
        return;

    const int kind = item->data(0, RoleKind).toInt();
    const QString database = item->data(0, RoleDatabase).toString();
    const QString table = item->data(0, RoleTable).toString();
    const QStringList lines = item->data(0, RoleLines).toStringList();
    if (!lines.isEmpty())
        emit objectDetailRequested(item->text(0), lines);

    if (kind == KindDatabase)
        emit databaseUseRequested(database);
    else if (kind == KindTable)
        emit tableOpenRequested(database, table);
}
