#include "Executor.h"
#include "ExprEvaluator.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <set>

// ============================================================
// 构造 / 初始化
// ============================================================

Executor::Executor(const std::string &dataDir)
    : dataDir_(dataDir),
      dbMgr_(dataDir),
      tblMgr_(dataDir),
      recMgr_(dataDir, tblMgr_),
      idxMgr_(dataDir),
      userMgr_(dataDir),
      walMgr_(dataDir)
{
    dbMgr_.init();
    userMgr_.init();
    // ── Crash Recovery ─────────────────────────────────────────────────────
    // On startup, replay undo ops for any transaction that had no COMMIT/ROLLBACK
    auto pending = walMgr_.recover();
    for (auto& [txId, undoOps] : pending) {
        for (const auto& op : undoOps) {
            try {
                switch (op.type) {
                case UndoOpType::INSERT_UNDO:
                    recMgr_.remove(op.db, op.table, op.offset);
                    break;
                case UndoOpType::UPDATE_UNDO:
                case UndoOpType::DELETE_UNDO:
                    recMgr_.update(op.db, op.table, op.offset, op.oldRecord);
                    break;
                }
            } catch (...) {
                // Best-effort recovery: skip ops that fail (e.g., missing file)
            }
        }
    }
    if (!pending.empty())
        walMgr_.compact(); // clean recovered entries from WAL
}

// ============================================================
// 辅助
// ============================================================

std::string Executor::resolveDb(const std::string &nodeDb, const Session &s)
{
    if (!nodeDb.empty())
        return nodeDb;
    if (!s.currentDatabase.empty())
        return s.currentDatabase;
    throw DBException(ErrorCode::DB_NOT_FOUND, "No database selected. Use USE <database> first.");
}

// 辅助：要求会话已认证（未登录则拒绝）
static void requireAuthenticated(const Session &s)
{
    if (s.user.empty())
        throw DBException(ErrorCode::PERMISSION_DENIED,
                          "Not authenticated. Use: CONNECT 'user' IDENTIFIED BY 'password'");
}

void Executor::checkPermission(const Session &s, const std::string &db,
                               const std::string &table, Privilege priv)
{
    requireAuthenticated(s);  // 未登录直接拒绝
    if (!userMgr_.hasPrivilege(s.user, db, table, priv))
    {
        std::string privName;
        switch (priv)
        {
        case Privilege::SELECT:
            privName = "SELECT";
            break;
        case Privilege::INSERT:
            privName = "INSERT";
            break;
        case Privilege::UPDATE:
            privName = "UPDATE";
            break;
        case Privilege::DELETE:
            privName = "DELETE";
            break;
        case Privilege::ALL:
            privName = "ALL";
            break;
        }
        throw DBException(ErrorCode::PERMISSION_DENIED,
                          "Permission denied: user '" + s.user + "' lacks " + privName + " privilege on '" + db + "." + table + "'");
    }
}

// 将 FieldValue 转为展示字符串
static std::string fvToStr(const FieldValue &v)
{
    if (std::holds_alternative<std::monostate>(v))
        return "NULL";
    if (std::holds_alternative<int64_t>(v))
        return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))
        return std::to_string(std::get<double>(v));
    if (std::holds_alternative<bool>(v))
        return std::get<bool>(v) ? "1" : "0";
    if (std::holds_alternative<std::string>(v))
        return std::get<std::string>(v);
    return "";
}

// 构建一行的 map<colName, value>（按列顺序）
static std::map<std::string, FieldValue> rowToMap(const TableDefinition &def, const Row &row)
{
    std::map<std::string, FieldValue> m;
    for (size_t i = 0; i < def.columns.size() && i < row.size(); ++i)
        m[def.columns[i].name] = row[i];
    return m;
}

// 解析默认值字符串为 FieldValue
static FieldValue parseDefault(const ColumnDefinition &col)
{
    if (col.defaultValue.empty())
        return std::monostate{};
    const auto &dv = col.defaultValue;
    switch (col.type)
    {
    case FieldType::INTEGER:
        return static_cast<int64_t>(std::stoll(dv));
    case FieldType::DOUBLE:
        return std::stod(dv);
    case FieldType::BOOL:
        return dv == "TRUE" || dv == "true" || dv == "1";
    case FieldType::VARCHAR:
    case FieldType::DATETIME:
        return dv;
    }
    return std::monostate{};
}

// 强制转型 FieldValue 以匹配列类型
static FieldValue coerce(const FieldValue &v, const ColumnDefinition &col)
{
    if (std::holds_alternative<std::monostate>(v))
        return v;
    switch (col.type)
    {
    case FieldType::INTEGER:
        if (std::holds_alternative<double>(v))
            return static_cast<int64_t>(std::get<double>(v));
        if (std::holds_alternative<bool>(v))
            return static_cast<int64_t>(std::get<bool>(v) ? 1 : 0);
        if (std::holds_alternative<std::string>(v))
            try
            {
                return static_cast<int64_t>(std::stoll(std::get<std::string>(v)));
            }
            catch (...)
            {
            }
        break;
    case FieldType::DOUBLE:
        if (std::holds_alternative<int64_t>(v))
            return static_cast<double>(std::get<int64_t>(v));
        if (std::holds_alternative<bool>(v))
            return std::get<bool>(v) ? 1.0 : 0.0;
        if (std::holds_alternative<std::string>(v))
            try
            {
                return std::stod(std::get<std::string>(v));
            }
            catch (...)
            {
            }
        break;
    case FieldType::BOOL:
        if (std::holds_alternative<int64_t>(v))
            return std::get<int64_t>(v) != 0;
        if (std::holds_alternative<double>(v))
            return std::get<double>(v) != 0.0;
        break;
    default:
        break;
    }
    return v;
}

// ============================================================
// 完整性约束检查
// ============================================================

// 比较两个 FieldValue 是否相等（用于约束检查）
static bool fvEqual(const FieldValue &a, const FieldValue &b)
{
    if (a.index() != b.index())
        return false;
    if (std::holds_alternative<std::monostate>(a))
        return true; // NULL==NULL 在约束中视为不同
    if (std::holds_alternative<int64_t>(a))
        return std::get<int64_t>(a) == std::get<int64_t>(b);
    if (std::holds_alternative<double>(a))
        return std::get<double>(a) == std::get<double>(b);
    if (std::holds_alternative<bool>(a))
        return std::get<bool>(a) == std::get<bool>(b);
    if (std::holds_alternative<std::string>(a))
        return std::get<std::string>(a) == std::get<std::string>(b);
    return false;
}

