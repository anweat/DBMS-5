#include "Parser.h"
#include <algorithm>
#include <cassert>

// ============================================================
// 内部递归下降解析器
// ============================================================

namespace
{

    static std::string toUpper(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c)
                       { return std::toupper(c); });
        return s;
    }

    class ParserImpl
    {
    public:
        const std::vector<Token> &toks;
        size_t pos = 0;

        explicit ParserImpl(const std::vector<Token> &t) : toks(t) {}

        // ---- 基本工具 ----

        bool atEnd() const
        {
            return toks[pos].type == TokenType::EOF_TOKEN;
        }

        const Token &cur() const { return toks[pos]; }

        const Token &peek(int n = 0) const
        {
            size_t idx = pos + static_cast<size_t>(n);
            return idx < toks.size() ? toks[idx] : toks.back();
        }

        Token advance()
        {
            Token t = toks[pos];
            if (!atEnd())
                ++pos;
            return t;
        }

        bool check(TokenType t) const { return cur().type == t; }

        bool match(TokenType t)
        {
            if (check(t))
            {
                advance();
                return true;
            }
            return false;
        }

        Token expect(TokenType t, const std::string &msg)
        {
            if (!check(t))
                throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                                  msg + " near '" + cur().value + "'");
            return advance();
        }

        // 匹配 IDENTIFIER 或某些关键字作为标识符（如 user, key 等）
        bool isIdentOrKw() const
        {
            auto tt = cur().type;
            return tt == TokenType::IDENTIFIER;
        }

        std::string parseIdent()
        {
            if (cur().type == TokenType::IDENTIFIER)
                return advance().value;
            // 允许部分关键字充当列/表名
            static const std::vector<TokenType> allowed = {
                TokenType::KEY, TokenType::USER, TokenType::PASSWORD,
                TokenType::OPTION, TokenType::INDEX,
                TokenType::COUNT, TokenType::SUM, TokenType::MAX,
                TokenType::MIN, TokenType::AVG};
            for (auto t : allowed)
                if (cur().type == t)
                    return advance().value;
            throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                              "Expected identifier, got '" + cur().value + "'");
        }

        // 解析 [db.]table，返回 {database, table}
        std::pair<std::string, std::string> parseTableRef()
        {
            std::string first = parseIdent();
            if (match(TokenType::DOT))
            {
                return {first, parseIdent()};
            }
            return {"", first};
        }

        // 解析字面量值（含负号）
        FieldValue parseLiteral()
        {
            if (check(TokenType::MINUS))
            {
                advance();
                if (check(TokenType::INT_LITERAL))
                    return static_cast<int64_t>(-std::stoll(advance().value));
                if (check(TokenType::DOUBLE_LITERAL))
                    return -std::stod(advance().value);
                throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                                  "Expected number after '-'");
            }
            if (check(TokenType::INT_LITERAL))
                return static_cast<int64_t>(std::stoll(advance().value));
            if (check(TokenType::DOUBLE_LITERAL))
                return std::stod(advance().value);
            if (check(TokenType::STRING_LITERAL))
                return advance().value;
            if (check(TokenType::BOOL_LITERAL))
            {
                std::string v = advance().value;
                return v == "TRUE" || v == "true";
            }
            if (check(TokenType::NULL_KW) || check(TokenType::NULL_LITERAL))
            {
                advance();
                return std::monostate{};
            }
            throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                              "Expected literal value, got '" + cur().value + "'");
        }

        // 解析数据类型，返回 (FieldType, length)
        std::pair<FieldType, int> parseDataType()
        {
            switch (cur().type)
            {
            case TokenType::INT_KW:
            case TokenType::INTEGER_KW:
            {
                advance();
                int len = 0;
                if (match(TokenType::LPAREN))
                {
                    len = std::stoi(expect(TokenType::INT_LITERAL, "Expected length").value);
                    expect(TokenType::RPAREN, "Expected ')'");
                }
                return {FieldType::INTEGER, len};
            }
            case TokenType::DOUBLE_KW:
            case TokenType::FLOAT_KW:
                advance();
                return {FieldType::DOUBLE, 0};
            case TokenType::BOOL_KW:
                advance();
                return {FieldType::BOOL, 0};
            case TokenType::VARCHAR_KW:
            {
                advance();
                expect(TokenType::LPAREN, "Expected '(' after VARCHAR");
                int len = std::stoi(expect(TokenType::INT_LITERAL, "Expected length").value);
                expect(TokenType::RPAREN, "Expected ')'");
                return {FieldType::VARCHAR, len};
            }
            case TokenType::DATETIME_KW:
                advance();
                return {FieldType::DATETIME, 0};
            default:
                throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                                  "Expected data type, got '" + cur().value + "'");
            }
        }

        // 解析列定义（在 CREATE TABLE 括号中）
        ColumnDefinition parseColumnDef()
        {
            ColumnDefinition col;
            col.name = parseIdent();
            auto [type, len] = parseDataType();
            col.type = type;
            col.length = len;
            // 列级约束
            while (!atEnd() && !check(TokenType::COMMA) && !check(TokenType::RPAREN))
            {
                if (check(TokenType::NOT))
                {
                    advance();
                    expect(TokenType::NULL_KW, "Expected NULL after NOT");
                    col.nullable = false;
                }
                else if (check(TokenType::NULL_KW))
                {
                    advance();
                    col.nullable = true;
                }
                else if (check(TokenType::PRIMARY))
                {
                    advance();
                    expect(TokenType::KEY, "Expected KEY after PRIMARY");
                    col.primaryKey = true;
                    col.nullable = false;
                }
                else if (check(TokenType::AUTO_INCREMENT))
                {
                    advance();
                    col.autoIncrement = true;
                }
                else if (check(TokenType::UNIQUE))
                {
                    advance();
                    col.unique = true;
                }
                else if (check(TokenType::DEFAULT))
                {
                    advance();
                    FieldValue v = parseLiteral();
                    if (std::holds_alternative<std::string>(v))
                        col.defaultValue = std::get<std::string>(v);
                    else if (std::holds_alternative<int64_t>(v))
                        col.defaultValue = std::to_string(std::get<int64_t>(v));
                    else if (std::holds_alternative<double>(v))
                        col.defaultValue = std::to_string(std::get<double>(v));
                    else if (std::holds_alternative<bool>(v))
                        col.defaultValue = std::get<bool>(v) ? "TRUE" : "FALSE";
                }
                else
                {
                    break;
                }
            }
            return col;
        }

        // 解析逗号分隔的标识符列表
        std::vector<std::string> parseIdentList()
        {
            std::vector<std::string> ids;
            ids.push_back(parseIdent());
            while (match(TokenType::COMMA))
                ids.push_back(parseIdent());
            return ids;
        }

        // ============================================================
        // WHERE 表达式解析
        // ============================================================

        std::shared_ptr<WhereExpr> parseWhereExpr()
        {
            return parseOrExpr();
        }

        std::shared_ptr<WhereExpr> parseOrExpr()
        {
            auto left = parseAndExpr();
            while (check(TokenType::OR))
            {
                advance();
                auto right = parseAndExpr();
                auto e = std::make_shared<WhereExpr>();
                e->kind = WhereExpr::Kind::LOGICAL;
                e->op = ExprOp::OR;
                e->left = left;
                e->right = right;
                left = e;
            }
            return left;
        }

        std::shared_ptr<WhereExpr> parseAndExpr()
        {
            auto left = parseNotExpr();
            while (check(TokenType::AND))
            {
                advance();
                auto right = parseNotExpr();
                auto e = std::make_shared<WhereExpr>();
                e->kind = WhereExpr::Kind::LOGICAL;
                e->op = ExprOp::AND;
                e->left = left;
                e->right = right;
                left = e;
            }
            return left;
        }

        std::shared_ptr<WhereExpr> parseNotExpr()
        {
            if (check(TokenType::NOT))
            {
                advance();
                auto e = std::make_shared<WhereExpr>();
                e->kind = WhereExpr::Kind::LOGICAL;
                e->op = ExprOp::NOT;
                e->left = parseNotExpr();
                return e;
            }
            return parseAtom();
        }

        std::shared_ptr<WhereExpr> parseAtom()
        {
            if (match(TokenType::LPAREN))
            {
                auto e = parseWhereExpr();
                expect(TokenType::RPAREN, "Expected ')'");
                return e;
            }
            return parseComparison();
        }

        std::shared_ptr<WhereExpr> parseComparison()
        {
            // column_ref [table.]col  OR  agg_func(col)  (used in HAVING)
            auto colExpr = std::make_shared<WhereExpr>();
            colExpr->kind = WhereExpr::Kind::COLUMN_REF;
            std::string first = parseIdent();
            if (check(TokenType::LPAREN))
            {
                // Aggregate function call: MAX(salary) → stored as column "MAX(salary)"
                advance(); // consume '('
                std::string argCol;
                if (check(TokenType::STAR))
                {
                    advance();
                    argCol = "*";
                }
                else
                {
                    argCol = parseIdent();
                }
                expect(TokenType::RPAREN, "Expected ')' after aggregate argument");
                colExpr->columnName = first + "(" + argCol + ")";
            }
            else if (match(TokenType::DOT))
            {
                colExpr->tableAlias = first;
                colExpr->columnName = parseIdent();
            }
            else
            {
                colExpr->columnName = first;
            }

            // IS [NOT] NULL
            if (check(TokenType::IS))
            {
                advance();
                bool notNull = match(TokenType::NOT);
                expect(TokenType::NULL_KW, "Expected NULL after IS");
                auto e = std::make_shared<WhereExpr>();
                e->kind = WhereExpr::Kind::COMPARISON;
                e->op = notNull ? ExprOp::IS_NOT_NULL : ExprOp::IS_NULL;
                e->left = colExpr;
                return e;
            }

            // [NOT] IN (...)
            bool negated = false;
            if (check(TokenType::NOT))
            {
                advance();
                negated = true;
            }

            if (check(TokenType::IN))
            {
                advance();
                expect(TokenType::LPAREN, "Expected '(' after IN");
                auto e = std::make_shared<WhereExpr>();
                e->kind = WhereExpr::Kind::COMPARISON;
                e->op = ExprOp::IN;
                e->left = colExpr;
                while (!check(TokenType::RPAREN) && !atEnd())
                {
                    e->inList.push_back(parseLiteral());
                    if (!match(TokenType::COMMA))
                        break;
                }
                expect(TokenType::RPAREN, "Expected ')'");
                if (negated)
                {
                    auto notE = std::make_shared<WhereExpr>();
                    notE->kind = WhereExpr::Kind::LOGICAL;
                    notE->op = ExprOp::NOT;
                    notE->left = e;
                    return notE;
                }
                return e;
            }

            // [NOT] LIKE
            if (check(TokenType::LIKE))
            {
                advance();
                auto e = std::make_shared<WhereExpr>();
                e->kind = WhereExpr::Kind::COMPARISON;
                e->op = ExprOp::LIKE;
                e->left = colExpr;
                auto litE = std::make_shared<WhereExpr>();
                litE->kind = WhereExpr::Kind::LITERAL;
                litE->value = parseLiteral();
                e->right = litE;
                if (negated)
                {
                    auto notE = std::make_shared<WhereExpr>();
                    notE->kind = WhereExpr::Kind::LOGICAL;
                    notE->op = ExprOp::NOT;
                    notE->left = e;
                    return notE;
                }
                return e;
            }

            // [NOT] BETWEEN val1 AND val2  →  col >= val1 AND col <= val2
            if (check(TokenType::BETWEEN))
            {
                advance();
                FieldValue lo = parseLiteral();
                expect(TokenType::AND, "Expected AND after BETWEEN lower bound");
                FieldValue hi = parseLiteral();

                auto loLit = std::make_shared<WhereExpr>();
                loLit->kind = WhereExpr::Kind::LITERAL;
                loLit->value = lo;
                auto hiLit = std::make_shared<WhereExpr>();
                hiLit->kind = WhereExpr::Kind::LITERAL;
                hiLit->value = hi;

                // col >= lo
                auto geE = std::make_shared<WhereExpr>();
                geE->kind = WhereExpr::Kind::COMPARISON;
                geE->op = ExprOp::GE;
                geE->left = colExpr;
                geE->right = loLit;

                // col <= hi (copy of colExpr)
                auto colExpr2 = std::make_shared<WhereExpr>(*colExpr);
                auto leE = std::make_shared<WhereExpr>();
                leE->kind = WhereExpr::Kind::COMPARISON;
                leE->op = ExprOp::LE;
                leE->left = colExpr2;
                leE->right = hiLit;

                // AND
                auto andE = std::make_shared<WhereExpr>();
                andE->kind = WhereExpr::Kind::LOGICAL;
                andE->op = ExprOp::AND;
                andE->left = geE;
                andE->right = leE;

                if (negated)
                {
                    auto notE = std::make_shared<WhereExpr>();
                    notE->kind = WhereExpr::Kind::LOGICAL;
                    notE->op = ExprOp::NOT;
                    notE->left = andE;
                    return notE;
                }
                return andE;
            }

            if (negated)
                throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                                  "Expected IN, LIKE, or BETWEEN after NOT");

            // comparison operator
            ExprOp op;
            switch (cur().type)
            {
            case TokenType::EQ:
                advance();
                op = ExprOp::EQ;
                break;
            case TokenType::NEQ:
                advance();
                op = ExprOp::NEQ;
                break;
            case TokenType::LT:
                advance();
                op = ExprOp::LT;
                break;
            case TokenType::LE:
                advance();
                op = ExprOp::LE;
                break;
            case TokenType::GT:
                advance();
                op = ExprOp::GT;
                break;
            case TokenType::GE:
                advance();
                op = ExprOp::GE;
                break;
            default:
                throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                                  "Expected comparison operator near '" + cur().value + "'");
            }
            auto litE = std::make_shared<WhereExpr>();
            litE->kind = WhereExpr::Kind::LITERAL;
            litE->value = parseLiteral();

            auto e = std::make_shared<WhereExpr>();
            e->kind = WhereExpr::Kind::COMPARISON;
            e->op = op;
            e->left = colExpr;
            e->right = litE;
            return e;
        }

        // ============================================================
        // SELECT 列列表
        // ============================================================

        std::vector<SelectColumn> parseSelectColumns()
        {
            std::vector<SelectColumn> cols;
            do
            {
                SelectColumn sc;
                // *
                if (match(TokenType::STAR))
                {
                    sc.kind = SelectColumn::Kind::WILDCARD;
                    cols.push_back(sc);
                    continue;
                }
                // 聚合函数
                auto tryAgg = [&](AggFunc f, TokenType tt) -> bool
                {
                    if (!check(tt))
                        return false;
                    advance();
                    expect(TokenType::LPAREN, "Expected '('");
                    sc.kind = SelectColumn::Kind::AGGREGATE;
                    sc.aggregate.func = f;
                    if (match(TokenType::STAR))
                        sc.aggregate.column = "*";
                    else
                        sc.aggregate.column = parseIdent();
                    expect(TokenType::RPAREN, "Expected ')'");
                    if (match(TokenType::AS))
                        sc.aggregate.alias = sc.alias = parseIdent();
                    cols.push_back(sc);
                    return true;
                };
                if (tryAgg(AggFunc::COUNT, TokenType::COUNT))
                    continue;
                if (tryAgg(AggFunc::SUM, TokenType::SUM))
                    continue;
                if (tryAgg(AggFunc::MAX, TokenType::MAX))
                    continue;
                if (tryAgg(AggFunc::MIN, TokenType::MIN))
                    continue;
                if (tryAgg(AggFunc::AVG, TokenType::AVG))
                    continue;

                // [table.]column [AS alias]
                sc.kind = SelectColumn::Kind::COLUMN_REF;
                std::string first = parseIdent();
                if (match(TokenType::DOT))
                {
                    sc.tableAlias = first;
                    sc.columnName = parseIdent();
                }
                else
                {
                    sc.columnName = first;
                }
                if (match(TokenType::AS))
                    sc.alias = parseIdent();
                cols.push_back(sc);
            } while (match(TokenType::COMMA));
            return cols;
        }

        // 解析用户名：可以是字符串字面量 'alice' 或标识符 alice
        std::string parseUsername()
        {
            if (check(TokenType::STRING_LITERAL))
                return advance().value;
            return parseIdent();
        }

        // ============================================================
        // 顶层 parseStatement
        // ============================================================

        ASTNodePtr parseStatement()
        {
            switch (cur().type)
            {
            case TokenType::CREATE:
                return parseCreate();
            case TokenType::DROP:
                return parseDrop();
            case TokenType::SHOW:
                return parseShow();
            case TokenType::USE:
                return parseUse();
            case TokenType::DESCRIBE:
                return parseDescribe();
            case TokenType::ALTER:
                return parseAlter();
            case TokenType::INSERT:
                return parseInsert();
            case TokenType::SELECT:
                return parseSelect();
            case TokenType::UPDATE:
                return parseUpdate();
            case TokenType::DELETE:
                return parseDelete();
            case TokenType::BEGIN:
                return parseBegin();
            case TokenType::COMMIT:
            {
                advance();
                auto n = std::make_unique<CommitNode>();
                n->type = NodeType::COMMIT;
                return n;
            }
            case TokenType::ROLLBACK:
            {
                advance();
                auto n = std::make_unique<RollbackNode>();
                n->type = NodeType::ROLLBACK;
                return n;
            }
            case TokenType::GRANT:
                return parseGrant();
            case TokenType::REVOKE:
                return parseRevoke();
            case TokenType::CONNECT:
                return parseConnect();
            case TokenType::BACKUP:
                return parseBackup();
            case TokenType::RESTORE:
                return parseRestore();
            default:
                throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                                  "Unexpected token '" + cur().value + "'");
            }
        }

        // ---- CREATE 分支 ----
        ASTNodePtr parseCreate()
        {
            expect(TokenType::CREATE, "");
            switch (cur().type)
            {
            case TokenType::DATABASE:
                return parseCreateDatabase();
            case TokenType::TABLE:
                return parseCreateTable();
            case TokenType::UNIQUE:
            case TokenType::INDEX:
                return parseCreateIndex();
            case TokenType::USER:
                return parseCreateUser();
            default:
                throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                                  "Expected DATABASE/TABLE/INDEX/USER after CREATE");
            }
        }

        ASTNodePtr parseCreateDatabase()
        {
            expect(TokenType::DATABASE, "");
            auto n = std::make_unique<CreateDatabaseNode>();
            n->type = NodeType::CREATE_DATABASE;
            if (match(TokenType::IF))
            {
                expect(TokenType::NOT, "Expected NOT after IF");
                expect(TokenType::EXISTS, "Expected EXISTS after NOT");
                n->ifNotExists = true;
            }
            n->name = parseIdent();
            return n;
        }

        ASTNodePtr parseCreateTable()
        {
            expect(TokenType::TABLE, "");
            auto n = std::make_unique<CreateTableNode>();
            n->type = NodeType::CREATE_TABLE;
            if (match(TokenType::IF))
            {
                expect(TokenType::NOT, "Expected NOT after IF");
                expect(TokenType::EXISTS, "Expected EXISTS after NOT");
                n->ifNotExists = true;
            }
            auto [db, tbl] = parseTableRef();
            n->database = db;
            n->def.name = tbl;

            expect(TokenType::LPAREN, "Expected '(' after table name");
            // 解析列定义 + 表级约束
            while (!check(TokenType::RPAREN) && !atEnd())
            {
                // PRIMARY KEY (...)
                if (check(TokenType::PRIMARY))
                {
                    advance();
                    expect(TokenType::KEY, "Expected KEY");
                    expect(TokenType::LPAREN, "Expected '('");
                    auto pkCols = parseIdentList();
                    expect(TokenType::RPAREN, "Expected ')'");
                    for (auto &col : n->def.columns)
                        for (const auto &pk : pkCols)
                            if (col.name == pk)
                            {
                                col.primaryKey = true;
                                col.nullable = false;
                            }
                }
                // [CONSTRAINT name] FOREIGN KEY
                else if (check(TokenType::CONSTRAINT) || check(TokenType::FOREIGN))
                {
                    std::string cname;
                    if (match(TokenType::CONSTRAINT))
                        cname = parseIdent();
                    expect(TokenType::FOREIGN, "Expected FOREIGN");
                    expect(TokenType::KEY, "Expected KEY");
                    expect(TokenType::LPAREN, "Expected '('");
                    auto cols = parseIdentList();
                    expect(TokenType::RPAREN, "Expected ')'");
                    expect(TokenType::REFERENCES, "Expected REFERENCES");
                    std::string refTable = parseIdent();
                    expect(TokenType::LPAREN, "Expected '('");
                    auto refCols = parseIdentList();
                    expect(TokenType::RPAREN, "Expected ')'");
                    ForeignKeyDefinition fk;
                    fk.constraintName = cname;
                    fk.columns = cols;
                    fk.refTable = refTable;
                    fk.refColumns = refCols;
                    n->def.foreignKeys.push_back(fk);
                }
                else
                {
                    n->def.columns.push_back(parseColumnDef());
                }
                if (!match(TokenType::COMMA))
                    break;
            }
            expect(TokenType::RPAREN, "Expected ')'");
            return n;
        }

        ASTNodePtr parseCreateIndex()
        {
            auto n = std::make_unique<CreateIndexNode>();
            n->type = NodeType::CREATE_INDEX;
            if (match(TokenType::UNIQUE))
                n->unique = true;
            expect(TokenType::INDEX, "Expected INDEX");
            n->indexName = parseIdent();
            expect(TokenType::ON, "Expected ON");
            auto [db, tbl] = parseTableRef();
            n->database = db;
            n->table = tbl;
            expect(TokenType::LPAREN, "Expected '('");
            n->columns = parseIdentList();
            expect(TokenType::RPAREN, "Expected ')'");
            return n;
        }

        // ---- DROP 分支 ----
        ASTNodePtr parseDrop()
        {
            expect(TokenType::DROP, "");
            switch (cur().type)
            {
            case TokenType::DATABASE:
                return parseDropDatabase();
            case TokenType::TABLE:
                return parseDropTable();
            case TokenType::INDEX:
                return parseDropIndex();
            case TokenType::USER:
                return parseDropUser();
            default:
                throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                                  "Expected DATABASE/TABLE/INDEX/USER after DROP");
            }
        }

        ASTNodePtr parseDropDatabase()
        {
            expect(TokenType::DATABASE, "");
            auto n = std::make_unique<DropDatabaseNode>();
            n->type = NodeType::DROP_DATABASE;
            if (match(TokenType::IF))
            {
                expect(TokenType::EXISTS, "Expected EXISTS after IF");
                n->ifExists = true;
            }
            n->name = parseIdent();
            return n;
        }

        ASTNodePtr parseDropTable()
        {
            expect(TokenType::TABLE, "");
            auto n = std::make_unique<DropTableNode>();
            n->type = NodeType::DROP_TABLE;
            if (match(TokenType::IF))
            {
                expect(TokenType::EXISTS, "Expected EXISTS after IF");
                n->ifExists = true;
            }
            auto [db, tbl] = parseTableRef();
            n->database = db;
            n->table = tbl;
            return n;
        }

        ASTNodePtr parseDropIndex()
        {
            expect(TokenType::INDEX, "");
            auto n = std::make_unique<DropIndexNode>();
            n->type = NodeType::DROP_INDEX;
            n->indexName = parseIdent();
            expect(TokenType::ON, "Expected ON");
            auto [db, tbl] = parseTableRef();
            n->database = db;
            n->table = tbl;
            return n;
        }

        // ---- SHOW ----
        ASTNodePtr parseShow()
        {
            expect(TokenType::SHOW, "");
            if (match(TokenType::DATABASES))
            {
                auto n = std::make_unique<ShowDatabasesNode>();
                n->type = NodeType::SHOW_DATABASES;
                return n;
            }
            if (match(TokenType::TABLES))
            {
                auto n = std::make_unique<ShowTablesNode>();
                n->type = NodeType::SHOW_TABLES;
                return n;
            }
            throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                              "Expected DATABASES or TABLES after SHOW");
        }

        // ---- USE ----
        ASTNodePtr parseUse()
        {
            expect(TokenType::USE, "");
            auto n = std::make_unique<UseDatabaseNode>();
            n->type = NodeType::USE_DATABASE;
            n->name = parseIdent();
            return n;
        }

        // ---- DESCRIBE ----
        ASTNodePtr parseDescribe()
        {
            expect(TokenType::DESCRIBE, "");
            auto n = std::make_unique<DescribeTableNode>();
            n->type = NodeType::DESCRIBE_TABLE;
            auto [db, tbl] = parseTableRef();
            n->database = db;
            n->table = tbl;
            return n;
        }

        // ---- ALTER TABLE ----
        ASTNodePtr parseAlter()
        {
            expect(TokenType::ALTER, "");
            expect(TokenType::TABLE, "Expected TABLE after ALTER");
            auto n = std::make_unique<AlterTableNode>();
            n->type = NodeType::ALTER_TABLE;
            auto [db, tbl] = parseTableRef();
            n->database = db;
            n->table = tbl;

            if (match(TokenType::ADD))
            {
                match(TokenType::COLUMN); // optional COLUMN keyword
                n->action = AlterAction::ADD_COLUMN;
                n->column = parseColumnDef();
            }
            else if (match(TokenType::MODIFY))
            {
                match(TokenType::COLUMN);
                n->action = AlterAction::MODIFY_COLUMN;
                n->column = parseColumnDef();
            }
            else if (match(TokenType::DROP))
            {
                match(TokenType::COLUMN);
                n->action = AlterAction::DROP_COLUMN;
                n->dropColName = parseIdent();
            }
            else
            {
                throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                                  "Expected ADD/MODIFY/DROP after ALTER TABLE");
            }
            return n;
        }

        // ---- INSERT ----
        ASTNodePtr parseInsert()
        {
            expect(TokenType::INSERT, "");
            expect(TokenType::INTO, "Expected INTO after INSERT");
            auto n = std::make_unique<InsertNode>();
            n->type = NodeType::INSERT;
            auto [db, tbl] = parseTableRef();
            n->database = db;
            n->table = tbl;
            // 可选列名列表
            if (match(TokenType::LPAREN))
            {
                n->columns = parseIdentList();
                expect(TokenType::RPAREN, "Expected ')'");
            }
            expect(TokenType::VALUES, "Expected VALUES");
            // 支持多行
            do
            {
                expect(TokenType::LPAREN, "Expected '('");
                std::vector<FieldValue> vals;
                if (!check(TokenType::RPAREN))
                {
                    vals.push_back(parseLiteral());
                    while (match(TokenType::COMMA))
                        vals.push_back(parseLiteral());
                }
                expect(TokenType::RPAREN, "Expected ')'");
                n->valueRows.push_back(std::move(vals));
            } while (match(TokenType::COMMA));
            return n;
        }

        // ---- SELECT ----
        ASTNodePtr parseSelect()
        {
            expect(TokenType::SELECT, "");
            auto n = std::make_unique<SelectNode>();
            n->type = NodeType::SELECT;
            n->distinct = match(TokenType::DISTINCT);
            n->columns = parseSelectColumns();
            expect(TokenType::FROM, "Expected FROM");
            auto [db, tbl] = parseTableRef();
            n->database = db;
            n->table = tbl;
            if (match(TokenType::AS))
                n->tableAlias = parseIdent();
            if (match(TokenType::WHERE))
                n->where = parseWhereExpr();
            if (check(TokenType::GROUP))
            {
                advance();
                expect(TokenType::BY, "Expected BY after GROUP");
                n->groupBy = parseIdentList();
            }
            if (match(TokenType::HAVING))
                n->having = parseWhereExpr();
            if (check(TokenType::ORDER))
            {
                advance();
                expect(TokenType::BY, "Expected BY after ORDER");
                do
                {
                    OrderByExpr ob;
                    ob.columnName = parseIdent();
                    if (match(TokenType::DESC))
                        ob.ascending = false;
                    else
                    {
                        match(TokenType::ASC);
                        ob.ascending = true;
                    }
                    n->orderBy.push_back(ob);
                } while (match(TokenType::COMMA));
            }
            if (match(TokenType::LIMIT))
                n->limit = std::stoi(expect(TokenType::INT_LITERAL, "Expected number").value);
            if (match(TokenType::OFFSET))
                n->offset = std::stoi(expect(TokenType::INT_LITERAL, "Expected number").value);
            return n;
        }

        // ---- UPDATE ----
        ASTNodePtr parseUpdate()
        {
            expect(TokenType::UPDATE, "");
            auto n = std::make_unique<UpdateNode>();
            n->type = NodeType::UPDATE;
            auto [db, tbl] = parseTableRef();
            n->database = db;
            n->table = tbl;
            expect(TokenType::SET, "Expected SET");
            do
            {
                UpdateAssignment a;
                a.columnName = parseIdent();
                expect(TokenType::EQ, "Expected '='");
                a.value = parseLiteral();
                n->assignments.push_back(a);
            } while (match(TokenType::COMMA));
            if (match(TokenType::WHERE))
                n->where = parseWhereExpr();
            return n;
        }

        // ---- DELETE ----
        ASTNodePtr parseDelete()
        {
            expect(TokenType::DELETE, "");
            expect(TokenType::FROM, "Expected FROM after DELETE");
            auto n = std::make_unique<DeleteNode>();
            n->type = NodeType::DELETE;
            auto [db, tbl] = parseTableRef();
            n->database = db;
            n->table = tbl;
            if (match(TokenType::WHERE))
                n->where = parseWhereExpr();
            return n;
        }

        // ---- BEGIN ----
        ASTNodePtr parseBegin()
        {
            expect(TokenType::BEGIN, "");
            match(TokenType::TRANSACTION);
            auto n = std::make_unique<BeginNode>();
            n->type = NodeType::BEGIN_TRANSACTION;
            return n;
        }

        // ---- CREATE USER ----
        ASTNodePtr parseCreateUser()
        {
            expect(TokenType::USER, "Expected USER after CREATE");
            auto n = std::make_unique<CreateUserNode>();
            n->type = NodeType::CREATE_USER;
            n->username = parseUsername();
            // IDENTIFIED BY 'password'
            if (match(TokenType::IDENTIFIED))
            {
                expect(TokenType::BY, "Expected BY after IDENTIFIED");
                n->password = parseLiteralStr();
            }
            else if (match(TokenType::PASSWORD))
            {
                n->password = parseLiteralStr();
            }
            return n;
        }

        // ---- DROP USER ----
        ASTNodePtr parseDropUser()
        {
            expect(TokenType::USER, "Expected USER after DROP");
            auto n = std::make_unique<DropUserNode>();
            n->type = NodeType::DROP_USER;
            n->username = parseUsername();
            return n;
        }

        // ---- 辅助：解析必须是字符串字面量或标识符的值 ----
        std::string parseLiteralStr()
        {
            if (check(TokenType::STRING_LITERAL))
                return advance().value;
            return parseIdent();
        }

        // ---- 解析权限列表（SELECT/INSERT/UPDATE/DELETE/ALL [PRIVILEGES]）----
        std::vector<Privilege> parsePrivilegeList()
        {
            std::vector<Privilege> privs;
            do
            {
                if (check(TokenType::ALL))
                {
                    advance();
                    match(TokenType::PRIVILEGES);
                    privs.push_back(Privilege::ALL);
                }
                else if (check(TokenType::SELECT))
                {
                    advance();
                    privs.push_back(Privilege::SELECT);
                }
                else if (check(TokenType::INSERT))
                {
                    advance();
                    privs.push_back(Privilege::INSERT);
                }
                else if (check(TokenType::UPDATE))
                {
                    advance();
                    privs.push_back(Privilege::UPDATE);
                }
                else if (check(TokenType::DELETE))
                {
                    advance();
                    privs.push_back(Privilege::DELETE);
                }
                else
                {
                    throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                                      "Expected privilege (SELECT/INSERT/UPDATE/DELETE/ALL), got '" + cur().value + "'");
                }
            } while (match(TokenType::COMMA));
            return privs;
        }

        // ---- 解析 ON db.table（允许 *.* 通配符）----
        std::pair<std::string, std::string> parseOnTarget()
        {
            // db or *
            std::string db, tbl;
            if (check(TokenType::STAR))
            {
                advance();
                db = "*";
            }
            else
            {
                db = parseIdent();
            }
            if (match(TokenType::DOT))
            {
                if (check(TokenType::STAR))
                {
                    advance();
                    tbl = "*";
                }
                else
                {
                    tbl = parseIdent();
                }
            }
            else
            {
                tbl = "*";
            }
            return {db, tbl};
        }

        // ---- GRANT ----
        ASTNodePtr parseGrant()
        {
            expect(TokenType::GRANT, "");
            auto n = std::make_unique<GrantNode>();
            n->type = NodeType::GRANT;
            n->privileges = parsePrivilegeList();
            expect(TokenType::ON, "Expected ON after privilege list");
            auto [db, tbl] = parseOnTarget();
            n->database = db;
            n->table = tbl;
            expect(TokenType::TO, "Expected TO after ON target");
            n->username = parseUsername();
            return n;
        }

        // ---- REVOKE ----
        ASTNodePtr parseRevoke()
        {
            expect(TokenType::REVOKE, "");
            auto n = std::make_unique<RevokeNode>();
            n->type = NodeType::REVOKE;
            n->privileges = parsePrivilegeList();
            expect(TokenType::ON, "Expected ON after privilege list");
            auto [db, tbl] = parseOnTarget();
            n->database = db;
            n->table = tbl;
            expect(TokenType::FROM, "Expected FROM after ON target");
            n->username = parseUsername();
            return n;
        }

        // ---- CONNECT ----
        ASTNodePtr parseConnect()
        {
            expect(TokenType::CONNECT, "");
            auto n = std::make_unique<ConnectNode>();
            n->type = NodeType::CONNECT;
            n->username = parseUsername();
            // IDENTIFIED BY 'password'
            if (match(TokenType::IDENTIFIED))
            {
                expect(TokenType::BY, "Expected BY after IDENTIFIED");
                n->password = parseLiteralStr();
            }
            else if (match(TokenType::PASSWORD))
            {
                n->password = parseLiteralStr();
            }
            return n;
        }

        // ---- BACKUP DATABASE <name> TO '<file>' ----
        ASTNodePtr parseBackup()
        {
            expect(TokenType::BACKUP, "");
            expect(TokenType::DATABASE, "Expected DATABASE after BACKUP");
            auto n = std::make_unique<BackupDatabaseNode>();
            n->type = NodeType::BACKUP_DATABASE;
            n->database = parseIdent();
            expect(TokenType::TO, "Expected TO after database name");
            n->filepath = expect(TokenType::STRING_LITERAL, "Expected file path string").value;
            return n;
        }

        // ---- RESTORE DATABASE <name> FROM '<file>' ----
        ASTNodePtr parseRestore()
        {
            expect(TokenType::RESTORE, "");
            expect(TokenType::DATABASE, "Expected DATABASE after RESTORE");
            auto n = std::make_unique<RestoreDatabaseNode>();
            n->type = NodeType::RESTORE_DATABASE;
            n->database = parseIdent();
            expect(TokenType::FROM, "Expected FROM after database name");
            n->filepath = expect(TokenType::STRING_LITERAL, "Expected file path string").value;
            return n;
        }
    };

} // anonymous namespace

// ============================================================
// Parser::parse
// ============================================================
ASTNodePtr Parser::parse(const std::vector<Token> &tokens)
{
    ParserImpl impl(tokens);
    auto node = impl.parseStatement();
    // 可选分号，忽略
    impl.match(TokenType::SEMICOLON);
    if (!impl.atEnd())
        throw DBException(ErrorCode::SQL_SYNTAX_ERROR,
                          "Unexpected token '" + impl.cur().value + "' after statement");
    return node;
}
