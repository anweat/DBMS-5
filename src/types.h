#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <stdexcept>

// ============================================================
// 基础字段类型
// ============================================================

enum class FieldType { INTEGER, DOUBLE, BOOL, VARCHAR, DATETIME };

// 单格值：monostate 表示 NULL
using FieldValue = std::variant<std::monostate, int64_t, double, bool, std::string>;

// 一行数据，顺序与 TableDefinition.columns 对应
using Row = std::vector<FieldValue>;

// ============================================================
// 表结构定义（AST 与 StorageEngine 共用）
// ============================================================

struct ColumnDefinition {
    std::string name;
    FieldType   type;
    int         length        = 255;   // VARCHAR 最大长度
    bool        nullable      = true;
    std::string defaultValue;          // 空串表示无默认值
    bool        primaryKey    = false;
    bool        autoIncrement = false;
    bool        unique        = false;
};

struct ForeignKeyDefinition {
    std::string              constraintName;
    std::vector<std::string> columns;
    std::string              refTable;
    std::vector<std::string> refColumns;
};

struct TableDefinition {
    std::string                       name;
    std::vector<ColumnDefinition>     columns;
    std::vector<ForeignKeyDefinition> foreignKeys;
};

struct IndexDefinition {
    std::string              name;
    std::vector<std::string> columns;
    bool                     unique = false;
};

// ============================================================
// 权限枚举（用于用户管理和 AST）
// ============================================================

enum class Privilege { SELECT, INSERT, UPDATE, DELETE, ALL };

// ============================================================
// 错误体系
// ============================================================

enum class ErrorCode {
    DB_ALREADY_EXISTS, DB_NOT_FOUND, DB_SYSTEM, DB_NAME_INVALID,
    TABLE_ALREADY_EXISTS, TABLE_NOT_FOUND, TABLE_NAME_INVALID,
    COLUMN_NOT_FOUND, COLUMN_ALREADY_EXISTS, COLUMN_INVALID,
    CONSTRAINT_VIOLATION, DUPLICATE_KEY, FOREIGN_KEY_VIOLATION,
    TRANSACTION_NOT_FOUND, TRANSACTION_CONFLICT,
    PERMISSION_DENIED, SQL_SYNTAX_ERROR, FILE_IO_ERROR, UNKNOWN_ERROR
};

struct DBError {
    ErrorCode   code;
    std::string message;
};

// 统一运行时异常：所有模块抛出此类型，由 DBEngine 捕获后填入 QueryResult
class DBException : public std::runtime_error {
public:
    explicit DBException(ErrorCode code, const std::string& msg)
        : std::runtime_error(msg), error_{code, msg} {}

    const DBError& error() const { return error_; }

private:
    DBError error_;
};

// ============================================================
// 查询结果（引擎 → CLI 唯一传递载体）
// ============================================================

struct ColumnMeta {
    std::string name;
    FieldType   type;
};

struct QueryResult {
    enum class Type { SELECT, DDL, DML, ERROR } type = Type::DDL;

    // SELECT 时填充
    std::vector<ColumnMeta> columns;
    std::vector<Row>        rows;
    int                     rowCount     = 0;

    // DML 时填充
    int                     affectedRows = 0;
    int64_t                 insertId     = -1;

    // 通用
    std::string             message;
    std::optional<DBError>  error;
    long long               elapsedMs    = 0;  // 执行耗时（ms）

    // 快速构造帮助函数
    static QueryResult ok(const std::string& msg = "") {
        QueryResult r; r.type = Type::DDL; r.message = msg; return r;
    }
    static QueryResult dml(int affected, int64_t lastId = -1) {
        QueryResult r; r.type = Type::DML;
        r.affectedRows = affected; r.insertId = lastId; return r;
    }
    static QueryResult err(ErrorCode code, const std::string& msg) {
        QueryResult r; r.type = Type::ERROR;
        r.error = DBError{code, msg}; r.message = msg; return r;
    }
};