// 检查唯一性 / PK 约束；excludeOffset=-1 表示不排除任何行（INSERT）
// excludeOffset >= 0 表示跳过该物理偏移的行（UPDATE）
static void checkUniqueConstraints(
    RecordManager &recMgr,
    const std::string &db,
    const std::string &table,
    const TableDefinition &def,
    const std::map<std::string, FieldValue> &record,
    int64_t excludeOffset = -1)
{
    // 只检查有 PK 或 UNIQUE 的列
    std::vector<const ColumnDefinition *> keyCols;
    for (const auto &col : def.columns)
        if (col.primaryKey || col.unique)
            keyCols.push_back(&col);
    if (keyCols.empty())
        return;

    // NULL 值不参与唯一性检查（SQL 标准：NULL != NULL）
    for (const auto *kc : keyCols)
    {
        auto it = record.find(kc->name);
        if (it == record.end() || std::holds_alternative<std::monostate>(it->second))
            continue; // NULL 不检查
    }

    auto withOffsets = recMgr.scanWithOffsets(db, table);
    for (const auto &[offset, row] : withOffsets)
    {
        if (offset == excludeOffset)
            continue;
        // 构建 map
        std::map<std::string, FieldValue> existing;
        for (size_t i = 0; i < def.columns.size() && i < row.size(); ++i)
            existing[def.columns[i].name] = row[i];

        for (const auto *kc : keyCols)
        {
            auto newIt = record.find(kc->name);
            if (newIt == record.end())
                continue;
            if (std::holds_alternative<std::monostate>(newIt->second))
                continue;
            auto exIt = existing.find(kc->name);
            if (exIt == existing.end())
                continue;
            if (fvEqual(newIt->second, exIt->second))
            {
                std::string kind = kc->primaryKey ? "PRIMARY KEY" : "UNIQUE";
                throw DBException(ErrorCode::DUPLICATE_KEY,
                                  "Duplicate entry for " + kind + " column '" + kc->name + "'");
            }
        }
    }
}

// VARCHAR 长度检查 + 截断
static FieldValue validateField(const FieldValue &v, const ColumnDefinition &col)
{
    if (col.type == FieldType::VARCHAR && std::holds_alternative<std::string>(v))
    {
        const auto &s = std::get<std::string>(v);
        if (col.length > 0 && static_cast<int>(s.size()) > col.length)
            throw DBException(ErrorCode::COLUMN_INVALID,
                              "Value too long for column '" + col.name + "' (max " + std::to_string(col.length) + ")");
    }
    return v;
}

// ============================================================
// execute 入口：按 AST 类型分发
// ============================================================

QueryResult Executor::execute(const ASTNode &ast, Session &session)
{
    switch (ast.type)
    {
    // DDL – 数据库
    case NodeType::CREATE_DATABASE:
        return execCreateDatabase(static_cast<const CreateDatabaseNode &>(ast), session);
    case NodeType::DROP_DATABASE:
        return execDropDatabase(static_cast<const DropDatabaseNode &>(ast), session);
    case NodeType::SHOW_DATABASES:
        return execShowDatabases(session);
    case NodeType::USE_DATABASE:
        return execUseDatabase(static_cast<const UseDatabaseNode &>(ast), session);
    // DDL – 表
    case NodeType::CREATE_TABLE:
        return execCreateTable(static_cast<const CreateTableNode &>(ast), session);
    case NodeType::DROP_TABLE:
        return execDropTable(static_cast<const DropTableNode &>(ast), session);
    case NodeType::SHOW_TABLES:
        return execShowTables(session);
    case NodeType::DESCRIBE_TABLE:
        return execDescribeTable(static_cast<const DescribeTableNode &>(ast), session);
    case NodeType::ALTER_TABLE:
        return execAlterTable(static_cast<const AlterTableNode &>(ast), session);
    // DDL – 索引
    case NodeType::CREATE_INDEX:
        return execCreateIndex(static_cast<const CreateIndexNode &>(ast), session);
    case NodeType::DROP_INDEX:
        return execDropIndex(static_cast<const DropIndexNode &>(ast), session);
    // DML
    case NodeType::INSERT:
        return execInsert(static_cast<const InsertNode &>(ast), session);
    case NodeType::SELECT:
        return execSelect(static_cast<const SelectNode &>(ast), session);
    case NodeType::UPDATE:
        return execUpdate(static_cast<const UpdateNode &>(ast), session);
    case NodeType::DELETE:
        return execDelete(static_cast<const DeleteNode &>(ast), session);
    // 事务
    case NodeType::BEGIN_TRANSACTION:
        return execBegin(session);
    case NodeType::COMMIT:
        return execCommit(session);
    case NodeType::ROLLBACK:
        return execRollback(session);
    // 安全
    case NodeType::CREATE_USER:
        return execCreateUser(static_cast<const CreateUserNode &>(ast), session);
    case NodeType::DROP_USER:
        return execDropUser(static_cast<const DropUserNode &>(ast), session);
    case NodeType::GRANT:
        return execGrant(static_cast<const GrantNode &>(ast), session);
    case NodeType::REVOKE:
        return execRevoke(static_cast<const RevokeNode &>(ast), session);
    case NodeType::CONNECT:
        return execConnect(static_cast<const ConnectNode &>(ast), session);
    case NodeType::BACKUP_DATABASE:
        return execBackupDatabase(static_cast<const BackupDatabaseNode &>(ast), session);
    default:
        throw DBException(ErrorCode::UNKNOWN_ERROR,
                          "Unsupported statement type");
    }
}

// ============================================================
// DDL – 数据库
// ============================================================

QueryResult Executor::execCreateDatabase(const CreateDatabaseNode &n, Session &s)
{
    checkPermission(s, "*", "*", Privilege::ALL);  // 仅 root 或全局 ALL 权限
    if (n.ifNotExists && dbMgr_.databaseExists(n.name))
        return QueryResult::ok("Database '" + n.name + "' already exists (skipped).");
    dbMgr_.createDatabase(n.name);
    return QueryResult::ok("Database '" + n.name + "' created.");
}

QueryResult Executor::execDropDatabase(const DropDatabaseNode &n, Session &s)
{
    checkPermission(s, "*", "*", Privilege::ALL);  // 仅 root 或全局 ALL 权限
    if (n.ifExists && !dbMgr_.databaseExists(n.name))
        return QueryResult::ok("Database '" + n.name + "' does not exist (skipped).");
    dbMgr_.dropDatabase(n.name);
    // 若当前 USE 的库被删除，清空上下文
    if (s.currentDatabase == n.name)
        s.currentDatabase.clear();
    return QueryResult::ok("Database '" + n.name + "' dropped.");
}

