#include "AdminPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

AdminPanel::AdminPanel(QWidget *parent)
    : QWidget(parent),
      databaseEdit_(new QLineEdit(this)),
      tableEdit_(new QLineEdit(this)),
      columnEdit_(new QLineEdit(this)),
      columnTypeEdit_(new QLineEdit(this)),
      defaultEdit_(new QLineEdit(this)),
      notNullCheck_(new QCheckBox(tr("NOT NULL"), this)),
      indexEdit_(new QLineEdit(this)),
      indexColumnsEdit_(new QLineEdit(this)),
      userEdit_(new QLineEdit(this)),
      passwordEdit_(new QLineEdit(this)),
      privilegeTargetDbEdit_(new QLineEdit(this)),
      privilegeTargetTableEdit_(new QLineEdit(this)),
      privilegeCombo_(new QComboBox(this))
{
    databaseEdit_->setObjectName(QStringLiteral("adminDatabaseEdit"));
    tableEdit_->setObjectName(QStringLiteral("adminTableEdit"));
    columnEdit_->setObjectName(QStringLiteral("adminColumnEdit"));
    columnTypeEdit_->setObjectName(QStringLiteral("adminColumnTypeEdit"));
    defaultEdit_->setObjectName(QStringLiteral("adminDefaultEdit"));
    notNullCheck_->setObjectName(QStringLiteral("adminNotNullCheck"));
    indexEdit_->setObjectName(QStringLiteral("adminIndexEdit"));
    indexColumnsEdit_->setObjectName(QStringLiteral("adminIndexColumnsEdit"));
    userEdit_->setObjectName(QStringLiteral("adminUserEdit"));
    passwordEdit_->setObjectName(QStringLiteral("adminPasswordEdit"));
    privilegeTargetDbEdit_->setObjectName(QStringLiteral("adminPrivilegeDbEdit"));
    privilegeTargetTableEdit_->setObjectName(QStringLiteral("adminPrivilegeTableEdit"));
    privilegeCombo_->setObjectName(QStringLiteral("adminPrivilegeCombo"));

    databaseEdit_->setPlaceholderText(tr("database"));
    tableEdit_->setPlaceholderText(tr("table"));
    columnEdit_->setPlaceholderText(tr("column"));
    columnTypeEdit_->setPlaceholderText(tr("INT / VARCHAR(64) / DOUBLE"));
    defaultEdit_->setPlaceholderText(tr("optional SQL literal"));
    indexEdit_->setPlaceholderText(tr("index name"));
    indexColumnsEdit_->setPlaceholderText(tr("col1, col2"));

    auto *schemaForm = new QFormLayout;
    schemaForm->addRow(tr("Database"), databaseEdit_);
    schemaForm->addRow(tr("Table"), tableEdit_);
    schemaForm->addRow(tr("Column"), columnEdit_);
    schemaForm->addRow(tr("Type"), columnTypeEdit_);
    schemaForm->addRow(tr("Default"), defaultEdit_);
    schemaForm->addRow(QString(), notNullCheck_);
    schemaForm->addRow(tr("Index"), indexEdit_);
    schemaForm->addRow(tr("Index Columns"), indexColumnsEdit_);

    auto *useDbButton = new QPushButton(tr("Use DB"), this);
    auto *createDbButton = new QPushButton(tr("Create DB"), this);
    auto *dropDbButton = new QPushButton(tr("Drop DB"), this);
    auto *addColumnButton = new QPushButton(tr("Add Column"), this);
    auto *modifyColumnButton = new QPushButton(tr("Modify Column"), this);
    auto *dropColumnButton = new QPushButton(tr("Drop Column"), this);
    auto *createIndexButton = new QPushButton(tr("Create Index"), this);
    auto *dropIndexButton = new QPushButton(tr("Drop Index"), this);
    useDbButton->setObjectName(QStringLiteral("adminUseDbButton"));
    createDbButton->setObjectName(QStringLiteral("adminCreateDbButton"));
    dropDbButton->setObjectName(QStringLiteral("adminDropDbButton"));
    addColumnButton->setObjectName(QStringLiteral("adminAddColumnButton"));
    modifyColumnButton->setObjectName(QStringLiteral("adminModifyColumnButton"));
    dropColumnButton->setObjectName(QStringLiteral("adminDropColumnButton"));
    createIndexButton->setObjectName(QStringLiteral("adminCreateIndexButton"));
    dropIndexButton->setObjectName(QStringLiteral("adminDropIndexButton"));

    connect(useDbButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("USE %1").arg(databaseEdit_->text().trimmed()));
    });
    connect(createDbButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("CREATE DATABASE %1").arg(databaseEdit_->text().trimmed()));
    });
    connect(dropDbButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("DROP DATABASE %1").arg(databaseEdit_->text().trimmed()));
    });
    connect(addColumnButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2")
                           .arg(targetName(), columnDefinitionSql()));
    });
    connect(modifyColumnButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("ALTER TABLE %1 MODIFY COLUMN %2")
                           .arg(targetName(), columnDefinitionSql()));
    });
    connect(dropColumnButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("ALTER TABLE %1 DROP COLUMN %2")
                           .arg(targetName(), columnEdit_->text().trimmed()));
    });
    connect(createIndexButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("CREATE INDEX %1 ON %2 (%3)")
                           .arg(indexEdit_->text().trimmed(), targetName(), indexColumnsEdit_->text().trimmed()));
    });
    connect(dropIndexButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("DROP INDEX %1 ON %2")
                           .arg(indexEdit_->text().trimmed(), targetName()));
    });

    auto *schemaButtons = new QHBoxLayout;
    schemaButtons->addWidget(useDbButton);
    schemaButtons->addWidget(createDbButton);
    schemaButtons->addWidget(dropDbButton);
    schemaButtons->addWidget(addColumnButton);
    schemaButtons->addWidget(modifyColumnButton);
    schemaButtons->addWidget(dropColumnButton);
    schemaButtons->addWidget(createIndexButton);
    schemaButtons->addWidget(dropIndexButton);

    auto *schemaButtonWidget = new QWidget(this);
    schemaButtonWidget->setLayout(schemaButtons);
    auto *schemaButtonScroll = new QScrollArea(this);
    schemaButtonScroll->setWidget(schemaButtonWidget);
    schemaButtonScroll->setWidgetResizable(true);
    schemaButtonScroll->setFrameShape(QFrame::NoFrame);
    schemaButtonScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    schemaButtonScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *schemaGroup = new QGroupBox(tr("Schema"), this);
    auto *schemaLayout = new QVBoxLayout(schemaGroup);
    schemaLayout->addLayout(schemaForm);
    schemaLayout->addWidget(schemaButtonScroll);

    passwordEdit_->setEchoMode(QLineEdit::Password);
    privilegeTargetDbEdit_->setPlaceholderText(tr("* or database"));
    privilegeTargetTableEdit_->setPlaceholderText(tr("* or table"));
    privilegeCombo_->addItems({QStringLiteral("ALL"), QStringLiteral("SELECT"), QStringLiteral("INSERT"),
                               QStringLiteral("UPDATE"), QStringLiteral("DELETE")});

    auto *userForm = new QFormLayout;
    userForm->addRow(tr("User"), userEdit_);
    userForm->addRow(tr("Password"), passwordEdit_);
    userForm->addRow(tr("Privilege"), privilegeCombo_);
    userForm->addRow(tr("Target DB"), privilegeTargetDbEdit_);
    userForm->addRow(tr("Target Table"), privilegeTargetTableEdit_);

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
                           .arg(privilegeCombo_->currentText(),
                                privilegeTargetDbEdit_->text().trimmed().isEmpty() ? QStringLiteral("*") : privilegeTargetDbEdit_->text().trimmed(),
                                privilegeTargetTableEdit_->text().trimmed().isEmpty() ? QStringLiteral("*") : privilegeTargetTableEdit_->text().trimmed(),
                                quoteString(userEdit_->text().trimmed())));
    });
    connect(revokeButton, &QPushButton::clicked, this, [this]() {
        emitIfNotEmpty(QStringLiteral("REVOKE %1 ON %2.%3 FROM %4")
                           .arg(privilegeCombo_->currentText(),
                                privilegeTargetDbEdit_->text().trimmed().isEmpty() ? QStringLiteral("*") : privilegeTargetDbEdit_->text().trimmed(),
                                privilegeTargetTableEdit_->text().trimmed().isEmpty() ? QStringLiteral("*") : privilegeTargetTableEdit_->text().trimmed(),
                                quoteString(userEdit_->text().trimmed())));
    });
    connect(refreshButton, &QPushButton::clicked, this, &AdminPanel::refreshRequested);

    auto *userButtons = new QHBoxLayout;
    userButtons->addWidget(createUserButton);
    userButtons->addWidget(dropUserButton);
    userButtons->addWidget(grantButton);
    userButtons->addWidget(revokeButton);
    userButtons->addWidget(refreshButton);

    auto *userButtonWidget = new QWidget(this);
    userButtonWidget->setLayout(userButtons);
    auto *userButtonScroll = new QScrollArea(this);
    userButtonScroll->setWidget(userButtonWidget);
    userButtonScroll->setWidgetResizable(true);
    userButtonScroll->setFrameShape(QFrame::NoFrame);
    userButtonScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    userButtonScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *userGroup = new QGroupBox(tr("Users / Privileges"), this);
    auto *userLayout = new QVBoxLayout(userGroup);
    userLayout->addLayout(userForm);
    userLayout->addWidget(userButtonScroll);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(schemaGroup);
    layout->addWidget(userGroup);
    layout->addStretch();
}

