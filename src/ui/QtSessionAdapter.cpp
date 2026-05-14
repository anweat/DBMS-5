#include "QtSessionAdapter.h"

QtSessionAdapter::QtSessionAdapter(const QString &dataDir, QObject *parent)
    : QObject(parent),
      engine_(dataDir.toStdString())
{
    emitSessionChanged();
}

const Session &QtSessionAdapter::session() const
{
    return session_;
}

void QtSessionAdapter::executeSql(const QString &sql)
{
    QueryResult result = engine_.execute(sql.toStdString(), session_);
    emit resultReady(result);
    emitSessionChanged();
}

void QtSessionAdapter::refreshCatalog()
{
    CatalogSnapshot catalog;
    const std::string originalDb = session_.currentDatabase;

    QueryResult dbs = engine_.execute("SHOW DATABASES", session_);
    if (dbs.type == QueryResult::Type::ERROR)
    {
        emit resultReady(dbs);
        emitSessionChanged();
        return;
    }

    for (const Row &dbRow : dbs.rows)
    {
        if (dbRow.empty() || !std::holds_alternative<std::string>(dbRow[0]))
            continue;

        CatalogDatabase db;
        db.name = QString::fromStdString(std::get<std::string>(dbRow[0]));

        engine_.execute("USE " + db.name.toStdString(), session_);
        QueryResult tables = engine_.execute("SHOW TABLES", session_);
        if (tables.type != QueryResult::Type::ERROR)
        {
            for (const Row &tableRow : tables.rows)
            {
                if (tableRow.empty() || !std::holds_alternative<std::string>(tableRow[0]))
                    continue;

                CatalogTable table;
                table.name = QString::fromStdString(std::get<std::string>(tableRow[0]));

                QueryResult desc = engine_.execute("DESCRIBE " + table.name.toStdString(), session_);
                if (desc.type != QueryResult::Type::ERROR)
                {
                    for (const Row &colRow : desc.rows)
                    {
                        CatalogColumn column;
                        if (colRow.size() > 0 && std::holds_alternative<std::string>(colRow[0]))
                            column.name = QString::fromStdString(std::get<std::string>(colRow[0]));
                        if (colRow.size() > 1 && std::holds_alternative<std::string>(colRow[1]))
                            column.type = QString::fromStdString(std::get<std::string>(colRow[1]));
                        if (colRow.size() > 2 && std::holds_alternative<std::string>(colRow[2]))
                            column.nullable = QString::fromStdString(std::get<std::string>(colRow[2]));
                        if (colRow.size() > 3 && std::holds_alternative<std::string>(colRow[3]))
                            column.key = QString::fromStdString(std::get<std::string>(colRow[3]));
                        table.columns.push_back(column);
                    }
                }

                QueryResult indexes = engine_.execute("SHOW INDEXES FROM " + table.name.toStdString(), session_);
                if (indexes.type != QueryResult::Type::ERROR)
                {
                    for (const Row &idxRow : indexes.rows)
                    {
                        QString indexName;
                        QString columns;
                        if (idxRow.size() > 0 && std::holds_alternative<std::string>(idxRow[0]))
                            indexName = QString::fromStdString(std::get<std::string>(idxRow[0]));
                        if (idxRow.size() > 2 && std::holds_alternative<std::string>(idxRow[2]))
                            columns = QString::fromStdString(std::get<std::string>(idxRow[2]));
                        if (!indexName.isEmpty())
                            table.indexes.push_back(columns.isEmpty()
                                                        ? indexName
                                                        : indexName + QStringLiteral("  (") + columns + QStringLiteral(")"));
                    }
                }
                db.tables.push_back(table);
            }
        }
        catalog.databases.push_back(db);
    }

    if (!originalDb.empty())
        engine_.execute("USE " + originalDb, session_);

    QueryResult users = engine_.execute("SHOW USERS", session_);
    if (users.type != QueryResult::Type::ERROR)
    {
        for (const Row &userRow : users.rows)
        {
            CatalogUser user;
            if (userRow.size() > 0 && std::holds_alternative<std::string>(userRow[0]))
                user.name = QString::fromStdString(std::get<std::string>(userRow[0]));
            if (userRow.size() > 1 && std::holds_alternative<std::string>(userRow[1]))
                user.privileges = QString::fromStdString(std::get<std::string>(userRow[1]));
            catalog.users.push_back(user);
        }
    }

    emit catalogReady(catalog);
    emitSessionChanged();
}

void QtSessionAdapter::loadTable(const QString &database, const QString &table)
{
    if (!database.isEmpty())
        engine_.execute("USE " + database.toStdString(), session_);
    QueryResult result = engine_.execute("SELECT * FROM " + table.toStdString(), session_);
    emit tableReady(database, table, result);
    emit resultReady(result);
    emitSessionChanged();
}

void QtSessionAdapter::connectUser(const QString &user, const QString &password)
{
    const QString escapedUser = user;
    QString escapedPassword = password;
    escapedPassword.replace("'", "''");

    QString escapedName = escapedUser;
    escapedName.replace("'", "''");

    executeSql(QString("CONNECT '%1' IDENTIFIED BY '%2'").arg(escapedName, escapedPassword));
}

void QtSessionAdapter::emitSessionChanged()
{
    emit sessionChanged(QString::fromStdString(session_.user),
                        QString::fromStdString(session_.currentDatabase),
                        !session_.transactionId.empty());
}