QueryResult Executor::execShowDatabases(Session &s)
{
    requireAuthenticated(s);
    auto dbs = dbMgr_.listDatabases();
    QueryResult r;
    r.type = QueryResult::Type::SELECT;
    r.columns.push_back({"Database", FieldType::VARCHAR});
    for (const auto &db : dbs)
    {
        r.rows.push_back({FieldValue{db}});
    }
    r.rowCount = static_cast<int>(r.rows.size());
    return r;
}

QueryResult Executor::execUseDatabase(const UseDatabaseNode &n, Session &s)
{
    requireAuthenticated(s);
    if (!dbMgr_.databaseExists(n.name))
        throw DBException(ErrorCode::DB_NOT_FOUND, "Unknown database '" + n.name + "'");
    s.currentDatabase = n.name;
    return QueryResult::ok("Database changed to '" + n.name + "'.");
}

// ============================================================
// DDL – 表
// ============================================================

QueryResult Executor::execCreateTable(const CreateTableNode &n, Session &s)
{
    std::string db = resolveDb(n.database, s);
    checkPermission(s, db, "*", Privilege::ALL);  // 需要对该库有 ALL 权限
    if (n.ifNotExists && tblMgr_.tableExists(db, n.def.name))
        return QueryResult::ok("Table '" + n.def.name + "' already exists (skipped).");
    tblMgr_.createTable(db, n.def);
    return QueryResult::ok("Table '" + n.def.name + "' created.");
}

QueryResult Executor::execDropTable(const DropTableNode &n, Session &s)
{
    std::string db = resolveDb(n.database, s);
    checkPermission(s, db, "*", Privilege::ALL);  // 需要对该库有 ALL 权限
    if (n.ifExists && !tblMgr_.tableExists(db, n.table))
        return QueryResult::ok("Table '" + n.table + "' does not exist (skipped).");
    tblMgr_.dropTable(db, n.table);
    return QueryResult::ok("Table '" + n.table + "' dropped.");
}

QueryResult Executor::execShowTables(Session &s)
{
    std::string db = resolveDb("", s);
    requireAuthenticated(s);
    auto tables = tblMgr_.listTables(db);
    QueryResult r;
    r.type = QueryResult::Type::SELECT;
    r.columns.push_back({"Tables_in_" + db, FieldType::VARCHAR});
    for (const auto &t : tables)
        r.rows.push_back({FieldValue{t}});
    r.rowCount = static_cast<int>(r.rows.size());
    return r;
}

QueryResult Executor::execDescribeTable(const DescribeTableNode &n, Session &s)
{
    requireAuthenticated(s);
    std::string db = resolveDb(n.database, s);
    auto defOpt = tblMgr_.describeTable(db, n.table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND, "Unknown table '" + n.table + "'");
    const auto &def = *defOpt;

    QueryResult r;
    r.type = QueryResult::Type::SELECT;
    r.columns = {
        {"Field", FieldType::VARCHAR},
        {"Type", FieldType::VARCHAR},
        {"Null", FieldType::VARCHAR},
        {"Key", FieldType::VARCHAR},
        {"Default", FieldType::VARCHAR},
        {"Extra", FieldType::VARCHAR},
    };

    auto typStr = [](const ColumnDefinition &c) -> std::string
    {
        switch (c.type)
        {
        case FieldType::INTEGER:
            return "int";
        case FieldType::DOUBLE:
            return "double";
        case FieldType::BOOL:
            return "bool";
        case FieldType::VARCHAR:
            return "varchar(" + std::to_string(c.length) + ")";
        case FieldType::DATETIME:
            return "datetime";
        }
        return "int";
    };

    for (const auto &col : def.columns)
    {
        std::string key, extra;
        if (col.primaryKey)
            key = "PRI";
        else if (col.unique)
            key = "UNI";
        if (col.autoIncrement)
            extra = "auto_increment";

        r.rows.push_back({
            FieldValue{col.name},
            FieldValue{typStr(col)},
            FieldValue{col.nullable ? std::string("YES") : std::string("NO")},
            FieldValue{key},
            FieldValue{col.defaultValue.empty() ? std::string("NULL") : col.defaultValue},
            FieldValue{extra},
        });
    }
    r.rowCount = static_cast<int>(r.rows.size());
    return r;
}

QueryResult Executor::execAlterTable(const AlterTableNode &n, Session &s)
{
    std::string db = resolveDb(n.database, s);
    checkPermission(s, db, "*", Privilege::ALL);
    switch (n.action)
    {
    case AlterAction::ADD_COLUMN:
        tblMgr_.addColumn(db, n.table, n.column);
        return QueryResult::ok("Column '" + n.column.name + "' added.");
    case AlterAction::MODIFY_COLUMN:
        tblMgr_.modifyColumn(db, n.table, n.column.name, n.column);
        return QueryResult::ok("Column '" + n.column.name + "' modified.");
    case AlterAction::DROP_COLUMN:
        tblMgr_.dropColumn(db, n.table, n.dropColName);
        return QueryResult::ok("Column '" + n.dropColName + "' dropped.");
    }
    return QueryResult::ok();
}

// ============================================================
// DDL – 索引
// ============================================================

QueryResult Executor::execCreateIndex(const CreateIndexNode &n, Session &s)
{
    std::string db = resolveDb(n.database, s);
    checkPermission(s, db, "*", Privilege::ALL);
    idxMgr_.createIndex(db, n.table, n.indexName, n.columns, n.unique);
    return QueryResult::ok("Index '" + n.indexName + "' created (schema only).");
}

QueryResult Executor::execDropIndex(const DropIndexNode &n, Session &s)
{
    std::string db = resolveDb(n.database, s);
    checkPermission(s, db, "*", Privilege::ALL);
    idxMgr_.dropIndex(db, n.table, n.indexName);
    return QueryResult::ok("Index '" + n.indexName + "' dropped.");
}

// ============================================================
// 外键约束检查
// ============================================================

