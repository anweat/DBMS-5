#pragma once

#include "../../types.h"
#include <string>
#include <vector>

// ---- Token ----

enum class TokenType
{
    // DDL 关键字
    CREATE,
    DROP,
    DATABASE,
    TABLE,
    INDEX,
    SHOW,
    DATABASES,
    TABLES,
    USE,
    DESCRIBE,
    ALTER,
    ADD,
    MODIFY,
    COLUMN,
    // DML 关键字
    INSERT,
    INTO,
    VALUES,
    SELECT,
    DISTINCT,
    FROM,
    WHERE,
    UPDATE,
    SET,
    DELETE,
    // 约束 / 类型
    PRIMARY,
    KEY,
    FOREIGN,
    REFERENCES,
    UNIQUE,
    NOT,
    NULL_KW,
    DEFAULT,
    AUTO_INCREMENT,
    INT_KW,
    INTEGER_KW,
    DOUBLE_KW,
    FLOAT_KW,
    VARCHAR_KW,
    BOOL_KW,
    DATETIME_KW,
    CONSTRAINT,
    // 查询修饰
    ORDER,
    BY,
    ASC,
    DESC,
    LIMIT,
    OFFSET,
    GROUP,
    HAVING,
    AS,
    AND,
    OR,
    IN,
    LIKE,
    IS,
    BETWEEN,
    COUNT,
    SUM,
    MAX,
    MIN,
    AVG,
    IF,
    EXISTS,
    // JOIN keywords
    JOIN,
    INNER,
    LEFT,
    RIGHT,
    OUTER,
    CROSS,
    // 事务
    BEGIN,
    COMMIT,
    ROLLBACK,
    TRANSACTION,
    // 安全
    GRANT,
    REVOKE,
    ON,
    TO,
    WITH,
    OPTION,
    ALL,
    PRIVILEGES,
    USER,
    PASSWORD,
    CONNECT,
    IDENTIFIED,
    // 备份/恢复
    BACKUP,
    RESTORE,
    // 运算符
    EQ,
    NEQ,
    LT,
    LE,
    GT,
    GE,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,
    LPAREN,
    RPAREN,
    COMMA,
    SEMICOLON,
    DOT,
    ASSIGN,
    // 字面量
    INT_LITERAL,
    DOUBLE_LITERAL,
    STRING_LITERAL,
    BOOL_LITERAL,
    NULL_LITERAL,
    // 标识符
    IDENTIFIER,
    // 特殊
    EOF_TOKEN,
    UNKNOWN
};

struct Token
{
    TokenType type = TokenType::UNKNOWN;
    std::string value;
    int line = 0;
    int col = 0;
};

// ---- Lexer ----

class Lexer
{
public:
    /** 将 SQL 字符串拆分为 Token 序列，末尾自动附加 EOF_TOKEN */
    std::vector<Token> tokenize(const std::string &sql);
};
