#include "Executor.h"
#include "ExprEvaluator.h"
#include <algorithm>
#include <sstream>

// ============================================================
// 构造 / 初始化
// ============================================================

Executor::Executor(const std::string& dataDir)
    : dataDir_(dataDir),
      dbMgr_(dataDir),
      tblMgr_(dataDir),
      recMgr_(dataDir, tblMgr_),
      idxMgr_(dataDir)
{
    dbMgr_.init();
}

// ============================================================
// 辅助
// ============================================================

std::string Executor::resolveDb(const std::string& nodeDb, const Session& s) {
    if (!nodeDb.empty()) return nodeDb;
    if (!s.currentDatabase.empty()) return s.currentDatabase;
    throw DBException(ErrorCode::DB_NOT_FOUND, "No database selected. Use USE <database> first.");
}

// 将 FieldValue 转为展示字符串
static std::string fvToStr(const FieldValue& v) {
    if (std::holds_alternative<std::monostate>(v)) return "NULL";
    if (std::holds_alternative<int64_t>(v))  return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))   return std::to_string(std::get<double>(v));
    if (std::holds_alternative<bool>(v))     return std::get<bool>(v) ? "1" : "0";
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    return "";
}

// 构建一行的 map<colName, value>（按列顺序）
static std::map<std::string, FieldValue> rowToMap(const TableDefinition& def, const Row& row) {
    std::map<std::string, FieldValue> m;
    for (size_t i = 0; i < def.columns.size() && i < row.size(); ++i)
        m[def.columns[i].name] = row[i];
    return m;
}