// INSERT/UPDATE 时检查：被引用表中必须存在对应的行
static void checkFKParentExists(
    TableManager &tblMgr, RecordManager &recMgr,
    const std::string &db,
    const TableDefinition &childDef,
    const std::map<std::string, FieldValue> &record)
{
    for (const auto &fk : childDef.foreignKeys)
    {
        std::vector<FieldValue> childVals;
        bool allNull = true;
        for (const auto &col : fk.columns)
        {
            auto it = record.find(col);
            FieldValue v = (it != record.end()) ? it->second : std::monostate{};
            if (!std::holds_alternative<std::monostate>(v))
                allNull = false;
            childVals.push_back(v);
        }
        if (allNull)
            continue;

        auto parentDefOpt = tblMgr.describeTable(db, fk.refTable);
        if (!parentDefOpt)
            throw DBException(ErrorCode::TABLE_NOT_FOUND,
                              "FK: Referenced table '" + fk.refTable + "' not found");

        auto parentRows = recMgr.scan(db, fk.refTable);
        bool found = false;
        for (const auto &pr : parentRows)
        {
            bool match = true;
            for (size_t i = 0; i < fk.refColumns.size() && i < childVals.size(); ++i)
            {
                FieldValue pv = std::monostate{};
                for (size_t ci = 0; ci < parentDefOpt->columns.size(); ++ci)
                {
                    if (parentDefOpt->columns[ci].name == fk.refColumns[i])
                    {
                        pv = (ci < pr.size()) ? pr[ci] : std::monostate{};
                        break;
                    }
                }
                if (std::holds_alternative<std::monostate>(childVals[i]) !=
                    std::holds_alternative<std::monostate>(pv))
                {
                    match = false;
                    break;
                }
                if (!std::holds_alternative<std::monostate>(childVals[i]))
                {
                    if (fvToStr(childVals[i]) != fvToStr(pv))
                    {
                        match = false;
                        break;
                    }
                }
            }
            if (match)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            throw DBException(ErrorCode::FOREIGN_KEY_VIOLATION,
                              "Foreign key constraint '" + fk.constraintName + "': referenced row not found in '" + fk.refTable + "'");
        }
    }
}

