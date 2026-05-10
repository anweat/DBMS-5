#pragma once

#include "types.h"
#include <memory>
#include <string>
#include <vector>
#include <map>

// ============================================================
// 表达式树（WHERE / ON 子句）
// ============================================================

enum class ExprOp
{
    // 比较
    EQ,
    NEQ,
    LT,
    LE,
    GT,
    GE,
    LIKE,
    IN,
    IS_NULL,
    IS_NOT_NULL,
    // 逻辑
    AND,
    OR,
    NOT
};

struct WhereExpr
{
    enum class Kind
    {
        COMPARISON,
        LOGICAL,
        COLUMN_REF,
        LITERAL
    } kind;

    ExprOp op = ExprOp::EQ;

    // COMPARISON / LOGICAL 子节点
    std::shared_ptr<WhereExpr> left;
    std::shared_ptr<WhereExpr> right; // NOT 时为 nullptr

    // COLUMN_REF
    std::string tableAlias; // 可选前缀：t.col 中的 t
    std::string columnName;

    // LITERAL
    FieldValue value;

    // IN 列表
    std::vector<FieldValue> inList;
};

// ============================================================
// 聚合表达式（SELECT 子句中）
// ============================================================

enum class AggFunc
{
    COUNT,
    SUM,
    MAX,
    MIN,
    AVG
};

struct AggregateExpr
{
    AggFunc func;
    std::string column; // "*" 表示 COUNT(*)
    std::string alias;  // AS 别名
};

// ============================================================
// SELECT 列描述
// ============================================================

struct SelectColumn
{
    enum class Kind
    {
        WILDCARD,      // *
        QUALIFIED_WILDCARD, // alias.*
        COLUMN_REF,
        AGGREGATE
    } kind = Kind::COLUMN_REF;

    std::string tableAlias;  // For QUALIFIED_WILDCARD and COLUMN_REF
    std::string columnName;
    AggregateExpr aggregate;
    std::string alias; // AS 别名
};

struct OrderByExpr
{
    std::string tableAlias; // 可选前缀：t.col 中的 t
    std::string columnName;
    bool ascending = true;
};

// ============================================================
// 表引用（用于 SELECT FROM 多表）
// ============================================================

enum class JoinType
{
    NONE,        // 单表或逗号分隔的多表（隐式 CROSS JOIN）
    INNER,
    LEFT,
    RIGHT,
    CROSS
};

struct TableRef
{
    std::string database; // 可选显式指定库名
    std::string table;
    std::string alias; // AS 别名或无 AS 的别名

    // 显式 JOIN 信息
    JoinType joinType = JoinType::NONE;
    std::shared_ptr<WhereExpr> onCondition; // JOIN ... ON condition
};

// ============================================================
// AST 节点基类
// ============================================================

enum class NodeType
{
    // DDL – 数据库
    CREATE_DATABASE,
    DROP_DATABASE,
    SHOW_DATABASES,
    USE_DATABASE,
    // DDL – 表
    CREATE_TABLE,
    DROP_TABLE,
    SHOW_TABLES,
    DESCRIBE_TABLE,
    ALTER_TABLE,
    // DDL – 索引
    CREATE_INDEX,
    DROP_INDEX,
    // DML
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    // 事务
    BEGIN_TRANSACTION,
    COMMIT,
    ROLLBACK,
    // 安全
    CREATE_USER,
    DROP_USER,
    GRANT,
    REVOKE,
    CONNECT,
    // 备份/恢复
    BACKUP_DATABASE,
    RESTORE_DATABASE
};

struct ASTNode
{
    NodeType type;
    virtual ~ASTNode() = default;
};

using ASTNodePtr = std::unique_ptr<ASTNode>;

// ============================================================
// DDL – 数据库
// ============================================================

struct CreateDatabaseNode : ASTNode
{
    std::string name;
    bool ifNotExists = false;
};

struct DropDatabaseNode : ASTNode
{
    std::string name;
    bool ifExists = false;
};

