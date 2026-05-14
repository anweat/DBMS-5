#include "DatabaseTreePanel.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
constexpr int RoleKind = Qt::UserRole + 1;
constexpr int RoleDatabase = Qt::UserRole + 2;
constexpr int RoleTable = Qt::UserRole + 3;
constexpr int KindTable = 1;
}

DatabaseTreePanel::DatabaseTreePanel(QWidget *parent)
    : QWidget(parent),
      tree_(new QTreeWidget(this))
{
    tree_->setObjectName(QStringLiteral("databaseTree"));
    tree_->setHeaderLabel(tr("Objects"));
    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    refreshButton->setObjectName(QStringLiteral("catalogRefreshButton"));

    connect(refreshButton, &QPushButton::clicked, this, &DatabaseTreePanel::refreshRequested);
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
        auto *tablesItem = new QTreeWidgetItem(dbItem, {tr("Tables")});
        for (const auto &table : database.tables)
        {
            auto *tableItem = new QTreeWidgetItem(tablesItem, {table.name});
            tableItem->setData(0, RoleKind, KindTable);
            tableItem->setData(0, RoleDatabase, database.name);
            tableItem->setData(0, RoleTable, table.name);

            auto *columnsItem = new QTreeWidgetItem(tableItem, {tr("Columns")});
            for (const auto &column : table.columns)
            {
                QString label = column.name + QStringLiteral("  ") + column.type;
                if (!column.key.isEmpty())
                    label += QStringLiteral("  [") + column.key + QStringLiteral("]");
                new QTreeWidgetItem(columnsItem, {label});
            }
            new QTreeWidgetItem(tableItem, {tr("Indexes")});
        }
    }

    auto *usersRoot = new QTreeWidgetItem(tree_, {tr("Users / Privileges")});
    for (const auto &user : catalog.users)
    {
        QString label = user.name;
        if (!user.privileges.isEmpty())
            label += QStringLiteral("  ") + user.privileges;
        new QTreeWidgetItem(usersRoot, {label});
    }

    tree_->expandItem(dbRoot);
    tree_->expandItem(usersRoot);
}

void DatabaseTreePanel::openCurrentItem(QTreeWidgetItem *item)
{
    if (!item || item->data(0, RoleKind).toInt() != KindTable)
        return;
    emit tableOpenRequested(item->data(0, RoleDatabase).toString(),
                            item->data(0, RoleTable).toString());
}