// DELETE/UPDATE 时检查：子表中不能有引用当前行的记录
static void checkFKChildAbsent(
    TableManager &tblMgr, RecordManager &recMgr,
    const std::string &db,
    const TableDefinition &parentDef,
    const std::map<std::string, FieldValue> &deletedRecord)
{
    auto allTables = tblMgr.listTables(db);
    for (const auto &childTblName : allTables)
    {
        auto childDefOpt = tblMgr.describeTable(db, childTblName);
        if (!childDefOpt)
            continue;
        for (const auto &fk : childDefOpt->foreignKeys)
        {
            if (fk.refTable != parentDef.name)
                continue;
            std::vector<FieldValue> refVals;
            for (const auto &rc : fk.refColumns)
            {
                auto it = deletedRecord.find(rc);
                refVals.push_back(it != deletedRecord.end() ? it->second : std::monostate{});
            }
            auto childRows = recMgr.scan(db, childTblName);
            for (const auto &cr : childRows)
            {
                bool match = true;
                for (size_t i = 0; i < fk.columns.size() && i < refVals.size(); ++i)
                {
                    FieldValue cv = std::monostate{};
                    for (size_t ci = 0; ci < childDefOpt->columns.size(); ++ci)
                    {
                        if (childDefOpt->columns[ci].name == fk.columns[i])
                        {
                            cv = (ci < cr.size()) ? cr[ci] : std::monostate{};
                            break;
                        }
                    }
                    if (std::holds_alternative<std::monostate>(cv))
                    {
                        match = false;
                        break;
                    }
                    if (fvToStr(cv) != fvToStr(refVals[i]))
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                {
                    throw DBException(ErrorCode::FOREIGN_KEY_VIOLATION,
                                      "Cannot delete/update: foreign key constraint from '" + childTblName + "' references this row");
                }
            }
        }
    }
}

// ============================================================
// DML – INSERT
// ============================================================

QueryResult Executor::execInsert(const InsertNode &n, Session &s)
{
    std::string db = resolveDb(n.database, s);
    checkPermission(s, db, n.table, Privilege::INSERT);
    auto defOpt = tblMgr_.describeTable(db, n.table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND,
                          "Unknown table '" + n.table + "'");
    const auto &def = *defOpt;

    int64_t lastId = -1;
    int affected = 0;

    for (const auto &valueRow : n.valueRows)
    {
        std::map<std::string, FieldValue> record;

        if (n.columns.empty())
        {
            // 按列顺序赋值（跳过 auto_increment 列若值为 0 或 NULL）
            for (size_t i = 0; i < def.columns.size() && i < valueRow.size(); ++i)
                record[def.columns[i].name] = coerce(valueRow[i], def.columns[i]);
        }
        else
        {
            if (n.columns.size() != valueRow.size())
                throw DBException(ErrorCode::COLUMN_INVALID,
                                  "Column count doesn't match value count");
            for (size_t i = 0; i < n.columns.size(); ++i)
            {
                // 找到列定义
                const ColumnDefinition *colDef = nullptr;
                for (const auto &c : def.columns)
                    if (c.name == n.columns[i])
                    {
                        colDef = &c;
                        break;
                    }
                if (!colDef)
                    throw DBException(ErrorCode::COLUMN_NOT_FOUND,
                                      "Unknown column '" + n.columns[i] + "'");
                record[n.columns[i]] = coerce(valueRow[i], *colDef);
            }
        }

        // 处理 AUTO_INCREMENT
        for (const auto &col : def.columns)
        {
            if (!col.autoIncrement)
                continue;
            auto it = record.find(col.name);
            bool needGen = (it == record.end()) || std::holds_alternative<std::monostate>(it->second) || (std::holds_alternative<int64_t>(it->second) && std::get<int64_t>(it->second) == 0);
            if (needGen)
            {
                int64_t newId = tblMgr_.nextAutoIncrement(db, n.table);
                record[col.name] = newId;
                lastId = newId;
            }
            else if (std::holds_alternative<int64_t>(it->second))
            {
                lastId = std::get<int64_t>(it->second);
            }
        }

        // 补全缺失列的默认值 / NOT NULL 检查 / VARCHAR 长度检查
        for (const auto &col : def.columns)
        {
            if (record.find(col.name) == record.end())
            {
                if (!col.defaultValue.empty())
                {
                    record[col.name] = parseDefault(col);
                }
                else if (col.nullable)
                {
                    record[col.name] = std::monostate{};
                }
                else if (!col.autoIncrement)
                {
                    throw DBException(ErrorCode::CONSTRAINT_VIOLATION,
                                      "Column '" + col.name + "' cannot be NULL");
                }
            }
            else
            {
                // NOT NULL 检查
                if (!col.nullable && !col.autoIncrement && std::holds_alternative<std::monostate>(record[col.name]))
                    throw DBException(ErrorCode::CONSTRAINT_VIOLATION,
                                      "Column '" + col.name + "' cannot be NULL");
                // VARCHAR 长度
                record[col.name] = validateField(record[col.name], col);
            }
        }

        // UNIQUE / PRIMARY KEY 唯一性检查
        checkUniqueConstraints(recMgr_, db, n.table, def, record);

        // FK parent 存在检查
        checkFKParentExists(tblMgr_, recMgr_, db, def, record);

        recMgr_.insert(db, n.table, record);
        int64_t insertedOffset = recMgr_.lastInsertOffset();
        idxMgr_.onInsert(db, n.table, record, insertedOffset);
        // WAL and in-memory undo log for INSERT
        if (!s.transactionId.empty()) {
            walMgr_.logInsert(s.transactionId, db, n.table, insertedOffset);
            txMgr_.logInsert(s.transactionId, db, n.table, insertedOffset);
        }
        ++affected;
    }

    QueryResult r = QueryResult::dml(affected, lastId);
    r.message = std::to_string(affected) + " row(s) inserted.";
    return r;
}

// ============================================================
// DML – SELECT
// ============================================================

QueryResult Executor::execSelect(const SelectNode &n, Session &s)
{
    std::string db = resolveDb(n.database, s);
    checkPermission(s, db, n.table, Privilege::SELECT);
    auto defOpt = tblMgr_.describeTable(db, n.table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND,
                          "Unknown table '" + n.table + "'");
    const auto &def = *defOpt;
    ExprEvaluator eval;

    // ── 1. 全表扫描 + WHERE 过滤 ──────────────────────────────────────
    auto rawRows = recMgr_.scan(db, n.table);
    std::vector<std::map<std::string, FieldValue>> filtered;
    filtered.reserve(rawRows.size());
    for (const auto &row : rawRows)
    {
        auto m = rowToMap(def, row);
        if (!n.where || eval.evaluate(*n.where, m))
            filtered.push_back(std::move(m));
    }

    // ── 2. 确定是否需要聚合/分组 ──────────────────────────────────────
    bool hasAgg = false;
    for (const auto &sc : n.columns)
        if (sc.kind == SelectColumn::Kind::AGGREGATE)
        {
            hasAgg = true;
            break;
        }
    bool needGroup = hasAgg || !n.groupBy.empty();

    // ── 辅助 lambda ───────────────────────────────────────────────────
    auto aggName = [](const AggregateExpr &agg) -> std::string
    {
        if (!agg.alias.empty())
            return agg.alias;
        std::string fn;
        switch (agg.func)
        {
        case AggFunc::COUNT:
            fn = "COUNT";
            break;
        case AggFunc::SUM:
            fn = "SUM";
            break;
        case AggFunc::MAX:
            fn = "MAX";
            break;
        case AggFunc::MIN:
            fn = "MIN";
            break;
        case AggFunc::AVG:
            fn = "AVG";
            break;
        }
        return fn + "(" + agg.column + ")";
    };

    auto aggType = [](const AggregateExpr &agg) -> FieldType
    {
        return (agg.func == AggFunc::COUNT) ? FieldType::INTEGER : FieldType::DOUBLE;
    };

    // 对一组行计算单个聚合
    using RowPtrVec = std::vector<const std::map<std::string, FieldValue> *>;
    auto computeAgg = [&](const AggregateExpr &agg, const RowPtrVec &rows) -> FieldValue
    {
        switch (agg.func)
        {
        case AggFunc::COUNT:
        {
            if (agg.column == "*")
                return static_cast<int64_t>(rows.size());
            int64_t cnt = 0;
            for (const auto *rm : rows)
            {
                auto it = rm->find(agg.column);
                if (it != rm->end() && !std::holds_alternative<std::monostate>(it->second))
                    ++cnt;
            }
            return cnt;
        }
        case AggFunc::SUM:
        case AggFunc::AVG:
        {
            double sum = 0;
            int64_t cnt = 0;
            for (const auto *rm : rows)
            {
                auto it = rm->find(agg.column);
                if (it == rm->end())
                    continue;
                if (std::holds_alternative<int64_t>(it->second))
                    sum += static_cast<double>(std::get<int64_t>(it->second)), ++cnt;
                else if (std::holds_alternative<double>(it->second))
                    sum += std::get<double>(it->second), ++cnt;
            }
            if (agg.func == AggFunc::AVG)
                return cnt ? sum / cnt : 0.0;
            return sum;
        }
        case AggFunc::MAX:
        case AggFunc::MIN:
        {
            FieldValue best = std::monostate{};
            for (const auto *rm : rows)
            {
                auto it = rm->find(agg.column);
                if (it == rm->end() || std::holds_alternative<std::monostate>(it->second))
                    continue;
                if (std::holds_alternative<std::monostate>(best))
                {
                    best = it->second;
                    continue;
                }
                bool pick;
                if (std::holds_alternative<int64_t>(it->second) && std::holds_alternative<int64_t>(best))
                    pick = agg.func == AggFunc::MAX
                               ? std::get<int64_t>(it->second) > std::get<int64_t>(best)
                               : std::get<int64_t>(it->second) < std::get<int64_t>(best);
                else
                {
                    double da = std::holds_alternative<int64_t>(it->second)
                                    ? static_cast<double>(std::get<int64_t>(it->second))
                                    : (std::holds_alternative<double>(it->second) ? std::get<double>(it->second) : 0.0);
                    double db2 = std::holds_alternative<int64_t>(best)
                                     ? static_cast<double>(std::get<int64_t>(best))
                                     : (std::holds_alternative<double>(best) ? std::get<double>(best) : 0.0);
                    if (std::holds_alternative<std::string>(it->second) && std::holds_alternative<std::string>(best))
                    {
                        pick = agg.func == AggFunc::MAX
                                   ? std::get<std::string>(it->second) > std::get<std::string>(best)
                                   : std::get<std::string>(it->second) < std::get<std::string>(best);
                    }
                    else
                    {
                        pick = agg.func == AggFunc::MAX ? da > db2 : da < db2;
                    }
                }
                if (pick)
                    best = it->second;
            }
            return best;
        }
        }
        return std::monostate{};
    };

    // ── 3. 确定输出列 ─────────────────────────────────────────────────
    std::vector<ColumnMeta> outCols;
    bool hasWildcard = false;
    for (const auto &sc : n.columns)
        if (sc.kind == SelectColumn::Kind::WILDCARD)
        {
            hasWildcard = true;
            break;
        }

    if (hasWildcard)
    {
        for (const auto &col : def.columns)
            outCols.push_back({col.name, col.type});
    }
    else
    {
        for (const auto &sc : n.columns)
        {
            if (sc.kind == SelectColumn::Kind::COLUMN_REF)
            {
                std::string nm = sc.alias.empty() ? sc.columnName : sc.alias;
                FieldType ft = FieldType::VARCHAR;
                for (const auto &col : def.columns)
                    if (col.name == sc.columnName)
                    {
                        ft = col.type;
                        break;
                    }
                outCols.push_back({nm, ft});
            }
            else if (sc.kind == SelectColumn::Kind::AGGREGATE)
            {
                outCols.push_back({aggName(sc.aggregate), aggType(sc.aggregate)});
            }
        }
    }

    // ── 4. 聚合/GROUP BY 路径 ─────────────────────────────────────────
    if (needGroup)
    {
        using GroupKey = std::vector<std::string>;
        std::map<GroupKey, RowPtrVec> groupMap;
        std::vector<GroupKey> keyOrder;

        if (n.groupBy.empty())
        {
            GroupKey emptyKey;
            groupMap[emptyKey]; // ensure key exists even when filtered is empty
            for (const auto &m : filtered)
                groupMap[emptyKey].push_back(&m);
            keyOrder.push_back(emptyKey);
        }
        else
        {
            for (const auto &m : filtered)
            {
                GroupKey gk;
                for (const auto &gc : n.groupBy)
                {
                    auto it = m.find(gc);
                    gk.push_back(it != m.end() ? fvToStr(it->second) : "");
                }
                if (!groupMap.count(gk))
                    keyOrder.push_back(gk);
                groupMap[gk].push_back(&m);
            }
        }

        std::vector<std::map<std::string, FieldValue>> aggRows;
        for (const auto &gk : keyOrder)
        {
            const auto &rows = groupMap.at(gk);
            std::map<std::string, FieldValue> repr;

            if (!n.groupBy.empty() && !rows.empty())
            {
                for (const auto &gc : n.groupBy)
                {
                    auto it = rows[0]->find(gc);
                    if (it != rows[0]->end())
                        repr[gc] = it->second;
                }
            }

            for (const auto &sc : n.columns)
            {
                if (sc.kind != SelectColumn::Kind::AGGREGATE)
                    continue;
                repr[aggName(sc.aggregate)] = computeAgg(sc.aggregate, rows);
            }

            if (n.having && !eval.evaluate(*n.having, repr))
                continue;

            if (!rows.empty())
            {
                for (const auto &col : def.columns)
                {
                    if (!repr.count(col.name))
                    {
                        auto it = rows[0]->find(col.name);
                        if (it != rows[0]->end())
                            repr[col.name] = it->second;
                    }
                }
            }
            aggRows.push_back(std::move(repr));
        }
        filtered = std::move(aggRows);
    }

    // ── 5. ORDER BY ───────────────────────────────────────────────────
    if (!n.orderBy.empty())
    {
        std::stable_sort(filtered.begin(), filtered.end(),
                         [&](const std::map<std::string, FieldValue> &a,
                             const std::map<std::string, FieldValue> &b)
                         {
                             for (const auto &ob : n.orderBy)
                             {
                                 auto ia = a.find(ob.columnName);
                                 auto ib = b.find(ob.columnName);
                                 FieldValue va = (ia != a.end()) ? ia->second : std::monostate{};
                                 FieldValue vb = (ib != b.end()) ? ib->second : std::monostate{};
                                 bool aNul = std::holds_alternative<std::monostate>(va);
                                 bool bNul = std::holds_alternative<std::monostate>(vb);
                                 if (aNul && bNul)
                                     continue;
                                 if (aNul)
                                     return !ob.ascending;
                                 if (bNul)
                                     return ob.ascending;
                                 if (std::holds_alternative<int64_t>(va) && std::holds_alternative<int64_t>(vb))
                                 {
                                     if (std::get<int64_t>(va) != std::get<int64_t>(vb))
                                         return ob.ascending ? std::get<int64_t>(va) < std::get<int64_t>(vb)
                                                             : std::get<int64_t>(va) > std::get<int64_t>(vb);
                                 }
                                 else if (std::holds_alternative<double>(va) || std::holds_alternative<double>(vb))
                                 {
                                     double da = std::holds_alternative<int64_t>(va)
                                                     ? static_cast<double>(std::get<int64_t>(va))
                                                     : (std::holds_alternative<double>(va) ? std::get<double>(va) : 0.0);
                                     double db2 = std::holds_alternative<int64_t>(vb)
                                                      ? static_cast<double>(std::get<int64_t>(vb))
                                                      : (std::holds_alternative<double>(vb) ? std::get<double>(vb) : 0.0);
                                     if (da != db2)
                                         return ob.ascending ? da < db2 : da > db2;
                                 }
                                 else
                                 {
                                     std::string sa = fvToStr(va), sb = fvToStr(vb);
                                     if (sa != sb)
                                         return ob.ascending ? sa < sb : sa > sb;
                                 }
                             }
                             return false;
                         });
    }

    // ── 6. LIMIT / OFFSET ─────────────────────────────────────────────
    size_t startIdx = static_cast<size_t>(n.offset >= 0 ? n.offset : 0);
    size_t endIdx = filtered.size();
    if (n.limit >= 0)
        endIdx = std::min(endIdx, startIdx + static_cast<size_t>(n.limit));
    if (startIdx > filtered.size())
        startIdx = filtered.size();

    // ── 7. 投影 → Row ─────────────────────────────────────────────────
    QueryResult r;
    r.type = QueryResult::Type::SELECT;
    r.columns = outCols;

    for (size_t i = startIdx; i < endIdx; ++i)
    {
        const auto &m = filtered[i];
        Row outRow;
        if (hasWildcard)
        {
            for (const auto &col : def.columns)
            {
                auto it = m.find(col.name);
                outRow.push_back(it != m.end() ? it->second : std::monostate{});
            }
        }
        else
        {
            for (const auto &sc : n.columns)
            {
                if (sc.kind == SelectColumn::Kind::COLUMN_REF)
                {
                    auto it = m.find(sc.columnName);
                    outRow.push_back(it != m.end() ? it->second : std::monostate{});
                }
                else if (sc.kind == SelectColumn::Kind::AGGREGATE)
                {
                    auto it = m.find(aggName(sc.aggregate));
                    outRow.push_back(it != m.end() ? it->second : std::monostate{});
                }
            }
        }
        r.rows.push_back(std::move(outRow));
    }

    // ── 8. DISTINCT ───────────────────────────────────────────────────
    if (n.distinct)
    {
        std::vector<Row> dedup;
        std::set<std::string> seen;
        for (const auto &row : r.rows)
        {
            std::string key;
            for (const auto &v : row)
                key += fvToStr(v) + '\x01';
            if (seen.insert(key).second)
                dedup.push_back(row);
        }
        r.rows = std::move(dedup);
    }

    r.rowCount = static_cast<int>(r.rows.size());
    return r;
}

// ============================================================
// DML – UPDATE
// ============================================================

QueryResult Executor::execUpdate(const UpdateNode &n, Session &s)
{
    std::string db = resolveDb(n.database, s);
    checkPermission(s, db, n.table, Privilege::UPDATE);
    auto defOpt = tblMgr_.describeTable(db, n.table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND,
                          "Unknown table '" + n.table + "'");
    const auto &def = *defOpt;

    ExprEvaluator eval;

    // 先验证 SET 列名合法
    for (const auto &a : n.assignments)
    {
        bool found = false;
        for (const auto &col : def.columns)
            if (col.name == a.columnName)
            {
                found = true;
                break;
            }
        if (!found)
            throw DBException(ErrorCode::COLUMN_NOT_FOUND,
                              "Unknown column '" + a.columnName + "' in SET clause");
    }

    auto withOffsets = recMgr_.scanWithOffsets(db, n.table);
    int affected = 0;

    for (const auto &[offset, row] : withOffsets)
    {
        auto m = rowToMap(def, row);
        if (n.where && !eval.evaluate(*n.where, m))
            continue;

        // 应用赋值（含类型转换 + 约束检查）
        for (const auto &a : n.assignments)
        {
            for (const auto &col : def.columns)
            {
                if (col.name != a.columnName)
                    continue;

                FieldValue newVal = coerce(a.value, col);

                // NOT NULL 检查
                if (!col.nullable && !col.autoIncrement && std::holds_alternative<std::monostate>(newVal))
                    throw DBException(ErrorCode::CONSTRAINT_VIOLATION,
                                      "Column '" + col.name + "' cannot be NULL");

                // VARCHAR 长度检查
                newVal = validateField(newVal, col);

                m[a.columnName] = std::move(newVal);
                break;
            }
        }

        // UNIQUE / PK 唯一性检查（排除自身行）
        checkUniqueConstraints(recMgr_, db, n.table, def, m, offset);

        // FK parent 存在检查 + 索引维护
        checkFKParentExists(tblMgr_, recMgr_, db, def, m);
        auto oldMap = rowToMap(def, row);
        // 事务撤销日志（保存旧行）
        if (!s.transactionId.empty()) {
            walMgr_.logUpdate(s.transactionId, db, n.table, offset, oldMap);
            txMgr_.logUpdate(s.transactionId, db, n.table, offset, oldMap);
        }
        idxMgr_.onUpdate(db, n.table, oldMap, m, offset);
        recMgr_.update(db, n.table, offset, m);
        ++affected;
    }

    QueryResult r = QueryResult::dml(affected);
    r.message = std::to_string(affected) + " row(s) updated.";
    return r;
}

// ============================================================
// DML – DELETE
// ============================================================

QueryResult Executor::execDelete(const DeleteNode &n, Session &s)
{
    std::string db = resolveDb(n.database, s);
    checkPermission(s, db, n.table, Privilege::DELETE);
    auto defOpt = tblMgr_.describeTable(db, n.table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND,
                          "Unknown table '" + n.table + "'");
    const auto &def = *defOpt;

    ExprEvaluator eval;
    auto withOffsets = recMgr_.scanWithOffsets(db, n.table);
    int affected = 0;

    for (const auto &[offset, row] : withOffsets)
    {
        auto m = rowToMap(def, row);
        if (n.where && !eval.evaluate(*n.where, m))
            continue;
        checkFKChildAbsent(tblMgr_, recMgr_, db, def, m);
        // 事务撤销日志（保存旧行）
        if (!s.transactionId.empty()) {
            walMgr_.logDelete(s.transactionId, db, n.table, offset, m);
            txMgr_.logDelete(s.transactionId, db, n.table, offset, m);
        }
        idxMgr_.onDelete(db, n.table, m, offset);
        recMgr_.remove(db, n.table, offset);
        ++affected;
    }

    QueryResult r = QueryResult::dml(affected);
    r.message = std::to_string(affected) + " row(s) deleted.";
    return r;
}

// ============================================================
// 事务
// ============================================================

QueryResult Executor::execBegin(Session &s)
{
    requireAuthenticated(s);
    if (!s.transactionId.empty())
        return QueryResult::err(ErrorCode::TRANSACTION_CONFLICT,
                                "Transaction already active. COMMIT or ROLLBACK first.");
    s.transactionId = txMgr_.begin();
    walMgr_.logBegin(s.transactionId);
    return QueryResult::ok("Transaction started (id=" + s.transactionId + ").");
}

QueryResult Executor::execCommit(Session &s)
{
    if (s.transactionId.empty())
        return QueryResult::ok("No active transaction to commit.");
    walMgr_.logCommit(s.transactionId);
    walMgr_.compact();
    txMgr_.clear(s.transactionId);
    s.transactionId.clear();
    return QueryResult::ok("COMMIT OK.");
}

QueryResult Executor::execRollback(Session &s)
{
    if (s.transactionId.empty())
        return QueryResult::ok("No active transaction to rollback.");

    const auto &undoLog = txMgr_.getUndoLog(s.transactionId);
    // 逆序回放撤销日志
    for (auto it = undoLog.rbegin(); it != undoLog.rend(); ++it)
    {
        const UndoOp &op = *it;
        switch (op.type)
        {
        case UndoOpType::INSERT_UNDO:
            // 撤销 INSERT：软删除该行
            recMgr_.remove(op.db, op.table, op.offset);
            break;
        case UndoOpType::UPDATE_UNDO:
            // 撤销 UPDATE：恢复旧行数据
            recMgr_.update(op.db, op.table, op.offset, op.oldRecord);
            break;
        case UndoOpType::DELETE_UNDO:
            // 撤销 DELETE：恢复被删除的行（update 会写 status=0 + 数据）
            recMgr_.update(op.db, op.table, op.offset, op.oldRecord);
            break;
        }
    }
    txMgr_.clear(s.transactionId);
    walMgr_.logRollback(s.transactionId);
    walMgr_.compact();
    s.transactionId.clear();
    return QueryResult::ok("ROLLBACK OK.");
}

// ============================================================
// 安全
// ============================================================

QueryResult Executor::execCreateUser(const CreateUserNode &n, Session &s)
{
    requireAuthenticated(s);
    if (s.user != "root")
        throw DBException(ErrorCode::PERMISSION_DENIED,
                          "Only root can create users");
    userMgr_.createUser(n.username, n.password);
    return QueryResult::ok("User '" + n.username + "' created.");
}

QueryResult Executor::execDropUser(const DropUserNode &n, Session &s)
{
    requireAuthenticated(s);
    if (s.user != "root")
        throw DBException(ErrorCode::PERMISSION_DENIED,
                          "Only root can drop users");
    userMgr_.dropUser(n.username);
    return QueryResult::ok("User '" + n.username + "' dropped.");
}

QueryResult Executor::execGrant(const GrantNode &n, Session &s)
{
    requireAuthenticated(s);
    if (s.user != "root")
        throw DBException(ErrorCode::PERMISSION_DENIED,
                          "Only root can grant privileges");
    userMgr_.grantPrivilege(n.username, n.database, n.table, n.privileges);
    return QueryResult::ok("Privileges granted to '" + n.username + "'.");
}

QueryResult Executor::execRevoke(const RevokeNode &n, Session &s)
{
    requireAuthenticated(s);
    if (s.user != "root")
        throw DBException(ErrorCode::PERMISSION_DENIED,
                          "Only root can revoke privileges");
    userMgr_.revokePrivilege(n.username, n.database, n.table, n.privileges);
    return QueryResult::ok("Privileges revoked from '" + n.username + "'.");
}

QueryResult Executor::execConnect(const ConnectNode &n, Session &s)
{
    if (!userMgr_.userExists(n.username))
        return QueryResult::err(ErrorCode::PERMISSION_DENIED,
                                "User '" + n.username + "' does not exist");
    if (!userMgr_.authenticate(n.username, n.password))
        return QueryResult::err(ErrorCode::PERMISSION_DENIED,
                                "Authentication failed for user '" + n.username + "'");
    s.user = n.username;
    return QueryResult::ok("Connected as '" + n.username + "'.");
}

// ============================================================
// 备份
// ============================================================

// 将 FieldValue 转为 SQL 字面量（可直接嵌入 INSERT 语句）
static std::string fvToSQL(const FieldValue& v) {
    if (std::holds_alternative<std::monostate>(v)) return "NULL";
    if (std::holds_alternative<int64_t>(v))   return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))    return std::to_string(std::get<double>(v));
    if (std::holds_alternative<bool>(v))      return std::get<bool>(v) ? "1" : "0";
    if (std::holds_alternative<std::string>(v)) {
        const std::string& src = std::get<std::string>(v);
        std::string out = "'";
        for (char c : src) {
            if (c == '\'') out += "''";
            else           out += c;
        }
        out += "'";
        return out;
    }
    return "NULL";
}

