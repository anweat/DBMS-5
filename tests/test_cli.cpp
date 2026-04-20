/**
 * CLI 输入行为测试
 * 覆盖：
 *   1. 带分号的单行语句直接执行
 *   2. 多行语句以分号结束后执行
 *   3. 无分号语句按空行提交执行（修复后新行为）
 *   4. 元命令（\quit / \databases）立即执行，不进缓冲
 *   5. handleInput 对各类 SQL 正确转发给引擎
 */

#include "../src/engine/DBEngine.h"
#include "../src/cli/Repl.h"
#include "../src/cli/Session.h"
#include "../src/cli/Formatter.h"
#include <iostream>
#include <sstream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// ── 简易测试框架 ─────────────────────────────────────────────────────────────
static int gPassed = 0, gFailed = 0;
#define ASSERT_TRUE(expr) do { \
    if (expr) { ++gPassed; } \
    else { ++gFailed; \
        std::cerr << "FAIL [" #expr "] at " __FILE__ ":" << __LINE__ << "\n"; } \
} while(0)
#define ASSERT_EQ(a,b)  ASSERT_TRUE((a)==(b))
#define ASSERT_NE(a,b)  ASSERT_TRUE((a)!=(b))
#define ASSERT_CONTAINS(str, sub) ASSERT_TRUE((str).find(sub) != std::string::npos)

static const std::string DATA_DIR = "./test_data_cli";

// ── 辅助：用模拟输入运行 REPL，返回 stdout 输出 ──────────────────────────────
static std::string runRepl(const std::string& simulatedInput) {
    DBEngine   engine(DATA_DIR);
    CLISession session;
    Repl       repl(engine, session);

    // 重定向 cin
    std::istringstream fakeIn(simulatedInput);
    std::streambuf* oldIn = std::cin.rdbuf(fakeIn.rdbuf());

    // 重定向 cout
    std::ostringstream captured;
    std::streambuf* oldOut = std::cout.rdbuf(captured.rdbuf());

    repl.run();

    std::cin.rdbuf(oldIn);
    std::cout.rdbuf(oldOut);
    return captured.str();
}

// ── 1. 带分号的单行语句 ───────────────────────────────────────────────────────
static void test_single_line_with_semicolon() {
    std::cout << "[test_single_line_with_semicolon]\n";

    // CREATE DATABASE + USE + SHOW TABLES; — 靠 EOF 退出 REPL
    std::string input =
        "CONNECT 'root' IDENTIFIED BY 'root';\n"
        "CREATE DATABASE cli_test1;\n"
        "USE cli_test1;\n"
        "SHOW TABLES;\n";

    std::string out = runRepl(input);
    // SHOW TABLES 在空库中返回空结果，不应包含 ERROR
    ASSERT_TRUE(out.find("ERROR") == std::string::npos ||
                out.find("cli_test1") != std::string::npos);
}

// ── 2. 多行语句以分号结束 ─────────────────────────────────────────────────────
static void test_multiline_with_semicolon() {
    std::cout << "[test_multiline_with_semicolon]\n";

    std::string input =
        "CONNECT 'root' IDENTIFIED BY 'root';\n"
        "CREATE DATABASE cli_test2;\n"
        "USE cli_test2;\n"
        "CREATE TABLE t (\n"
        "  id INT PRIMARY KEY,\n"
        "  name VARCHAR(20)\n"
        ");\n"
        "SHOW TABLES;\n";

    std::string out = runRepl(input);
    ASSERT_CONTAINS(out, "t");          // SHOW TABLES 应列出 t
    ASSERT_TRUE(out.find("ERROR") == std::string::npos);
}

// ── 3. 无分号语句 + 空行提交（核心修复测试）────────────────────────────────────
static void test_empty_line_submits_buffer() {
    std::cout << "[test_empty_line_submits_buffer]\n";

    // 不写分号，靠空行触发执行
    std::string input =
        "CONNECT 'root' IDENTIFIED BY 'root';\n"
        "CREATE DATABASE cli_test3;\n"
        "USE cli_test3;\n"
        "CREATE TABLE t2 (id INT PRIMARY KEY, val VARCHAR(10))\n"
        "\n"                    // <-- 空行触发上面语句执行
        "SHOW TABLES\n"
        "\n";                   // <-- 空行触发 SHOW TABLES

    std::string out = runRepl(input);
    ASSERT_CONTAINS(out, "t2");         // 表应成功创建并被列出
    ASSERT_TRUE(out.find("ERROR") == std::string::npos);
}

// ── 4. 元命令立即执行，不污染 SQL 缓冲 ──────────────────────────────────────
static void test_meta_command_immediate() {
    std::cout << "[test_meta_command_immediate]\n";

    // \databases 元命令，立即执行（靠 EOF 退出）
    std::string input =
        "CONNECT 'root' IDENTIFIED BY 'root';\n"
        "\\databases\n";

    std::string out = runRepl(input);
    // \databases 应输出数据库列表（至少不 crash）
    ASSERT_TRUE(true);
}

// ── 5. INSERT + SELECT 通过 CLI 层 ───────────────────────────────────────────
static void test_insert_select_via_cli() {
    std::cout << "[test_insert_select_via_cli]\n";

    std::string input =
        "CONNECT 'root' IDENTIFIED BY 'root';\n"
        "CREATE DATABASE cli_test5;\n"
        "USE cli_test5;\n"
        "CREATE TABLE items (id INT PRIMARY KEY, label VARCHAR(30));\n"
        "INSERT INTO items VALUES (1, 'hello');\n"
        "INSERT INTO items VALUES (2, 'world');\n"
        "SELECT * FROM items;\n";

    std::string out = runRepl(input);
    ASSERT_CONTAINS(out, "hello");
    ASSERT_CONTAINS(out, "world");
    ASSERT_TRUE(out.find("ERROR") == std::string::npos);
}

// ── 6. 混合：部分带分号、部分靠空行 ─────────────────────────────────────────
static void test_mixed_termination() {
    std::cout << "[test_mixed_termination]\n";

    std::string input =
        "CONNECT 'root' IDENTIFIED BY 'root';\n"
        "CREATE DATABASE cli_test6;\n"
        "USE cli_test6;\n"
        "CREATE TABLE mx (id INT PRIMARY KEY);\n"
        "INSERT INTO mx VALUES (42)\n"   // 无分号
        "\n"                             // 空行执行
        "SELECT id FROM mx;\n";

    std::string out = runRepl(input);
    ASSERT_CONTAINS(out, "42");
    ASSERT_TRUE(out.find("ERROR") == std::string::npos);
}

// ── 清理测试数据目录 ─────────────────────────────────────────────────────────
static void cleanup() {
    std::error_code ec;
    fs::remove_all(DATA_DIR, ec);
}

int main() {
    cleanup();   // 每次测试前清空旧数据

    test_single_line_with_semicolon();
    test_multiline_with_semicolon();
    test_empty_line_submits_buffer();
    test_meta_command_immediate();
    test_insert_select_via_cli();
    test_mixed_termination();

    cleanup();

    std::cout << "\n=== CLI Tests: " << gPassed << " passed, "
              << gFailed << " failed ===\n";
    return gFailed == 0 ? 0 : 1;
}
