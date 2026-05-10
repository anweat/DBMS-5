#pragma once

#include "../../types.h"
#include "../parser/AST.h"
#include "../../engine/DBEngine.h"
#include "../storage/DatabaseManager.h"
#include "../storage/TableManager.h"
#include "../storage/RecordManager.h"
#include "../storage/IndexManager.h"
#include "../storage/UserManager.h"
#include "../storage/TransactionManager.h"
#include "../storage/WalManager.h"

class Executor
{
public:
    explicit Executor(const std::string &dataDir);

    QueryResult execute(const ASTNode &ast, Session &session);

private:
    std::string dataDir_;
    DatabaseManager dbMgr_;
    TableManager tblMgr_;
    RecordManager recMgr_;
    IndexManager idxMgr_;
    UserManager userMgr_;
    TransactionManager txMgr_;
    WalManager walMgr_;     // write-ahead log / crash recovery

    // 解析 SQL 中指定的库名 or 使用当前会话库
    std::string resolveDb(const std::string &nodeDb, const Session &s);

    // 权限检查（若 session.user 为空则跳过）
    void checkPermission(const Session &s, const std::string &db,
                         const std::string &table, Privilege priv);

    // DDL – 数据库
    QueryResult execCreateDatabase(const CreateDatabaseNode &n, Session &s);
    QueryResult execDropDatabase(const DropDatabaseNode &n, Session &s);
    QueryResult execShowDatabases(Session &s);
    QueryResult execUseDatabase(const UseDatabaseNode &n, Session &s);

    // DDL – 表
    QueryResult execCreateTable(const CreateTableNode &n, Session &s);
    QueryResult execDropTable(const DropTableNode &n, Session &s);
    QueryResult execShowTables(Session &s);
    QueryResult execDescribeTable(const DescribeTableNode &n, Session &s);
    QueryResult execAlterTable(const AlterTableNode &n, Session &s);

    // DDL – 索引
    QueryResult execCreateIndex(const CreateIndexNode &n, Session &s);
    QueryResult execDropIndex(const DropIndexNode &n, Session &s);

    // DML
    QueryResult execInsert(const InsertNode &n, Session &s);
    QueryResult execSelect(const SelectNode &n, Session &s);
    QueryResult execUpdate(const UpdateNode &n, Session &s);
    QueryResult execDelete(const DeleteNode &n, Session &s);

    // 事务
    QueryResult execBegin(Session &s);
    QueryResult execCommit(Session &s);
    QueryResult execRollback(Session &s);

    // 安全
    QueryResult execCreateUser(const CreateUserNode &n, Session &s);
    QueryResult execDropUser(const DropUserNode &n, Session &s);
    QueryResult execGrant(const GrantNode &n, Session &s);
    QueryResult execRevoke(const RevokeNode &n, Session &s);
    QueryResult execConnect(const ConnectNode &n, Session &s);

    // 备份
    QueryResult execBackupDatabase(const BackupDatabaseNode &n, Session &s);
    QueryResult execRestoreDatabase(const RestoreDatabaseNode &n, Session &s);
};