struct ShowDatabasesNode : ASTNode
{
};

struct UseDatabaseNode : ASTNode
{
    std::string name;
};

// ============================================================
// DDL – 表
// ============================================================

struct CreateTableNode : ASTNode
{
    std::string database; // 可选显式指定库名
    TableDefinition def;
    bool ifNotExists = false;
};

struct DropTableNode : ASTNode
{
    std::string table;
    std::string database;
    bool ifExists = false;
};

struct ShowTablesNode : ASTNode
{
};

struct DescribeTableNode : ASTNode
{
    std::string table;
    std::string database;
};

enum class AlterAction
{
    ADD_COLUMN,
    MODIFY_COLUMN,
    DROP_COLUMN
};

struct AlterTableNode : ASTNode
{
    std::string table;
    std::string database;
    AlterAction action;
    ColumnDefinition column; // ADD / MODIFY 时有效
    std::string dropColName; // DROP 时有效
};

// ============================================================
// DDL – 索引
// ============================================================

struct CreateIndexNode : ASTNode
{
    std::string indexName;
    std::string table;
    std::string database;
    std::vector<std::string> columns;
    bool unique = false;
};

struct DropIndexNode : ASTNode
{
    std::string indexName;
    std::string table;
    std::string database;
};

// ============================================================
// DML
// ============================================================

struct InsertNode : ASTNode
{
    std::string table;
    std::string database;
    std::vector<std::string> columns;               // 为空时按表列顺序
    std::vector<std::vector<FieldValue>> valueRows; // 支持多行插入
};

struct SelectNode : ASTNode
{
    std::vector<SelectColumn> columns;

    // 多表支持：fromTables[0] 是主表
    // 单表查询时仍可用 table/database/tableAlias（兼容）
    std::vector<TableRef> fromTables;

    // 保留单表字段用于向后兼容和简单查询
    std::string table;
    std::string database;
    std::string tableAlias;

    std::shared_ptr<WhereExpr> where; // nullptr 表示无 WHERE
    std::vector<std::string> groupBy;
    std::shared_ptr<WhereExpr> having;
    std::vector<OrderByExpr> orderBy;
    int limit = -1; // -1 表示无 LIMIT
    int offset = 0;
    bool distinct = false;
};

struct UpdateAssignment
{
    std::string columnName;
    FieldValue value;
};

struct UpdateNode : ASTNode
{
    std::string table;
    std::string database;
    std::vector<UpdateAssignment> assignments;
    std::shared_ptr<WhereExpr> where;
};

struct DeleteNode : ASTNode
{
    std::string table;
    std::string database;
    std::shared_ptr<WhereExpr> where;
};

// ============================================================
// 事务
// ============================================================

struct BeginNode : ASTNode
{
};
struct CommitNode : ASTNode
{
};
struct RollbackNode : ASTNode
{
};

// ============================================================
// 安全管理
// ============================================================

// Note: Privilege is defined in types.h

struct CreateUserNode : ASTNode
{
    std::string username;
    std::string password;
};

struct DropUserNode : ASTNode
{
    std::string username;
};

struct GrantNode : ASTNode
{
    std::vector<Privilege> privileges;
    std::string database; // "*" 表示所有库
    std::string table;    // "*" 表示所有表
    std::string username;
};

struct RevokeNode : ASTNode
{
    std::vector<Privilege> privileges;
    std::string database;
    std::string table;
    std::string username;
};

struct ConnectNode : ASTNode
{
    std::string username;
    std::string password;
};

// ============================================================
// 备份 / 恢复
// ============================================================

struct BackupDatabaseNode : ASTNode {
    std::string database;   // 要备份的数据库名
    std::string filepath;   // 目标 SQL 文件路径
};

struct RestoreDatabaseNode : ASTNode {
    std::string database;   // 恢复目标库名（可选，可直接在文件内 USE）
    std::string filepath;   // 源 SQL 文件路径
};
