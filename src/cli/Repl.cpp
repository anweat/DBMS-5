#include "Repl.h"
#include "Formatter.h"
#include "MetaCommands.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

Repl::Repl(DBEngine& engine, CLISession& session)
    : engine_(engine), session_(session) {}

std::string Repl::prompt() const {
    const auto& db = session_.engineSession.currentDatabase;
    const auto& tx = session_.engineSession.transactionId;
    if (db.empty()) return "dbms> ";
    // 活跃事务时显示 * 后缀
    return "dbms [" + db + (tx.empty() ? "" : "*") + "]> ";
}

void Repl::handleInput(const std::string& raw) {
    // 去掉前后空白
    std::string sql = raw;
    size_t f = sql.find_first_not_of(" \t\r\n");
    if (f == std::string::npos) return;
    sql = sql.substr(f);
    size_t l = sql.find_last_not_of(" \t\r\n");
    if (l != std::string::npos) sql = sql.substr(0, l + 1);
    if (sql.empty()) return;

    // 处理 source <file> 命令（不以 \ 开头，优先检测）
    {
        std::string lower = sql;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (lower.substr(0, 7) == "source ") {
            std::string filepath = sql.substr(7);
            size_t fp = filepath.find_first_not_of(" \t\"'");
            size_t lp = filepath.find_last_not_of(" \t\"'");
            if (fp != std::string::npos) filepath = filepath.substr(fp, lp - fp + 1);
            runFile(filepath);
            return;
        }
    }

    Formatter fmt;
    MetaCommandHandler meta;
    auto mr = meta.handle(sql, session_);
    if (mr.handled) {
        if (!mr.output.empty()) std::cout << mr.output;
        return;
    }

    // meta 返回 false：mr.output 有内容时为别名 SQL，否则用原始输入
    const std::string& execSql = mr.output.empty() ? sql : mr.output;
    auto result = engine_.execute(execSql, session_.engineSession);
    fmt.formatPaged(result, session_.pageSize);
}

void Repl::run() {
    std::string line, buf;
    while (true) {
        std::cout << (buf.empty() ? prompt() : "    -> ");
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;

        // 去掉首尾空白，检查是否为元命令或 source 命令（立即执行，不需要 ;）
        std::string trimmed = line;
        {
            size_t f = trimmed.find_first_not_of(" \t\r\n");
            if (f == std::string::npos) {
                if (buf.empty()) continue;  // 空行且无缓冲时跳过
                trimmed.clear();
            } else {
                size_t l = trimmed.find_last_not_of(" \t\r\n");
                trimmed = trimmed.substr(f, l - f + 1);
            }
        }

        // 元命令（\ 开头）或 source 命令：立即调度，不进缓冲区
        bool isImmediate = false;
        if (!trimmed.empty() && trimmed[0] == '\\') {
            isImmediate = true;
        } else if (trimmed.size() >= 7) {
            std::string low = trimmed.substr(0, 7);
            for (auto& c : low) c = (char)std::tolower((unsigned char)c);
            if (low == "source ") isImmediate = true;
        }

        if (isImmediate) {
            if (!buf.empty()) {
                // 先提交已有缓冲（不完整 SQL，直接放弃，提示用户）
                std::cerr << Formatter::yellow("Warning: incomplete SQL statement discarded.\n");
                buf.clear();
            }
            session_.addHistory(trimmed);
            handleInput(trimmed);
            continue;
        }

        buf += line + '\n';

        // 检测语句结束的 ;
        std::string stripped = buf;
        size_t last = stripped.find_last_not_of(" \t\r\n");
        if (last != std::string::npos && stripped[last] == ';') {
            std::string stmt = stripped.substr(0, last);
            size_t s = stmt.find_first_not_of(" \t\r\n");
            if (s != std::string::npos) stmt = stmt.substr(s);

            if (!stmt.empty()) {
                session_.addHistory(stmt);
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
            session_.addHistory(stmt);
            handleInput(stmt);
        }
    }
}

void Repl::runFile(const std::string& filePath) {
    std::ifstream f(filePath);
    if (!f) { std::cerr << "Cannot open: " << filePath << "\n"; return; }
    std::string sql, line;
    while (std::getline(f, line)) {
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
    // 文件末尾无分号的语句
    if (!sql.empty()) {
        size_t s = sql.find_first_not_of(" \t\r\n");
        if (s != std::string::npos) {
            sql = sql.substr(s);
            size_t e = sql.find_last_not_of(" \t\r\n");
            if (e != std::string::npos) sql = sql.substr(0, e + 1);
            if (!sql.empty()) handleInput(sql);
        }
    }
}