// 将列类型转为 SQL 类型字符串
static std::string colTypeSQL(const ColumnDefinition& col) {
    switch (col.type) {
        case FieldType::INTEGER:  return "INT";
        case FieldType::DOUBLE:   return "DOUBLE";
        case FieldType::BOOL:     return "BOOL";
        case FieldType::VARCHAR:  return "VARCHAR(" + std::to_string(col.length) + ")";
        case FieldType::DATETIME: return "DATETIME";
    }
    return "INT";
}

// 根据 TableDefinition 生成 CREATE TABLE IF NOT EXISTS SQL
static std::string genCreateTableSQL(const TableDefinition& def) {
    // 找出所有 PK 列
    std::vector<std::string> pkCols;
    for (const auto& col : def.columns)
        if (col.primaryKey) pkCols.push_back(col.name);

    std::string sql = "CREATE TABLE IF NOT EXISTS " + def.name + " (\n";
    for (size_t i = 0; i < def.columns.size(); ++i) {
        const auto& col = def.columns[i];
        sql += "  " + col.name + " " + colTypeSQL(col);
        if (!col.nullable && !col.primaryKey) sql += " NOT NULL";
        if (col.autoIncrement) sql += " AUTO_INCREMENT";
        if (col.unique && !col.primaryKey) sql += " UNIQUE";
        if (!col.defaultValue.empty())
            sql += " DEFAULT " + col.defaultValue;
        sql += ",\n";
    }
    // 表级 PRIMARY KEY
    if (!pkCols.empty()) {
        sql += "  PRIMARY KEY (";
        for (size_t i = 0; i < pkCols.size(); ++i) {
            if (i) sql += ", ";
            sql += pkCols[i];
        }
        sql += ")";
        if (!def.foreignKeys.empty()) sql += ",";
        sql += "\n";
    }
    // 外键约束
    for (size_t i = 0; i < def.foreignKeys.size(); ++i) {
        const auto& fk = def.foreignKeys[i];
        sql += "  FOREIGN KEY (";
        for (size_t j = 0; j < fk.columns.size(); ++j) {
            if (j) sql += ", ";
            sql += fk.columns[j];
        }
        sql += ") REFERENCES " + fk.refTable + " (";
        for (size_t j = 0; j < fk.refColumns.size(); ++j) {
            if (j) sql += ", ";
            sql += fk.refColumns[j];
        }
        sql += ")";
        if (i + 1 < def.foreignKeys.size()) sql += ",";
        sql += "\n";
    }
    // 移除最后多余的逗号（当既无 PK 又无 FK 时列末尾有 ",\n"）
    if (pkCols.empty() && def.foreignKeys.empty() && !def.columns.empty()) {
        // 去掉最后一列末尾的 ","
        size_t pos = sql.rfind(",\n");
        if (pos != std::string::npos) sql.erase(pos, 1);
    }
    sql += ")";
    return sql;
}