void AdminPanel::setCurrentTable(const QString &database, const QString &table)
{
    databaseEdit_->setText(database);
    tableEdit_->setText(table);
    privilegeTargetDbEdit_->setText(database);
    privilegeTargetTableEdit_->setText(table);
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

bool AdminPanel::looksLikeRawLiteral(const QString &value) const
{
    if (value.compare(QStringLiteral("NULL"), Qt::CaseInsensitive) == 0 ||
        value.compare(QStringLiteral("TRUE"), Qt::CaseInsensitive) == 0 ||
        value.compare(QStringLiteral("FALSE"), Qt::CaseInsensitive) == 0)
        return true;

    bool ok = false;
    value.toDouble(&ok);
    if (ok)
        return true;

    return (value.size() >= 2 &&
            ((value.startsWith('\'') && value.endsWith('\'')) ||
             (value.startsWith('"') && value.endsWith('"'))));
}

QString AdminPanel::defaultLiteral() const
{
    const QString value = defaultEdit_->text().trimmed();
    if (value.isEmpty())
        return QString();
    if (looksLikeRawLiteral(value))
        return value;
    return quoteString(value);
}

QString AdminPanel::columnDefinitionSql() const
{
    QStringList parts;
    parts << columnEdit_->text().trimmed();
    parts << columnTypeEdit_->text().trimmed();
    const QString defaultValue = defaultLiteral();
    if (!defaultValue.isEmpty())
        parts << QStringLiteral("DEFAULT") << defaultValue;
    if (notNullCheck_->isChecked())
        parts << QStringLiteral("NOT NULL");
    return parts.join(QStringLiteral(" "));
}

void AdminPanel::emitIfNotEmpty(const QString &sql)
{
    const QString normalized = sql.simplified();
    if (!normalized.isEmpty())
        emit sqlRequested(normalized);
}
