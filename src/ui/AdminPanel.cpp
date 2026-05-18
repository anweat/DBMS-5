#include "AdminPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

AdminPanel::AdminPanel(QWidget *parent)
    : QWidget(parent),
      contextLabel_(new QLabel(tr("Select a database or table from the object tree"), this)),
      tabs_(new QTabWidget(this)),
      databaseEdit_(new QLineEdit(this)),
      tableEdit_(new QLineEdit(this)),
      tableColumnsTable_(new QTableWidget(this)),
      deleteWhereEdit_(new QLineEdit(this)),
      indexEdit_(new QLineEdit(this)),
      indexColumnsEdit_(new QLineEdit(this)),
      userEdit_(new QLineEdit(this)),
      passwordEdit_(new QLineEdit(this)),
      privilegeTargetDbEdit_(new QLineEdit(this)),
      privilegeTargetTableEdit_(new QLineEdit(this)),
      privilegeCombo_(new QComboBox(this)),
      privilegeAllCheck_(new QCheckBox(tr("ALL"), this)),
      privilegeSelectCheck_(new QCheckBox(tr("SELECT"), this)),
      privilegeInsertCheck_(new QCheckBox(tr("INSERT"), this)),
      privilegeUpdateCheck_(new QCheckBox(tr("UPDATE"), this)),
      privilegeDeleteCheck_(new QCheckBox(tr("DELETE"), this))
{
    databaseEdit_->setObjectName(QStringLiteral("adminDatabaseEdit"));
    tableEdit_->setObjectName(QStringLiteral("adminTableEdit"));
    tableColumnsTable_->setObjectName(QStringLiteral("adminTableColumnsTable"));
    deleteWhereEdit_->setObjectName(QStringLiteral("adminDeleteWhereEdit"));
    indexEdit_->setObjectName(QStringLiteral("adminIndexEdit"));
    indexColumnsEdit_->setObjectName(QStringLiteral("adminIndexColumnsEdit"));
    userEdit_->setObjectName(QStringLiteral("adminUserEdit"));
    passwordEdit_->setObjectName(QStringLiteral("adminPasswordEdit"));
    privilegeTargetDbEdit_->setObjectName(QStringLiteral("adminPrivilegeDbEdit"));
    privilegeTargetTableEdit_->setObjectName(QStringLiteral("adminPrivilegeTableEdit"));
    privilegeCombo_->setObjectName(QStringLiteral("adminPrivilegeCombo"));
    privilegeAllCheck_->setObjectName(QStringLiteral("adminPrivilegeAllCheck"));
    privilegeSelectCheck_->setObjectName(QStringLiteral("adminPrivilegeSelectCheck"));
    privilegeInsertCheck_->setObjectName(QStringLiteral("adminPrivilegeInsertCheck"));
    privilegeUpdateCheck_->setObjectName(QStringLiteral("adminPrivilegeUpdateCheck"));
    privilegeDeleteCheck_->setObjectName(QStringLiteral("adminPrivilegeDeleteCheck"));
    contextLabel_->setObjectName(QStringLiteral("adminContextLabel"));
    tabs_->setObjectName(QStringLiteral("adminTabs"));
    contextLabel_->setWordWrap(true);

    databaseEdit_->setPlaceholderText(tr("database"));
    tableEdit_->setPlaceholderText(tr("table"));
    tableColumnsTable_->setColumnCount(5);
    tableColumnsTable_->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Not Null"), tr("Primary Key"), QString()});
    tableColumnsTable_->setMinimumHeight(150);
    tableColumnsTable_->verticalHeader()->setVisible(false);
    tableColumnsTable_->horizontalHeader()->setStretchLastSection(false);
    tableColumnsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tableColumnsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tableColumnsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tableColumnsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tableColumnsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    deleteWhereEdit_->setPlaceholderText(tr("id = 1  (leave empty to delete all rows)"));
    indexEdit_->setPlaceholderText(tr("index name"));
    indexColumnsEdit_->setPlaceholderText(tr("col1, col2"));

    auto *tableColumnsHelp = new QLabel(tr("Fields for the focused table. Add rows for a new table; existing rows show the selected table schema."), this);
    tableColumnsHelp->setWordWrap(true);
    tableColumnsHelp->setObjectName(QStringLiteral("adminTableColumnsHelp"));
    auto *addColumnSpecButton = new QPushButton(tr("+"), this);
    auto *applyColumnSpecButton = new QPushButton(tr("Apply Columns"), this);
    addColumnSpecButton->setObjectName(QStringLiteral("adminAddColumnSpecButton"));
    applyColumnSpecButton->setObjectName(QStringLiteral("adminApplyColumnsButton"));
    addColumnSpecButton->setFixedWidth(36);
    connect(addColumnSpecButton, &QPushButton::clicked, this, [this]() {
        addColumnSpecRow();
    });
    connect(applyColumnSpecButton, &QPushButton::clicked, this, &AdminPanel::applyColumnSpecChanges);

    auto *databaseForm = new QFormLayout;
    databaseForm->addRow(tr("Database"), databaseEdit_);
    databaseForm->addRow(tr("Table"), tableEdit_);
    databaseForm->addRow(tr("Columns"), tableColumnsTable_);
    auto *columnEditButtons = new QHBoxLayout;
    columnEditButtons->addWidget(addColumnSpecButton);
    columnEditButtons->addWidget(applyColumnSpecButton);
    columnEditButtons->addStretch();
    databaseForm->addRow(QString(), columnEditButtons);
    databaseForm->addRow(QString(), tableColumnsHelp);
    databaseForm->addRow(tr("Delete WHERE"), deleteWhereEdit_);

    auto *useDbButton = new QPushButton(tr("Use DB"), this);
    auto *createDbButton = new QPushButton(tr("Create DB"), this);
    auto *dropDbButton = new QPushButton(tr("Drop DB"), this);
    auto *createTableButton = new QPushButton(tr("Create Table"), this);
    auto *dropTableButton = new QPushButton(tr("Drop Table"), this);
    auto *deleteRowsButton = new QPushButton(tr("Delete Rows"), this);
    auto *createIndexButton = new QPushButton(tr("Create Index"), this);
    auto *dropIndexButton = new QPushButton(tr("Drop Index"), this);
    useDbButton->setObjectName(QStringLiteral("adminUseDbButton"));
    createDbButton->setObjectName(QStringLiteral("adminCreateDbButton"));
    dropDbButton->setObjectName(QStringLiteral("adminDropDbButton"));
    createTableButton->setObjectName(QStringLiteral("adminCreateTableButton"));
    dropTableButton->setObjectName(QStringLiteral("adminDropTableButton"));
    deleteRowsButton->setObjectName(QStringLiteral("adminDeleteRowsButton"));
    createIndexButton->setObjectName(QStringLiteral("adminCreateIndexButton"));
    dropIndexButton->setObjectName(QStringLiteral("adminDropIndexButton"));

    const auto polishButton = [](QPushButton *button) {
        button->setMinimumHeight(30);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    };
    for (auto *button : {useDbButton, createDbButton, dropDbButton, createTableButton, dropTableButton,
                         deleteRowsButton, applyColumnSpecButton, createIndexButton, dropIndexButton})
    {
        polishButton(button);
    }

    connect(useDbButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("USE %1").arg(databaseEdit_->text().trimmed()));
    });
    connect(createDbButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("CREATE DATABASE %1").arg(databaseEdit_->text().trimmed()));
    });
    connect(dropDbButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("DROP DATABASE %1").arg(databaseEdit_->text().trimmed()));
    });
    connect(createTableButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("CREATE TABLE %1 (%2)")
                           .arg(targetName(), createTableColumnsSql()));
    });
    connect(dropTableButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("DROP TABLE %1").arg(targetName()));
    });
    connect(deleteRowsButton, &QPushButton::clicked, this, [this]() {
        const QString where = deleteWhereEdit_->text().trimmed();
        emitIfNotEmpty(where.isEmpty()
                           ? QStringLiteral("DELETE FROM %1").arg(targetName())
                           : QStringLiteral("DELETE FROM %1 WHERE %2").arg(targetName(), where));
    });
    connect(createIndexButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("CREATE INDEX %1 ON %2 (%3)")
                           .arg(indexEdit_->text().trimmed(), targetName(), indexColumnsEdit_->text().trimmed()));
    });
    connect(dropIndexButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("DROP INDEX %1 ON %2")
                           .arg(indexEdit_->text().trimmed(), targetName()));
    });

    auto *databaseGroup = new QGroupBox(tr("Focused Object"), this);
    auto *databaseLayout = new QVBoxLayout(databaseGroup);
    databaseLayout->addLayout(databaseForm);
    auto *databaseButtons = new QGridLayout;
    databaseButtons->addWidget(useDbButton, 0, 0);
    databaseButtons->addWidget(createDbButton, 0, 1);
    databaseButtons->addWidget(dropDbButton, 1, 0);
    databaseButtons->addWidget(createTableButton, 1, 1);
    databaseButtons->addWidget(dropTableButton, 2, 0);
    databaseButtons->addWidget(deleteRowsButton, 2, 1);
    databaseLayout->addLayout(databaseButtons);

    auto *schemaForm = new QFormLayout;
    auto *schemaHelp = new QLabel(tr("Use the Object tab column table to add, edit, or remove columns, then click Apply Columns."), this);
    schemaHelp->setWordWrap(true);
    schemaHelp->setObjectName(QStringLiteral("adminStructureHelp"));
    schemaForm->addRow(QString(), schemaHelp);
    schemaForm->addRow(tr("Index"), indexEdit_);
    schemaForm->addRow(tr("Index Columns"), indexColumnsEdit_);

    auto *schemaGroup = new QGroupBox(tr("Columns / Indexes"), this);
    auto *schemaLayout = new QVBoxLayout(schemaGroup);
    schemaLayout->addLayout(schemaForm);
    auto *schemaButtons = new QGridLayout;
    schemaButtons->addWidget(createIndexButton, 0, 0);
    schemaButtons->addWidget(dropIndexButton, 0, 1);
    schemaLayout->addLayout(schemaButtons);

    passwordEdit_->setEchoMode(QLineEdit::Password);
    privilegeTargetDbEdit_->setPlaceholderText(tr("* or database"));
    privilegeTargetTableEdit_->setPlaceholderText(tr("* or table"));
    privilegeCombo_->addItems({QStringLiteral("ALL"), QStringLiteral("SELECT"), QStringLiteral("INSERT"),
                               QStringLiteral("UPDATE"), QStringLiteral("DELETE")});
    privilegeCombo_->setVisible(false);

    auto *privilegeHelp = new QLabel(
        tr("Grant or revoke permissions for the selected user. Target DB/Table accepts exact names or *; for example demo.* applies to all tables in demo, and *.* is global."),
        this);
    privilegeHelp->setObjectName(QStringLiteral("adminPrivilegeHelp"));
    privilegeHelp->setWordWrap(true);

    auto *userForm = new QFormLayout;
    userForm->addRow(tr("User"), userEdit_);
    userForm->addRow(tr("Password"), passwordEdit_);
    userForm->addRow(tr("Target DB"), privilegeTargetDbEdit_);
    userForm->addRow(tr("Target Table"), privilegeTargetTableEdit_);
    userForm->addRow(QString(), privilegeHelp);

    auto *privilegeChecks = new QGridLayout;
    privilegeChecks->addWidget(privilegeAllCheck_, 0, 0);
    privilegeChecks->addWidget(privilegeSelectCheck_, 0, 1);
    privilegeChecks->addWidget(privilegeInsertCheck_, 1, 0);
    privilegeChecks->addWidget(privilegeUpdateCheck_, 1, 1);
    privilegeChecks->addWidget(privilegeDeleteCheck_, 2, 0);
    userForm->addRow(tr("Privileges"), privilegeChecks);
    connect(privilegeAllCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        for (auto *box : {privilegeSelectCheck_, privilegeInsertCheck_, privilegeUpdateCheck_, privilegeDeleteCheck_})
            box->setEnabled(!checked);
    });

    auto *createUserButton = new QPushButton(tr("Create User"), this);
    auto *dropUserButton = new QPushButton(tr("Drop User"), this);
    auto *grantButton = new QPushButton(tr("Grant"), this);
    auto *revokeButton = new QPushButton(tr("Revoke"), this);
    auto *refreshButton = new QPushButton(tr("Refresh Tree"), this);
    createUserButton->setObjectName(QStringLiteral("adminCreateUserButton"));
    dropUserButton->setObjectName(QStringLiteral("adminDropUserButton"));
    grantButton->setObjectName(QStringLiteral("adminGrantButton"));
    revokeButton->setObjectName(QStringLiteral("adminRevokeButton"));
    refreshButton->setObjectName(QStringLiteral("adminRefreshTreeButton"));
    for (auto *button : {createUserButton, dropUserButton, grantButton, revokeButton, refreshButton})
    {
        polishButton(button);
    }

    connect(createUserButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("CREATE USER %1 IDENTIFIED BY %2")
                           .arg(quoteString(userEdit_->text().trimmed()),
                                quoteString(passwordEdit_->text())));
    });
    connect(dropUserButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("DROP USER %1").arg(quoteString(userEdit_->text().trimmed())));
    });
    connect(grantButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("GRANT %1 ON %2.%3 TO %4")
                           .arg(privilegesSql(),
                                privilegeTargetDbEdit_->text().trimmed().isEmpty() ? QStringLiteral("*") : privilegeTargetDbEdit_->text().trimmed(),
                                privilegeTargetTableEdit_->text().trimmed().isEmpty() ? QStringLiteral("*") : privilegeTargetTableEdit_->text().trimmed(),
                                quoteString(userEdit_->text().trimmed())));
    });
    connect(revokeButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("REVOKE %1 ON %2.%3 FROM %4")
                           .arg(privilegesSql(),
                                privilegeTargetDbEdit_->text().trimmed().isEmpty() ? QStringLiteral("*") : privilegeTargetDbEdit_->text().trimmed(),
                                privilegeTargetTableEdit_->text().trimmed().isEmpty() ? QStringLiteral("*") : privilegeTargetTableEdit_->text().trimmed(),
                                quoteString(userEdit_->text().trimmed())));
    });
    connect(refreshButton, &QPushButton::clicked, this, &AdminPanel::refreshRequested);

    auto *userGroup = new QGroupBox(tr("Users / Privileges"), this);
    auto *userLayout = new QVBoxLayout(userGroup);
    userLayout->addLayout(userForm);
    auto *userButtons = new QGridLayout;
    userButtons->addWidget(createUserButton, 0, 0);
    userButtons->addWidget(dropUserButton, 0, 1);
    userButtons->addWidget(grantButton, 1, 0);
    userButtons->addWidget(revokeButton, 1, 1);
    userButtons->addWidget(refreshButton, 2, 0, 1, 2);
    userLayout->addLayout(userButtons);

    tabs_->addTab(databaseGroup, tr("Object"));
    tabs_->addTab(schemaGroup, tr("Structure"));
    tabs_->addTab(userGroup, tr("Users"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(contextLabel_);
    layout->addWidget(tabs_);
}

void AdminPanel::setCurrentDatabase(const QString &database)
{
    databaseEdit_->setText(database);
    tableEdit_->clear();
    deleteWhereEdit_->clear();
    clearColumnSpecRows();
    originalColumnSpecs_.clear();
    privilegeTargetDbEdit_->setText(database);
    privilegeTargetTableEdit_->setText(QStringLiteral("*"));
    contextLabel_->setText(tr("Database: %1").arg(database));
    tabs_->setCurrentIndex(0);
}

void AdminPanel::setCurrentTable(const QString &database, const QString &table)
{
    databaseEdit_->setText(database);
    tableEdit_->setText(table);
    privilegeTargetDbEdit_->setText(database);
    privilegeTargetTableEdit_->setText(table);
    contextLabel_->setText(tr("Table: %1.%2").arg(database, table));
    tabs_->setCurrentIndex(0);
}

void AdminPanel::setCurrentTable(const QString &database, const QString &table, const QStringList &columns)
{
    setCurrentTable(database, table);
    clearColumnSpecRows();
    for (const QString &column : columns)
    {
        const QStringList parts = column.split('\t');
        const QString name = parts.value(0);
        const QString type = parts.value(1);
        const QString nullable = parts.value(2);
        const QString key = parts.value(3);
        addColumnSpecRow(name, type, nullable.compare(QStringLiteral("NO"), Qt::CaseInsensitive) == 0,
                         key.compare(QStringLiteral("PRI"), Qt::CaseInsensitive) == 0 ||
                             key.compare(QStringLiteral("PRIMARY"), Qt::CaseInsensitive) == 0);
    }
    originalColumnSpecs_.clear();
    for (int row = 0; row < tableColumnsTable_->rowCount(); ++row)
    {
        const QString spec = columnSpecFromRow(row);
        if (!spec.isEmpty())
            originalColumnSpecs_ << spec;
    }
}

void AdminPanel::setCurrentStructure(const QString &database, const QString &table, const QStringList &columns)
{
    setCurrentTable(database, table, columns);
    tabs_->setCurrentIndex(1);
}

QString AdminPanel::targetName() const
{
    const QString db = databaseEdit_->text().trimmed();
    const QString table = tableEdit_->text().trimmed();
    return db.isEmpty() ? table : db + QStringLiteral(".") + table;
}

QString AdminPanel::quoteString(const QString &value) const
{
    QString escaped = value;
    escaped.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'") + escaped + QStringLiteral("'");
}

QString AdminPanel::createTableColumnsSql() const
{
    QStringList columns;
    for (int row = 0; row < tableColumnsTable_->rowCount(); ++row)
    {
        const QString definition = columnDefinitionFromRow(row);
        if (!definition.isEmpty())
            columns << definition;
    }
    return columns.join(QStringLiteral(", "));
}

QString AdminPanel::columnDefinitionFromRow(int row) const
{
    const auto *nameItem = tableColumnsTable_->item(row, 0);
    const auto *typeItem = tableColumnsTable_->item(row, 1);
    const QString name = nameItem ? nameItem->text().trimmed() : QString();
    const QString type = typeItem ? typeItem->text().trimmed() : QString();
    if (name.isEmpty() || type.isEmpty())
        return QString();

    QStringList parts{name, type};
    if (auto *notNull = qobject_cast<QCheckBox *>(tableColumnsTable_->cellWidget(row, 2)); notNull && notNull->isChecked())
        parts << QStringLiteral("NOT NULL");
    if (auto *primaryKey = qobject_cast<QCheckBox *>(tableColumnsTable_->cellWidget(row, 3)); primaryKey && primaryKey->isChecked())
        parts << QStringLiteral("PRIMARY KEY");
    return parts.join(QStringLiteral(" "));
}

QString AdminPanel::columnSpecFromRow(int row) const
{
    const auto *nameItem = tableColumnsTable_->item(row, 0);
    const auto *typeItem = tableColumnsTable_->item(row, 1);
    const QString name = nameItem ? nameItem->text().trimmed() : QString();
    const QString type = typeItem ? typeItem->text().trimmed() : QString();
    if (name.isEmpty() || type.isEmpty())
        return QString();

    const bool notNull = qobject_cast<QCheckBox *>(tableColumnsTable_->cellWidget(row, 2)) &&
                         qobject_cast<QCheckBox *>(tableColumnsTable_->cellWidget(row, 2))->isChecked();
    const bool primaryKey = qobject_cast<QCheckBox *>(tableColumnsTable_->cellWidget(row, 3)) &&
                            qobject_cast<QCheckBox *>(tableColumnsTable_->cellWidget(row, 3))->isChecked();
    return QStringList{name, type, notNull ? QStringLiteral("NO") : QStringLiteral("YES"),
                       primaryKey ? QStringLiteral("PRI") : QString()}
        .join('\t');
}

QString AdminPanel::columnNameFromSpec(const QString &spec) const
{
    return spec.split('\t').value(0).trimmed();
}

void AdminPanel::addColumnSpecRow(const QString &name, const QString &type, bool notNull, bool primaryKey)
{
    const int row = tableColumnsTable_->rowCount();
    tableColumnsTable_->insertRow(row);
    tableColumnsTable_->setItem(row, 0, new QTableWidgetItem(name));
    tableColumnsTable_->setItem(row, 1, new QTableWidgetItem(type.isEmpty() ? QStringLiteral("INT") : type));

    auto *notNullCheck = new QCheckBox(tableColumnsTable_);
    notNullCheck->setObjectName(QStringLiteral("adminColumnSpecNotNullCheck"));
    notNullCheck->setChecked(notNull);
    tableColumnsTable_->setCellWidget(row, 2, notNullCheck);

    auto *primaryKeyCheck = new QCheckBox(tableColumnsTable_);
    primaryKeyCheck->setObjectName(QStringLiteral("adminColumnSpecPrimaryKeyCheck"));
    primaryKeyCheck->setChecked(primaryKey);
    tableColumnsTable_->setCellWidget(row, 3, primaryKeyCheck);

    auto *removeButton = new QPushButton(tr("-"), tableColumnsTable_);
    removeButton->setObjectName(QStringLiteral("adminRemoveColumnSpecButton"));
    removeButton->setFixedWidth(32);
    connect(removeButton, &QPushButton::clicked, this, [this, removeButton]() {
        for (int row = 0; row < tableColumnsTable_->rowCount(); ++row)
        {
            if (tableColumnsTable_->cellWidget(row, 4) == removeButton)
            {
                tableColumnsTable_->removeRow(row);
                return;
            }
        }
    });
    tableColumnsTable_->setCellWidget(row, 4, removeButton);
}

void AdminPanel::clearColumnSpecRows()
{
    tableColumnsTable_->setRowCount(0);
}

void AdminPanel::applyColumnSpecChanges()
{
    if (tableEdit_->text().trimmed().isEmpty())
        return;

    QMap<QString, QString> originalByName;
    for (const QString &spec : originalColumnSpecs_)
    {
        const QString name = columnNameFromSpec(spec);
        if (!name.isEmpty())
            originalByName.insert(name, spec);
    }

    QMap<QString, QString> currentByName;
    QMap<QString, QString> currentDefinitionByName;
    for (int row = 0; row < tableColumnsTable_->rowCount(); ++row)
    {
        const QString spec = columnSpecFromRow(row);
        const QString definition = columnDefinitionFromRow(row);
        const QString name = columnNameFromSpec(spec);
        if (name.isEmpty() || definition.isEmpty())
            continue;
        currentByName.insert(name, spec);
        currentDefinitionByName.insert(name, definition);
    }

    for (auto it = originalByName.cbegin(); it != originalByName.cend(); ++it)
    {
        if (!currentByName.contains(it.key()))
            emitIfNotEmpty(QStringLiteral("ALTER TABLE %1 DROP COLUMN %2").arg(targetName(), it.key()));
    }

    for (auto it = currentByName.cbegin(); it != currentByName.cend(); ++it)
    {
        if (!originalByName.contains(it.key()))
        {
            emitIfNotEmpty(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2")
                               .arg(targetName(), currentDefinitionByName.value(it.key())));
        }
        else if (originalByName.value(it.key()) != it.value())
        {
            emitIfNotEmpty(QStringLiteral("ALTER TABLE %1 MODIFY COLUMN %2")
                               .arg(targetName(), currentDefinitionByName.value(it.key())));
        }
    }

    originalColumnSpecs_ = currentByName.values();
}

QString AdminPanel::privilegesSql() const
{
    if (privilegeAllCheck_->isChecked())
        return QStringLiteral("ALL");

    QStringList privileges;
    if (privilegeSelectCheck_->isChecked())
        privileges << QStringLiteral("SELECT");
    if (privilegeInsertCheck_->isChecked())
        privileges << QStringLiteral("INSERT");
    if (privilegeUpdateCheck_->isChecked())
        privileges << QStringLiteral("UPDATE");
    if (privilegeDeleteCheck_->isChecked())
        privileges << QStringLiteral("DELETE");

    return privileges.isEmpty() ? privilegeCombo_->currentText() : privileges.join(QStringLiteral(", "));
}

void AdminPanel::emitIfNotEmpty(const QString &sql)
{
    const QString normalized = sql.simplified();
    if (!normalized.isEmpty())
        emit sqlRequested(normalized);
}
