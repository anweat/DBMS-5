#include "Formatter.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <variant>

#ifdef DBMS_WINDOWS
#include <windows.h>
#endif

void Formatter::enableAnsi()
{
#ifdef DBMS_WINDOWS
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

std::string Formatter::red(const std::string &s) { return "\033[31m" + s + "\033[0m"; }
std::string Formatter::green(const std::string &s) { return "\033[32m" + s + "\033[0m"; }
std::string Formatter::yellow(const std::string &s) { return "\033[33m" + s + "\033[0m"; }
std::string Formatter::bold(const std::string &s) { return "\033[1m" + s + "\033[0m"; }

// 将 FieldValue 转为显示字符串
static std::string fvStr(const FieldValue &v)
{
    if (std::holds_alternative<std::monostate>(v))
        return "NULL";
    if (std::holds_alternative<int64_t>(v))
        return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<bool>(v))
        return std::get<bool>(v) ? "1" : "0";
    if (std::holds_alternative<std::string>(v))
        return std::get<std::string>(v);
    if (std::holds_alternative<double>(v))
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << std::get<double>(v);
        // 去掉末尾多余的 0
        std::string s = oss.str();
        auto dot = s.find('.');
        if (dot != std::string::npos)
        {
            size_t last = s.find_last_not_of('0');
            if (last != std::string::npos && last > dot)
                s = s.substr(0, last + 1);
            else if (last == dot)
                s = s.substr(0, dot);
        }
        return s;
    }
    return "";
}

// MySQL 风格表格渲染
static std::string renderTable(const QueryResult &result)
{
    if (result.columns.empty())
        return "";

    size_t nCols = result.columns.size();
    std::vector<size_t> widths(nCols);

    // 初始化：列名宽度
    for (size_t j = 0; j < nCols; ++j)
        widths[j] = result.columns[j].name.size();

    // 数据宽度
    for (const auto &row : result.rows)
        for (size_t j = 0; j < row.size() && j < nCols; ++j)
            widths[j] = std::max(widths[j], fvStr(row[j]).size());

    // 构建分隔行
    std::string sep = "+";
    for (size_t j = 0; j < nCols; ++j)
        sep += std::string(widths[j] + 2, '-') + "+";
    sep += "\n";

    std::string out = sep;
    // 表头
    out += "|";
    for (size_t j = 0; j < nCols; ++j)
    {
        out += " ";
        out += result.columns[j].name;
        out += std::string(widths[j] - result.columns[j].name.size(), ' ');
        out += " |";
    }
    out += "\n" + sep;

    // 数据行
    for (const auto &row : result.rows)
    {
        out += "|";
        for (size_t j = 0; j < nCols; ++j)
        {
            std::string cell = (j < row.size()) ? fvStr(row[j]) : "";
            out += " " + cell + std::string(widths[j] - cell.size(), ' ') + " |";
        }
        out += "\n";
    }
    out += sep;
    out += std::to_string(result.rows.size()) + " row(s) in set";
    if (result.elapsedMs > 0)
        out += " (" + std::to_string(result.elapsedMs) + " ms)";
    out += "\n";
    return out;
}

std::string Formatter::format(const QueryResult &result)
{
    if (result.error)
        return red("ERROR: " + result.error->message) + "\n";

    switch (result.type)
    {
    case QueryResult::Type::SELECT:
        if (result.columns.empty())
            return green("Empty set\n");
        return renderTable(result);

    case QueryResult::Type::DML:
    {
        std::string msg = green("Query OK");
        if (result.affectedRows >= 0)
            msg += ", " + std::to_string(result.affectedRows) + " row(s) affected";
        if (result.insertId >= 0)
            msg += " (last insert id: " + std::to_string(result.insertId) + ")";
        if (result.elapsedMs > 0)
            msg += " (" + std::to_string(result.elapsedMs) + " ms)";
        return msg + "\n";
    }

    case QueryResult::Type::DDL:
        return green("Query OK") +
               (result.message.empty() ? "" : " -- " + result.message) +
               (result.elapsedMs > 0 ? " (" + std::to_string(result.elapsedMs) + " ms)" : "") +
               "\n";

    default:
        return result.message + "\n";
    }
}

void Formatter::formatPaged(const QueryResult &result, int pageSize)
{
    if (result.type != QueryResult::Type::SELECT || result.rows.empty())
    {
        std::cout << format(result);
        return;
    }

    // 逐批分页输出
    size_t nCols = result.columns.size();
    std::vector<size_t> widths(nCols);
    for (size_t j = 0; j < nCols; ++j)
        widths[j] = result.columns[j].name.size();
    for (const auto &row : result.rows)
        for (size_t j = 0; j < row.size() && j < nCols; ++j)
            widths[j] = std::max(widths[j], fvStr(row[j]).size());

    std::string sep = "+";
    for (size_t j = 0; j < nCols; ++j)
        sep += std::string(widths[j] + 2, '-') + "+";
    sep += "\n";

    // 表头
    std::string header = sep;
    header += "|";
    for (size_t j = 0; j < nCols; ++j)
    {
        header += " " + result.columns[j].name + std::string(widths[j] - result.columns[j].name.size(), ' ') + " |";
    }
    header += "\n" + sep;

    std::cout << header;
    int lineCount = 2; // header lines

    for (size_t i = 0; i < result.rows.size(); ++i)
    {
        const auto &row = result.rows[i];
        std::string line = "|";
        for (size_t j = 0; j < nCols; ++j)
        {
            std::string cell = (j < row.size()) ? fvStr(row[j]) : "";
            line += " " + cell + std::string(widths[j] - cell.size(), ' ') + " |";
        }
        std::cout << line << "\n";
        ++lineCount;

        // 分页暂停
        if (pageSize > 0 && lineCount >= pageSize && i + 1 < result.rows.size())
        {
            std::cout << "-- More -- (Press Enter to continue)";
            std::string dummy;
            std::getline(std::cin, dummy);
            lineCount = 0;
            std::cout << header;
            lineCount = 2;
        }
    }
    std::cout << sep;
    std::cout << result.rows.size() << " row(s) in set";
    if (result.elapsedMs > 0)
        std::cout << " (" << result.elapsedMs << " ms)";
    std::cout << "\n";
}
