#include "MetaCommands.h"
#include "Formatter.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

MetaResult MetaCommandHandler::handle(const std::string& input, CLISession& session) {
    std::string s = trim(input);
    if (s.empty() || s[0] != '\\') return {false, ""};

    // 取命令词（去掉前导 \，小写）
    std::string cmd;
    std::string args;
    auto sp = s.find_first_of(" \t", 1);
    if (sp == std::string::npos) {
        cmd = s.substr(1);
    } else {
        cmd  = s.substr(1, sp - 1);
        args = trim(s.substr(sp + 1));
    }
    std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    if (cmd == "help" || cmd == "h" || cmd == "?") {
        return {true,
            "Available commands:\n"
            "  \\help              Show this help\n"
            "  \\quit / \\q        Exit the DBMS\n"
            "  \\status            Show session status\n"
            "  \\clear             Clear screen\n"
            "  \\history           Show command history\n"
            "  \\use <db>          Switch database  (alias: USE <db>)\n"
            "  \\desc <table>      Describe table   (alias: DESCRIBE <table>)\n"
            "  \\databases         List databases   (alias: SHOW DATABASES)\n"
            "  \\tables            List tables      (alias: SHOW TABLES)\n"
            "  \\connect <u> <p>   Authenticate     (alias: CONNECT ... IDENTIFIED BY ...)\n"
            "  source <file>      Execute SQL file\n"
        };
    }

    if (cmd == "quit" || cmd == "q" || cmd == "exit") {
        std::cout << Formatter::green("Bye!") << "\n";
        std::exit(0);
    }

    if (cmd == "status") {
        const auto& es = session.engineSession;
        std::string db   = es.currentDatabase.empty() ? "(none)"      : es.currentDatabase;
        std::string user = es.user.empty()             ? "(anonymous)" : es.user;
        std::string tx   = es.transactionId.empty()    ? "none"        : "active (" + es.transactionId + ")";
        return {true,
            "User       : " + user + "\n"
            "Database   : " + db   + "\n"
            "Transaction: " + tx   + "\n"
            "Page size  : " + std::to_string(session.pageSize) + "\n"
        };
    }

    if (cmd == "clear") {
#ifdef DBMS_WINDOWS
        std::system("cls");
#else
        std::cout << "\033[2J\033[H";
#endif
        return {true, ""};
    }

    if (cmd == "history") {
        std::string out;
        for (size_t i = 0; i < session.history.size(); ++i)
            out += std::to_string(i + 1) + "\t" + session.history[i] + "\n";
        if (out.empty()) out = "(empty history)\n";
        return {true, out};
    }

    // \use <db>  →  forward as SQL: USE <db>
    if (cmd == "use") {
        if (args.empty())
            return {true, Formatter::yellow("Usage: \\use <database>\n")};
        return {false, "USE " + args};
    }

    // \desc <table>  →  forward as SQL: DESCRIBE <table>
    if (cmd == "desc" || cmd == "describe") {
        if (args.empty())
            return {true, Formatter::yellow("Usage: \\desc <table>\n")};
        return {false, "DESCRIBE " + args};
    }

    // \connect <user> <password>  →  CONNECT 'user' IDENTIFIED BY 'pass'
    if (cmd == "connect") {
        std::istringstream iss(args);
        std::string user, pass;
        iss >> user >> pass;
        if (user.empty())
            return {true, Formatter::yellow("Usage: \\connect <user> <password>\n")};
        return {false, "CONNECT '" + user + "' IDENTIFIED BY '" + pass + "'"};
    }

    if (cmd == "databases") return {false, "SHOW DATABASES"};
    if (cmd == "tables")    return {false, "SHOW TABLES"};

    // 未知元命令
    return {true, Formatter::yellow("Unknown meta-command: \\" + cmd
                                    + "  (type \\help for help)\n")};
}