QueryResult Executor::execBackupDatabase(const BackupDatabaseNode& n, Session& s)
{
    checkPermission(s, "*", "*", Privilege::ALL);  // 仅 root 或全局 ALL 权限
    std::string db = n.database.empty() ? resolveDb("", s) : n.database;
    if (n.filepath.empty())
        throw DBException(ErrorCode::FILE_IO_ERROR, "No output file specified for BACKUP");

    std::ofstream out(n.filepath);
    if (!out)
        throw DBException(ErrorCode::FILE_IO_ERROR,
                          "Cannot create backup file: " + n.filepath);

    out << "-- DBMS Backup: " << db << "\n"
        << "-- Generated by DBMS v0.1\n\n"
        << "CREATE DATABASE IF NOT EXISTS " << db << ";\n"
        << "USE " << db << ";\n\n";

    auto tables = tblMgr_.listTables(db);
    int stmtCount = 0;

    for (const auto& tableName : tables) {
        auto defOpt = tblMgr_.describeTable(db, tableName);
        if (!defOpt) continue;
        const auto& def = *defOpt;

        out << genCreateTableSQL(def) << ";\n\n";
        ++stmtCount;

        auto rows = recMgr_.scan(db, tableName);
        for (const auto& row : rows) {
            out << "INSERT INTO " << tableName << " (";
            for (size_t i = 0; i < def.columns.size(); ++i) {
                if (i) out << ", ";
                out << def.columns[i].name;
            }
            out << ") VALUES (";
            for (size_t i = 0; i < def.columns.size(); ++i) {
                if (i) out << ", ";
                FieldValue v = (i < row.size()) ? row[i] : std::monostate{};
                out << fvToSQL(v);
            }
            out << ");\n";
            ++stmtCount;
        }
        out << "\n";
    }

    out.flush();
    return QueryResult::ok(
        "Backup of '" + db + "' written to '" + n.filepath + "' ("
        + std::to_string(stmtCount) + " statement(s)).");
}
