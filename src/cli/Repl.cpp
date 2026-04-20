#include "Repl.h"
#include "Formatter.h"
#include "MetaCommands.h"
#include <iostream>
#include <fstream>
#include <sstream>

Repl::Repl(DBEngine& engine, CLISession& session)
    : engine_(engine), session_(session) {}

std::string Repl::prompt() const {
    const auto& db = session_.engineSession.currentDatabase;
    return db.empty() ? "dbms> " : "dbms [" + db + "]> ";
}

void Repl::handleInput(const std::string& sql) {
    Formatter fmt;
    MetaCommandHandler meta;
    auto mr = meta.handle(sql, session_);
    if (mr.handled) {
        std::cout << mr.output;
    } else {
        // meta 返回 false 但有 output 时，是别名 SQL（如 \databases → SHOW DATABASES）
        const std::string& execSql = mr.output.empty() ? sql : mr.output;
        auto result = engine_.execute(execSql, session_.engineSession);
        fmt.formatPaged(result, session_.pageSize);
    }
}

void Repl::run() {
    std::string line, buf;
    while (true) {
        // 续行时用 -> 提示
        std::cout << (buf.empty() ? prompt() : "    -> ");
        if (!std::getline(std::cin, line)) break;

        // 跳过纯空行（不影响注释处理）
        std::string trimmed = line;
        while (!trimmed.empty() && std::isspace((unsigned char)trimmed.front()))
            trimmed.erase(trimmed.begin());

        if (trimmed.empty() && buf.empty()) continue;

        // 保留换行符以便词法器正确终止 -- 注释
        buf += line + '\n';

        // 查找语句结束的 ; （简单检测，不处理字符串内的 ;）
        // 去掉末尾空白后检查是否以 ; 结尾
        std::string stripped = buf;
        size_t last = stripped.find_last_not_of(" \t\r\n");
        if (last != std::string::npos && stripped[last] == ';') {
            stripped = stripped.substr(0, last); // 去掉结尾 ;
            std::string stmt = stripped;
            // 去除首尾空白
            size_t s = stmt.find_first_not_of(" \t\r\n");
            if (s != std::string::npos) stmt = stmt.substr(s);

            if (!stmt.empty()) {
                session_.history.push_back(stmt);
                handleInput(stmt);
            }
            buf.clear();
        }
    }
    // 处理无结尾 ; 的最后一条语句
    std::string stmt = buf;
    size_t s = stmt.find_first_not_of(" \t\r\n");
    if (s != std::string::npos) {
        stmt = stmt.substr(s);
        size_t e = stmt.find_last_not_of(" \t\r\n");
        if (e != std::string::npos) stmt = stmt.substr(0, e + 1);
        if (!stmt.empty()) {
            session_.history.push_back(stmt);
            handleInput(stmt);
        }
    }
}

void Repl::runFile(const std::string& filePath) {
    std::ifstream f(filePath);
    if (!f) { std::cerr << "Cannot open: " << filePath << "\n"; return; }
    std::string sql, line;
    while (std::getline(f, line)) {
        // 保留换行，以便 -- 注释正确终止
        sql += line + '\n';
        std::string stripped = sql;
        size_t last = stripped.find_last_not_of(" \t\r\n");
        if (last != std::string::npos && stripped[last] == ';') {
            std::string stmt = stripped.substr(0, last);
            size_t s = stmt.find_first_not_of(" \t\r\n");
            if (s != std::string::npos) stmt = stmt.substr(s);
            if (!stmt.empty()) handleInput(stmt);
            sql.clear();
        }
    }
}
