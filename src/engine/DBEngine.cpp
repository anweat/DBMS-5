#include "engine/DBEngine.h"
#include "engine/lexer/Lexer.h"
#include "engine/parser/Parser.h"
#include "engine/executor/Executor.h"
#include "engine/parser/AST.h"

#include <chrono>
#include <fstream>

struct DBEngine::Impl {
    Lexer    lexer;
    Parser   parser;
    Executor executor;

    explicit Impl(const std::string& dataDir) : executor(dataDir) {}
};

DBEngine::DBEngine(const std::string& dataDir)
    : dataDir_(dataDir), impl_(new Impl(dataDir))
{
    // 引擎启动：DatabaseManager 初始化由 Executor 内部完成
}

DBEngine::~DBEngine() {
    delete impl_;
}

// 从 SQL 文件逐条执行（RESTORE DATABASE 内部实现）
static QueryResult executeFileStmts(DBEngine& engine, const std::string& filepath, Session& session) {
    std::ifstream f(filepath);
    if (!f)
        return QueryResult::err(ErrorCode::FILE_IO_ERROR, "Cannot open file: " + filepath);

    int count = 0;
    std::string sql, line;
    while (std::getline(f, line)) {
        sql += line + '\n';
        size_t last = sql.find_last_not_of(" \t\r\n");
        if (last != std::string::npos && sql[last] == ';') {
            std::string stmt = sql.substr(0, last);
            size_t s = stmt.find_first_not_of(" \t\r\n");
            if (s != std::string::npos) stmt = stmt.substr(s);
            if (!stmt.empty()) {
                auto r = engine.execute(stmt, session);
                if (r.error)
                    return QueryResult::err(r.error->code,
                        "Restore failed: " + r.error->message
                        + "\nStatement: " + stmt.substr(0, 80));
                ++count;
            }
            sql.clear();
        }
    }
    return QueryResult::ok("Restore completed: " + std::to_string(count)
                           + " statement(s) from '" + filepath + "'.");
}

QueryResult DBEngine::execute(const std::string& sql, Session& session) {
    auto t0 = std::chrono::steady_clock::now();
    try {
        auto tokens = impl_->lexer.tokenize(sql);
        auto ast    = impl_->parser.parse(tokens);

        // RESTORE DATABASE 需要递归调用 execute()，在此层处理
        if (ast->type == NodeType::RESTORE_DATABASE) {
            const auto& rn = static_cast<const RestoreDatabaseNode&>(*ast);
            if (!rn.database.empty())
                session.currentDatabase = rn.database;
            auto result = executeFileStmts(*this, rn.filepath, session);
            auto t1 = std::chrono::steady_clock::now();
            result.elapsedMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            return result;
        }

        auto result = impl_->executor.execute(*ast, session);
        auto t1     = std::chrono::steady_clock::now();
        result.elapsedMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        return result;
    } catch (const DBException& e) {
        return QueryResult::err(e.error().code, e.error().message);
    } catch (const std::exception& e) {
        return QueryResult::err(ErrorCode::UNKNOWN_ERROR, e.what());
    }
}

void DBEngine::beginTransaction(Session& session) {
    execute("BEGIN", session);
}

void DBEngine::commit(Session& session) {
    execute("COMMIT", session);
}

void DBEngine::rollback(Session& session) {
    execute("ROLLBACK", session);
}