// 解析默认值字符串为 FieldValue
static FieldValue parseDefault(const ColumnDefinition& col) {
    if (col.defaultValue.empty()) return std::monostate{};
    const auto& dv = col.defaultValue;
    switch (col.type) {
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
static FieldValue coerce(const FieldValue& v, const ColumnDefinition& col) {
    if (std::holds_alternative<std::monostate>(v)) return v;
    switch (col.type) {
        case FieldType::INTEGER:
            if (std::holds_alternative<double>(v))
                return static_cast<int64_t>(std::get<double>(v));
            if (std::holds_alternative<bool>(v))
                return static_cast<int64_t>(std::get<bool>(v) ? 1 : 0);
            if (std::holds_alternative<std::string>(v))
                try { return static_cast<int64_t>(std::stoll(std::get<std::string>(v))); } catch (...) {}
            break;
        case FieldType::DOUBLE:
            if (std::holds_alternative<int64_t>(v))
                return static_cast<double>(std::get<int64_t>(v));
            if (std::holds_alternative<bool>(v))
                return std::get<bool>(v) ? 1.0 : 0.0;
            if (std::holds_alternative<std::string>(v))
                try { return std::stod(std::get<std::string>(v)); } catch (...) {}
            break;
        case FieldType::BOOL:
            if (std::holds_alternative<int64_t>(v))
                return std::get<int64_t>(v) != 0;
            if (std::holds_alternative<double>(v))
                return std::get<double>(v) != 0.0;
            break;
        default: break;
    }
    return v;
}

// ============================================================
// 完整性约束检查
// ============================================================

// 比较两个 FieldValue 是否相等（用于约束检查）
static bool fvEqual(const FieldValue& a, const FieldValue& b) {
    if (a.index() != b.index()) return false;
    if (std::holds_alternative<std::monostate>(a)) return true; // NULL==NULL 在约束中视为不同
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
    RecordManager& recMgr,
    const std::string& db,
    const std::string& table,
    const TableDefinition& def,
    const std::map<std::string, FieldValue>& record,
    int64_t excludeOffset = -1)
{
    // 只检查有 PK 或 UNIQUE 的列
    std::vector<const ColumnDefinition*> keyCols;
    for (const auto& col : def.columns)
        if (col.primaryKey || col.unique) keyCols.push_back(&col);
    if (keyCols.empty()) return;

    // NULL 值不参与唯一性检查（SQL 标准：NULL != NULL）
    for (const auto* kc : keyCols) {
        auto it = record.find(kc->name);
        if (it == record.end() || std::holds_alternative<std::monostate>(it->second))
            continue; // NULL 不检查
    }

    auto withOffsets = recMgr.scanWithOffsets(db, table);
    for (const auto& [offset, row] : withOffsets) {
        if (offset == excludeOffset) continue;
        // 构建 map
        std::map<std::string, FieldValue> existing;
        for (size_t i = 0; i < def.columns.size() && i < row.size(); ++i)
            existing[def.columns[i].name] = row[i];

        for (const auto* kc : keyCols) {
            auto newIt = record.find(kc->name);
            if (newIt == record.end()) continue;
            if (std::holds_alternative<std::monostate>(newIt->second)) continue;
            auto exIt = existing.find(kc->name);
            if (exIt == existing.end()) continue;
            if (fvEqual(newIt->second, exIt->second)) {
                std::string kind = kc->primaryKey ? "PRIMARY KEY" : "UNIQUE";
                throw DBException(ErrorCode::DUPLICATE_KEY,
                    "Duplicate entry for " + kind + " column '" + kc->name + "'");
            }
        }
    }
}

// VARCHAR 长度检查 + 截断
static FieldValue validateField(const FieldValue& v, const ColumnDefinition& col) {
    if (col.type == FieldType::VARCHAR
        && std::holds_alternative<std::string>(v))
    {
        const auto& s = std::get<std::string>(v);
        if (col.length > 0 && static_cast<int>(s.size()) > col.length)
            throw DBException(ErrorCode::COLUMN_INVALID,
                "Value too long for column '" + col.name
                + "' (max " + std::to_string(col.length) + ")");
    }
    return v;
}

// ============================================================
// execute 入口：按 AST 类型分发
// ============================================================

QueryResult Executor::execute(const ASTNode& ast, Session& session) {
    switch (ast.type) {
        // DDL – 数据库
        case NodeType::CREATE_DATABASE:
            return execCreateDatabase(static_cast<const CreateDatabaseNode&>(ast), session);
        case NodeType::DROP_DATABASE:
            return execDropDatabase(static_cast<const DropDatabaseNode&>(ast), session);
        case NodeType::SHOW_DATABASES:
            return execShowDatabases(session);
        case NodeType::USE_DATABASE:
            return execUseDatabase(static_cast<const UseDatabaseNode&>(ast), session);
        // DDL – 表
        case NodeType::CREATE_TABLE:
            return execCreateTable(static_cast<const CreateTableNode&>(ast), session);
        case NodeType::DROP_TABLE:
            return execDropTable(static_cast<const DropTableNode&>(ast), session);
        case NodeType::SHOW_TABLES:
            return execShowTables(session);
        case NodeType::DESCRIBE_TABLE:
            return execDescribeTable(static_cast<const DescribeTableNode&>(ast), session);
        case NodeType::ALTER_TABLE:
            return execAlterTable(static_cast<const AlterTableNode&>(ast), session);
        // DDL – 索引
        case NodeType::CREATE_INDEX:
            return execCreateIndex(static_cast<const CreateIndexNode&>(ast), session);
        case NodeType::DROP_INDEX:
            return execDropIndex(static_cast<const DropIndexNode&>(ast), session);
        // DML
        case NodeType::INSERT:
            return execInsert(static_cast<const InsertNode&>(ast), session);
        case NodeType::SELECT:
            return execSelect(static_cast<const SelectNode&>(ast), session);
        case NodeType::UPDATE:
            return execUpdate(static_cast<const UpdateNode&>(ast), session);
        case NodeType::DELETE:
            return execDelete(static_cast<const DeleteNode&>(ast), session);
        // 事务（本期简单实现：仅返回 OK）
        case NodeType::BEGIN_TRANSACTION:
            return QueryResult::ok("Transaction started (not yet persistent).");
        case NodeType::COMMIT:
            return QueryResult::ok("COMMIT OK.");
        case NodeType::ROLLBACK:
            return QueryResult::ok("ROLLBACK OK.");
        default:
            throw DBException(ErrorCode::UNKNOWN_ERROR,
                              "Unsupported statement type");
    }
}

// ============================================================
// DDL – 数据库
// ============================================================

QueryResult Executor::execCreateDatabase(const CreateDatabaseNode& n, Session& /*s*/) {
    if (n.ifNotExists && dbMgr_.databaseExists(n.name))
        return QueryResult::ok("Database '" + n.name + "' already exists (skipped).");
    dbMgr_.createDatabase(n.name);
    return QueryResult::ok("Database '" + n.name + "' created.");
}

QueryResult Executor::execDropDatabase(const DropDatabaseNode& n, Session& s) {
    if (n.ifExists && !dbMgr_.databaseExists(n.name))
        return QueryResult::ok("Database '" + n.name + "' does not exist (skipped).");
    dbMgr_.dropDatabase(n.name);
    // 若当前 USE 的库被删除，清空上下文
    if (s.currentDatabase == n.name) s.currentDatabase.clear();
    return QueryResult::ok("Database '" + n.name + "' dropped.");
}

QueryResult Executor::execShowDatabases(Session& /*s*/) {
    auto dbs = dbMgr_.listDatabases();
    QueryResult r;
    r.type = QueryResult::Type::SELECT;
    r.columns.push_back({"Database", FieldType::VARCHAR});
    for (const auto& db : dbs) {
        r.rows.push_back({ FieldValue{db} });
    }
    r.rowCount = static_cast<int>(r.rows.size());
    return r;
}

QueryResult Executor::execUseDatabase(const UseDatabaseNode& n, Session& s) {
    if (!dbMgr_.databaseExists(n.name))
        throw DBException(ErrorCode::DB_NOT_FOUND, "Unknown database '" + n.name + "'");
    s.currentDatabase = n.name;
    return QueryResult::ok("Database changed to '" + n.name + "'.");
}

// ============================================================
// DDL – 表
// ============================================================

QueryResult Executor::execCreateTable(const CreateTableNode& n, Session& s) {
    std::string db = resolveDb(n.database, s);
    if (n.ifNotExists && tblMgr_.tableExists(db, n.def.name))
        return QueryResult::ok("Table '" + n.def.name + "' already exists (skipped).");
    tblMgr_.createTable(db, n.def);
    return QueryResult::ok("Table '" + n.def.name + "' created.");
}

QueryResult Executor::execDropTable(const DropTableNode& n, Session& s) {
    std::string db = resolveDb(n.database, s);
    if (n.ifExists && !tblMgr_.tableExists(db, n.table))
        return QueryResult::ok("Table '" + n.table + "' does not exist (skipped).");
    tblMgr_.dropTable(db, n.table);
    return QueryResult::ok("Table '" + n.table + "' dropped.");
}

QueryResult Executor::execShowTables(Session& s) {
    std::string db = resolveDb("", s);
    auto tables = tblMgr_.listTables(db);
    QueryResult r;
    r.type = QueryResult::Type::SELECT;
    r.columns.push_back({"Tables_in_" + db, FieldType::VARCHAR});
    for (const auto& t : tables)
        r.rows.push_back({ FieldValue{t} });
    r.rowCount = static_cast<int>(r.rows.size());
    return r;
}

QueryResult Executor::execDescribeTable(const DescribeTableNode& n, Session& s) {
    std::string db = resolveDb(n.database, s);
    auto defOpt = tblMgr_.describeTable(db, n.table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND, "Unknown table '" + n.table + "'");
    const auto& def = *defOpt;

    QueryResult r;
    r.type = QueryResult::Type::SELECT;
    r.columns = {
        {"Field",   FieldType::VARCHAR},
        {"Type",    FieldType::VARCHAR},
        {"Null",    FieldType::VARCHAR},
        {"Key",     FieldType::VARCHAR},
        {"Default", FieldType::VARCHAR},
        {"Extra",   FieldType::VARCHAR},
    };

    auto typStr = [](const ColumnDefinition& c) -> std::string {
        switch (c.type) {
            case FieldType::INTEGER:  return "int";
            case FieldType::DOUBLE:   return "double";
            case FieldType::BOOL:     return "bool";
            case FieldType::VARCHAR:  return "varchar(" + std::to_string(c.length) + ")";
            case FieldType::DATETIME: return "datetime";
        }
        return "int";
    };

    for (const auto& col : def.columns) {
        std::string key, extra;
        if (col.primaryKey)    key   = "PRI";
        else if (col.unique)   key   = "UNI";
        if (col.autoIncrement) extra = "auto_increment";

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

QueryResult Executor::execAlterTable(const AlterTableNode& n, Session& s) {
    std::string db = resolveDb(n.database, s);
    switch (n.action) {
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

QueryResult Executor::execCreateIndex(const CreateIndexNode& n, Session& s) {
    std::string db = resolveDb(n.database, s);
    idxMgr_.createIndex(db, n.table, n.indexName, n.columns, n.unique);
    return QueryResult::ok("Index '" + n.indexName + "' created (schema only).");
}

QueryResult Executor::execDropIndex(const DropIndexNode& n, Session& s) {
    std::string db = resolveDb(n.database, s);
    idxMgr_.dropIndex(db, n.table, n.indexName);
    return QueryResult::ok("Index '" + n.indexName + "' dropped.");
}

// ============================================================
// DML – INSERT
// ============================================================

QueryResult Executor::execInsert(const InsertNode& n, Session& s) {
    std::string db = resolveDb(n.database, s);
    auto defOpt = tblMgr_.describeTable(db, n.table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND,
                          "Unknown table '" + n.table + "'");
    const auto& def = *defOpt;

    int64_t lastId = -1;
    int affected   = 0;

    for (const auto& valueRow : n.valueRows) {
        std::map<std::string, FieldValue> record;

        if (n.columns.empty()) {
            // 按列顺序赋值（跳过 auto_increment 列若值为 0 或 NULL）
            for (size_t i = 0; i < def.columns.size() && i < valueRow.size(); ++i)
                record[def.columns[i].name] = coerce(valueRow[i], def.columns[i]);
        } else {
            if (n.columns.size() != valueRow.size())
                throw DBException(ErrorCode::COLUMN_INVALID,
                                  "Column count doesn't match value count");
            for (size_t i = 0; i < n.columns.size(); ++i) {
                // 找到列定义
                const ColumnDefinition* colDef = nullptr;
                for (const auto& c : def.columns)
                    if (c.name == n.columns[i]) { colDef = &c; break; }
                if (!colDef)
                    throw DBException(ErrorCode::COLUMN_NOT_FOUND,
                                      "Unknown column '" + n.columns[i] + "'");
                record[n.columns[i]] = coerce(valueRow[i], *colDef);
            }
        }

        // 处理 AUTO_INCREMENT
        for (const auto& col : def.columns) {
            if (!col.autoIncrement) continue;
            auto it = record.find(col.name);
            bool needGen = (it == record.end())
                || std::holds_alternative<std::monostate>(it->second)
                || (std::holds_alternative<int64_t>(it->second)
                    && std::get<int64_t>(it->second) == 0);
            if (needGen) {
                int64_t newId = tblMgr_.nextAutoIncrement(db, n.table);
                record[col.name] = newId;
                lastId = newId;
            } else if (std::holds_alternative<int64_t>(it->second)) {
                lastId = std::get<int64_t>(it->second);
            }
        }

        // 补全缺失列的默认值 / NOT NULL 检查 / VARCHAR 长度检查
        for (const auto& col : def.columns) {
            if (record.find(col.name) == record.end()) {
                if (!col.defaultValue.empty()) {
                    record[col.name] = parseDefault(col);
                } else if (col.nullable) {
                    record[col.name] = std::monostate{};
                } else if (!col.autoIncrement) {
                    throw DBException(ErrorCode::CONSTRAINT_VIOLATION,
                                      "Column '" + col.name + "' cannot be NULL");
                }
            } else {
                // NOT NULL 检查
                if (!col.nullable && !col.autoIncrement
                    && std::holds_alternative<std::monostate>(record[col.name]))
                    throw DBException(ErrorCode::CONSTRAINT_VIOLATION,
                                      "Column '" + col.name + "' cannot be NULL");
                // VARCHAR 长度
                record[col.name] = validateField(record[col.name], col);
            }
        }

        // UNIQUE / PRIMARY KEY 唯一性检查
        checkUniqueConstraints(recMgr_, db, n.table, def, record);

        recMgr_.insert(db, n.table, record);
        ++affected;
    }

    QueryResult r = QueryResult::dml(affected, lastId);
    r.message = std::to_string(affected) + " row(s) inserted.";
    return r;
}

// ============================================================
// DML – SELECT
// ============================================================

QueryResult Executor::execSelect(const SelectNode& n, Session& s) {
    std::string db = resolveDb(n.database, s);
    auto defOpt = tblMgr_.describeTable(db, n.table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND,
                          "Unknown table '" + n.table + "'");
    const auto& def = *defOpt;

    ExprEvaluator eval;

    // 1. 全表扫描 + WHERE 过滤
    auto rawRows = recMgr_.scan(db, n.table);
    std::vector<std::map<std::string,FieldValue>> filtered;
    for (const auto& row : rawRows) {
        auto m = rowToMap(def, row);
        if (!n.where || eval.evaluate(*n.where, m))
            filtered.push_back(std::move(m));
    }

    // 2. 聚合函数处理（简化：仅支持无 GROUP BY 的全局聚合）
    bool hasAgg = false;
    for (const auto& sc : n.columns)
        if (sc.kind == SelectColumn::Kind::AGGREGATE) { hasAgg = true; break; }

    if (hasAgg && n.groupBy.empty()) {
        QueryResult r;
        r.type = QueryResult::Type::SELECT;
        Row aggRow;
        for (const auto& sc : n.columns) {
            if (sc.kind != SelectColumn::Kind::AGGREGATE) continue;
            std::string name = sc.aggregate.alias.empty()
                ? std::string(sc.aggregate.func == AggFunc::COUNT ? "COUNT" :
                              sc.aggregate.func == AggFunc::SUM   ? "SUM"   :
                              sc.aggregate.func == AggFunc::MAX   ? "MAX"   :
                              sc.aggregate.func == AggFunc::MIN   ? "MIN"   : "AVG")
                  + "(" + sc.aggregate.column + ")"
                : sc.aggregate.alias;

            switch (sc.aggregate.func) {
                case AggFunc::COUNT: {
                    int64_t cnt = 0;
                    if (sc.aggregate.column == "*") {
                        cnt = static_cast<int64_t>(filtered.size());
                    } else {
                        for (const auto& m : filtered) {
                            auto it = m.find(sc.aggregate.column);
                            if (it != m.end() && !std::holds_alternative<std::monostate>(it->second))
                                ++cnt;
                        }
                    }
                    r.columns.push_back({name, FieldType::INTEGER});
                    aggRow.push_back(FieldValue{cnt});
                    break;
                }
                case AggFunc::SUM: case AggFunc::AVG: {
                    double sum = 0;
                    int64_t cnt2 = 0;
                    for (const auto& m : filtered) {
                        auto it = m.find(sc.aggregate.column);
                        if (it != m.end()) {
                            if (std::holds_alternative<int64_t>(it->second))
                                sum += static_cast<double>(std::get<int64_t>(it->second));
                            else if (std::holds_alternative<double>(it->second))
                                sum += std::get<double>(it->second);
                            ++cnt2;
                        }
                    }
                    r.columns.push_back({name, FieldType::DOUBLE});
                    aggRow.push_back(FieldValue{sc.aggregate.func == AggFunc::AVG && cnt2 > 0
                                                ? sum / cnt2 : sum});
                    break;
                }
                case AggFunc::MAX: case AggFunc::MIN: {
                    FieldValue best = std::monostate{};
                    for (const auto& m : filtered) {
                        auto it = m.find(sc.aggregate.column);
                        if (it == m.end() || std::holds_alternative<std::monostate>(it->second))
                            continue;
                        if (std::holds_alternative<std::monostate>(best)) {
                            best = it->second;
                        } else {
                            bool isBetter;
                            if (std::holds_alternative<int64_t>(it->second)
                                && std::holds_alternative<int64_t>(best))
                                isBetter = sc.aggregate.func == AggFunc::MAX
                                    ? std::get<int64_t>(it->second) > std::get<int64_t>(best)
                                    : std::get<int64_t>(it->second) < std::get<int64_t>(best);
                            else if (std::holds_alternative<double>(it->second)
                                     || std::holds_alternative<double>(best)) {
                                double a = std::holds_alternative<int64_t>(it->second)
                                    ? static_cast<double>(std::get<int64_t>(it->second))
                                    : std::get<double>(it->second);
                                double b = std::holds_alternative<int64_t>(best)
                                    ? static_cast<double>(std::get<int64_t>(best))
                                    : std::get<double>(best);
                                isBetter = sc.aggregate.func == AggFunc::MAX ? a > b : a < b;
                            } else {
                                std::string a = fvToStr(it->second), b = fvToStr(best);
                                isBetter = sc.aggregate.func == AggFunc::MAX ? a > b : a < b;
                            }
                            if (isBetter) best = it->second;
                        }
                    }
                    r.columns.push_back({name, FieldType::DOUBLE});
                    aggRow.push_back(best);
                    break;
                }
            }
        }
        if (!aggRow.empty()) r.rows.push_back(aggRow);
        r.rowCount = static_cast<int>(r.rows.size());
        return r;
    }

    // 3. ORDER BY
    if (!n.orderBy.empty()) {
        std::stable_sort(filtered.begin(), filtered.end(),
            [&](const std::map<std::string,FieldValue>& a,
                const std::map<std::string,FieldValue>& b) {
                for (const auto& ob : n.orderBy) {
                    auto ia = a.find(ob.columnName);
                    auto ib = b.find(ob.columnName);
                    FieldValue va = (ia != a.end()) ? ia->second : std::monostate{};
                    FieldValue vb = (ib != b.end()) ? ib->second : std::monostate{};
                    // compare
                    bool aNul = std::holds_alternative<std::monostate>(va);
                    bool bNul = std::holds_alternative<std::monostate>(vb);
                    if (aNul && bNul) continue;
                    if (aNul) return !ob.ascending;
                    if (bNul) return ob.ascending;
                    if (std::holds_alternative<int64_t>(va) && std::holds_alternative<int64_t>(vb)) {
                        if (std::get<int64_t>(va) != std::get<int64_t>(vb))
                            return ob.ascending ? std::get<int64_t>(va) < std::get<int64_t>(vb)
                                               : std::get<int64_t>(va) > std::get<int64_t>(vb);
                    } else if (std::holds_alternative<double>(va) || std::holds_alternative<double>(vb)) {
                        double da = std::holds_alternative<int64_t>(va)
                            ? static_cast<double>(std::get<int64_t>(va)) : std::get<double>(va);
                        double db2 = std::holds_alternative<int64_t>(vb)
                            ? static_cast<double>(std::get<int64_t>(vb)) : std::get<double>(vb);
                        if (da != db2) return ob.ascending ? da < db2 : da > db2;
                    } else {
                        std::string sa = fvToStr(va), sb = fvToStr(vb);
                        if (sa != sb) return ob.ascending ? sa < sb : sa > sb;
                    }
                }
                return false;
            });
    }

    // 4. LIMIT / OFFSET
    size_t startIdx = static_cast<size_t>(n.offset >= 0 ? n.offset : 0);
    size_t endIdx   = filtered.size();
    if (n.limit >= 0)
        endIdx = std::min(endIdx, startIdx + static_cast<size_t>(n.limit));
    if (startIdx >= filtered.size()) startIdx = filtered.size();

    // 5. 投影：确定输出列
    QueryResult r;
    r.type = QueryResult::Type::SELECT;

    // 确定输出列元数据
    bool hasWildcard = false;
    for (const auto& sc : n.columns)
        if (sc.kind == SelectColumn::Kind::WILDCARD) { hasWildcard = true; break; }

    if (hasWildcard) {
        for (const auto& col : def.columns)
            r.columns.push_back({col.name, col.type});
    } else {
        for (const auto& sc : n.columns) {
            if (sc.kind == SelectColumn::Kind::COLUMN_REF) {
                std::string name = sc.alias.empty() ? sc.columnName : sc.alias;
                FieldType type = FieldType::VARCHAR;
                for (const auto& col : def.columns)
                    if (col.name == sc.columnName) { type = col.type; break; }
                r.columns.push_back({name, type});
            }
        }
    }

    // 6. 生成结果行
    for (size_t i = startIdx; i < endIdx; ++i) {
        const auto& m = filtered[i];
        Row outRow;
        if (hasWildcard) {
            for (const auto& col : def.columns) {
                auto it = m.find(col.name);
                outRow.push_back(it != m.end() ? it->second : std::monostate{});
            }
        } else {
            for (const auto& sc : n.columns) {
                if (sc.kind != SelectColumn::Kind::COLUMN_REF) continue;
                auto it = m.find(sc.columnName);
                outRow.push_back(it != m.end() ? it->second : std::monostate{});
            }
        }
        r.rows.push_back(std::move(outRow));
    }
    r.rowCount = static_cast<int>(r.rows.size());
    return r;
}

// ============================================================
// DML – UPDATE
// ============================================================

QueryResult Executor::execUpdate(const UpdateNode& n, Session& s) {
    std::string db = resolveDb(n.database, s);
    auto defOpt = tblMgr_.describeTable(db, n.table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND,
                          "Unknown table '" + n.table + "'");
    const auto& def = *defOpt;

    ExprEvaluator eval;

    // 先验证 SET 列名合法
    for (const auto& a : n.assignments) {
        bool found = false;
        for (const auto& col : def.columns)
            if (col.name == a.columnName) { found = true; break; }
        if (!found)
            throw DBException(ErrorCode::COLUMN_NOT_FOUND,
                              "Unknown column '" + a.columnName + "' in SET clause");
    }

    auto withOffsets = recMgr_.scanWithOffsets(db, n.table);
    int affected = 0;

    for (const auto& [offset, row] : withOffsets) {
        auto m = rowToMap(def, row);
        if (n.where && !eval.evaluate(*n.where, m)) continue;

        // 应用赋值（含类型转换 + 约束检查）
        for (const auto& a : n.assignments) {
            for (const auto& col : def.columns) {
                if (col.name != a.columnName) continue;

                FieldValue newVal = coerce(a.value, col);

                // NOT NULL 检查
                if (!col.nullable && !col.autoIncrement
                    && std::holds_alternative<std::monostate>(newVal))
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

QueryResult Executor::execDelete(const DeleteNode& n, Session& s) {
    std::string db = resolveDb(n.database, s);
    auto defOpt = tblMgr_.describeTable(db, n.table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND,
                          "Unknown table '" + n.table + "'");
    const auto& def = *defOpt;

    ExprEvaluator eval;
    auto withOffsets = recMgr_.scanWithOffsets(db, n.table);
    int affected = 0;

    for (const auto& [offset, row] : withOffsets) {
        auto m = rowToMap(def, row);
        if (n.where && !eval.evaluate(*n.where, m)) continue;
        recMgr_.remove(db, n.table, offset);
        ++affected;
    }

    QueryResult r = QueryResult::dml(affected);
    r.message = std::to_string(affected) + " row(s) deleted.";
    return r;
}
