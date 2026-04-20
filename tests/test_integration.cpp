/**
 * 综合集成测试
 * 覆盖：DDL / DML / WHERE / ORDER BY / LIMIT / GROUP BY / HAVING /
 *       DISTINCT / 聚合函数 / 完整性约束 / 索引 / 外键约束
 */

#include "../src/engine/DBEngine.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <cassert>
#include <stdexcept>

namespace fs = std::filesystem;

// ── 简易测试框架 ─────────────────────────────────────────────────────────────
static int gPassed = 0, gFailed = 0;
#define ASSERT_TRUE(expr) do { \
    if (expr) { ++gPassed; } \
    else { ++gFailed; \
        std::cerr << "FAIL [" #expr "] at " __FILE__ ":" << __LINE__ << "\n"; } \
} while(0)
#define ASSERT_EQ(a,b) ASSERT_TRUE((a)==(b))
#define ASSERT_NE(a,b) ASSERT_TRUE((a)!=(b))

// ── 全局引擎 ─────────────────────────────────────────────────────────────────
static const std::string DATA_DIR = "./test_data_integ";
static DBEngine* gEng = nullptr;
static Session   gSess;

// 执行 SQL，断言不出错
static QueryResult exec(const std::string& sql) {
    auto r = gEng->execute(sql, gSess);
    if (r.type == QueryResult::Type::ERROR) {
        std::cerr << "  SQL Error: " << r.message << "\n  SQL: " << sql << "\n";
    }
    return r;
}

// 执行 SQL，期望出错（约束等）
static QueryResult execExpectError(const std::string& sql) {
    auto r = gEng->execute(sql, gSess);
    ASSERT_TRUE(r.type == QueryResult::Type::ERROR);
    return r;
}

// ── 辅助：把 FieldValue 转字符串 ─────────────────────────────────────────────
static std::string fvs(const FieldValue& v) {
    if (std::holds_alternative<std::monostate>(v)) return "NULL";
    if (std::holds_alternative<int64_t>(v))  return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))   return std::to_string(std::get<double>(v));
    if (std::holds_alternative<bool>(v))     return std::get<bool>(v) ? "true" : "false";
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    return "?";
}

// ── 1. 基础 DDL ───────────────────────────────────────────────────────────────
static void test_ddl() {
    std::cout << "[test_ddl]\n";
    auto r = exec("CREATE DATABASE testdb");
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);

    exec("USE testdb");
    ASSERT_EQ(gSess.currentDatabase, "testdb");

    r = exec("CREATE TABLE employees ("
             "  id INT PRIMARY KEY AUTO_INCREMENT,"
             "  name VARCHAR(50) NOT NULL,"
             "  dept VARCHAR(30),"
             "  salary DOUBLE DEFAULT 0.0"
             ")");
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);

    r = exec("SHOW TABLES");
    ASSERT_EQ(r.type, QueryResult::Type::SELECT);
    ASSERT_EQ(r.rows.size(), (size_t)1);

    r = exec("DESCRIBE employees");
    ASSERT_EQ(r.type, QueryResult::Type::SELECT);
    ASSERT_EQ(r.rows.size(), (size_t)4);   // 4 columns
}

// ── 2. 基础 DML + AUTO_INCREMENT ─────────────────────────────────────────────
static void test_dml_basic() {
    std::cout << "[test_dml_basic]\n";
    auto r = exec("INSERT INTO employees (name, dept, salary) VALUES ('Alice', 'Engineering', 9000)");
    ASSERT_EQ(r.affectedRows, 1);
    ASSERT_EQ(r.insertId, (int64_t)1);

    exec("INSERT INTO employees (name, dept, salary) VALUES ('Bob', 'Marketing', 6000)");
    exec("INSERT INTO employees (name, dept, salary) VALUES ('Carol', 'Engineering', 8500)");
    exec("INSERT INTO employees (name, dept, salary) VALUES ('Dave', 'Marketing', 7000)");
    exec("INSERT INTO employees (name, dept, salary) VALUES ('Eve', 'Engineering', 9500)");

    r = exec("SELECT * FROM employees");
    ASSERT_EQ(r.type, QueryResult::Type::SELECT);
    ASSERT_EQ(r.rows.size(), (size_t)5);
}

// ── 3. WHERE 条件 ─────────────────────────────────────────────────────────────
static void test_where() {
    std::cout << "[test_where]\n";
    auto r = exec("SELECT name FROM employees WHERE dept = 'Engineering'");
    ASSERT_EQ(r.rows.size(), (size_t)3);

    r = exec("SELECT name FROM employees WHERE salary >= 8500 AND dept = 'Engineering'");
    ASSERT_EQ(r.rows.size(), (size_t)3);  // Alice(9000), Carol(8500), Eve(9500)

    r = exec("SELECT name FROM employees WHERE salary BETWEEN 7000 AND 9000");
    ASSERT_EQ(r.rows.size(), (size_t)3);  // Alice(9000), Carol(8500), Dave(7000)

    r = exec("SELECT name FROM employees WHERE dept IN ('Marketing', 'HR')");
    ASSERT_EQ(r.rows.size(), (size_t)2);  // Bob, Dave

    r = exec("SELECT name FROM employees WHERE name LIKE 'A%'");
    ASSERT_EQ(r.rows.size(), (size_t)1);

    r = exec("SELECT name FROM employees WHERE dept IS NOT NULL");
    ASSERT_EQ(r.rows.size(), (size_t)5);
}

// ── 4. ORDER BY + LIMIT + OFFSET ─────────────────────────────────────────────
static void test_order_limit() {
    std::cout << "[test_order_limit]\n";
    auto r = exec("SELECT name, salary FROM employees ORDER BY salary DESC LIMIT 3");
    ASSERT_EQ(r.rows.size(), (size_t)3);
    // 最高薪: Eve(9500), Alice(9000), Carol(8500)
    ASSERT_EQ(fvs(r.rows[0][0]), "Eve");
    ASSERT_EQ(fvs(r.rows[1][0]), "Alice");

    r = exec("SELECT name FROM employees ORDER BY name ASC LIMIT 2 OFFSET 2");
    ASSERT_EQ(r.rows.size(), (size_t)2);
    // 排序后: Alice Bob Carol Dave Eve → OFFSET 2: Carol, Dave
    ASSERT_EQ(fvs(r.rows[0][0]), "Carol");
    ASSERT_EQ(fvs(r.rows[1][0]), "Dave");
}

// ── 5. 聚合函数（无 GROUP BY） ────────────────────────────────────────────────
static void test_aggregates_global() {
    std::cout << "[test_aggregates_global]\n";
    auto r = exec("SELECT COUNT(*) FROM employees");
    ASSERT_EQ(r.rows.size(), (size_t)1);
    ASSERT_EQ(fvs(r.rows[0][0]), "5");

    r = exec("SELECT SUM(salary), AVG(salary), MAX(salary), MIN(salary) FROM employees");
    ASSERT_EQ(r.rows.size(), (size_t)1);
    ASSERT_EQ(fvs(r.rows[0][0]), "40000.000000");  // sum
    ASSERT_EQ(fvs(r.rows[0][2]), "9500.000000");    // max
    ASSERT_EQ(fvs(r.rows[0][3]), "6000.000000");    // min
}

// ── 6. GROUP BY + HAVING ──────────────────────────────────────────────────────
static void test_group_by() {
    std::cout << "[test_group_by]\n";
    auto r = exec(
        "SELECT dept, COUNT(*), AVG(salary) FROM employees "
        "GROUP BY dept ORDER BY dept ASC");
    ASSERT_EQ(r.rows.size(), (size_t)2);
    // Engineering: 3 rows, Marketing: 2 rows (sorted by dept)
    ASSERT_EQ(fvs(r.rows[0][0]), "Engineering");
    ASSERT_EQ(fvs(r.rows[0][1]), "3");
    ASSERT_EQ(fvs(r.rows[1][0]), "Marketing");
    ASSERT_EQ(fvs(r.rows[1][1]), "2");

    r = exec(
        "SELECT dept, MAX(salary) FROM employees "
        "GROUP BY dept HAVING MAX(salary) > 8000");
    ASSERT_EQ(r.rows.size(), (size_t)1);  // only Engineering (max=9500)
    if (r.rows.size() == 1)
        ASSERT_EQ(fvs(r.rows[0][0]), "Engineering");
}

// ── 7. DISTINCT ───────────────────────────────────────────────────────────────
static void test_distinct() {
    std::cout << "[test_distinct]\n";
    auto r = exec("SELECT DISTINCT dept FROM employees ORDER BY dept ASC");
    ASSERT_EQ(r.rows.size(), (size_t)2);
    ASSERT_EQ(fvs(r.rows[0][0]), "Engineering");
    ASSERT_EQ(fvs(r.rows[1][0]), "Marketing");
}

// ── 8. UPDATE ─────────────────────────────────────────────────────────────────
static void test_update() {
    std::cout << "[test_update]\n";
    auto r = exec("UPDATE employees SET salary = 10000 WHERE name = 'Eve'");
    ASSERT_EQ(r.affectedRows, 1);

    r = exec("SELECT salary FROM employees WHERE name = 'Eve'");
    ASSERT_EQ(fvs(r.rows[0][0]), "10000.000000");
}

// ── 9. DELETE ─────────────────────────────────────────────────────────────────
static void test_delete() {
    std::cout << "[test_delete]\n";
    auto r = exec("DELETE FROM employees WHERE name = 'Dave'");
    ASSERT_EQ(r.affectedRows, 1);

    r = exec("SELECT COUNT(*) FROM employees");
    ASSERT_EQ(fvs(r.rows[0][0]), "4");
}

// ── 10. 完整性约束 ────────────────────────────────────────────────────────────
static void test_constraints() {
    std::cout << "[test_constraints]\n";

    // NOT NULL 约束
    auto r = execExpectError("INSERT INTO employees (dept, salary) VALUES ('HR', 5000)");
    ASSERT_TRUE(!r.message.empty());

    // PK/UNIQUE 重复（id 为 PK）
    r = execExpectError("INSERT INTO employees (id, name, dept) VALUES (1, 'Dup', 'HR')");
    ASSERT_TRUE(!r.message.empty());

    // VARCHAR 长度超限
    r = execExpectError("INSERT INTO employees (name) VALUES ('"
        + std::string(60, 'x') + "')");
    ASSERT_TRUE(!r.message.empty());
}

// ── 11. 索引 DDL + 重复 UNIQUE ───────────────────────────────────────────────
static void test_index() {
    std::cout << "[test_index]\n";
    exec("CREATE TABLE products ("
         "  pid INT PRIMARY KEY AUTO_INCREMENT,"
         "  sku VARCHAR(20) NOT NULL,"
         "  price DOUBLE"
         ")");

    auto r = exec("CREATE UNIQUE INDEX idx_sku ON products (sku)");
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);

    exec("INSERT INTO products (sku, price) VALUES ('A001', 99.9)");
    exec("INSERT INTO products (sku, price) VALUES ('A002', 149.9)");

    // 重复 SKU → 索引 UNIQUE 违反
    r = execExpectError("INSERT INTO products (sku, price) VALUES ('A001', 50.0)");
    ASSERT_TRUE(!r.message.empty());

    r = exec("DROP INDEX idx_sku ON products");
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);

    // 删除索引后允许重复
    r = exec("INSERT INTO products (sku, price) VALUES ('A001', 50.0)");
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);
}

// ── 12. 外键约束 ──────────────────────────────────────────────────────────────
static void test_foreign_key() {
    std::cout << "[test_foreign_key]\n";

    // 父表
    exec("CREATE TABLE departments ("
         "  did INT PRIMARY KEY AUTO_INCREMENT,"
         "  dname VARCHAR(30) NOT NULL"
         ")");
    exec("INSERT INTO departments (dname) VALUES ('R&D')");
    exec("INSERT INTO departments (dname) VALUES ('Sales')");

    // 子表
    exec("CREATE TABLE staff ("
         "  sid INT PRIMARY KEY AUTO_INCREMENT,"
         "  sname VARCHAR(40) NOT NULL,"
         "  dept_id INT,"
         "  CONSTRAINT fk_dept FOREIGN KEY (dept_id) REFERENCES departments(did)"
         ")");

    // 合法插入
    auto r = exec("INSERT INTO staff (sname, dept_id) VALUES ('Tom', 1)");
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);

    // 引用不存在的父行 → FK 违反
    r = execExpectError("INSERT INTO staff (sname, dept_id) VALUES ('Jerry', 99)");
    ASSERT_TRUE(!r.message.empty());

    // 尝试删除被子表引用的父行 → FK 违反
    r = execExpectError("DELETE FROM departments WHERE did = 1");
    ASSERT_TRUE(!r.message.empty());

    // 先删子行，再删父行
    exec("DELETE FROM staff WHERE dept_id = 1");
    r = exec("DELETE FROM departments WHERE did = 1");
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);
}

// ── 13. ALTER TABLE ───────────────────────────────────────────────────────────
static void test_alter() {
    std::cout << "[test_alter]\n";
    auto r = exec("ALTER TABLE employees ADD COLUMN bonus DOUBLE DEFAULT 0.0");
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);

    r = exec("DESCRIBE employees");
    ASSERT_EQ(r.rows.size(), (size_t)5);  // 原4列 + bonus
}

// ── 清理 ──────────────────────────────────────────────────────────────────────
static void cleanup() {
    exec("DROP TABLE staff");
    exec("DROP TABLE departments");
    exec("DROP TABLE products");
    exec("DROP TABLE employees");
    exec("DROP DATABASE testdb");
}

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
    // 清理残留测试数据
    if (fs::exists(DATA_DIR)) fs::remove_all(DATA_DIR);

    gEng = new DBEngine(DATA_DIR);
    gSess = Session{"test", "", "", "root"};

    try {
        test_ddl();
        test_dml_basic();
        test_where();
        test_order_limit();
        test_aggregates_global();
        test_group_by();
        test_distinct();
        test_update();
        test_delete();
        test_constraints();
        test_index();
        test_foreign_key();
        test_alter();
        cleanup();
    } catch (const std::exception& e) {
        std::cerr << "UNCAUGHT EXCEPTION: " << e.what() << "\n";
        ++gFailed;
    }

    delete gEng;
    if (fs::exists(DATA_DIR)) fs::remove_all(DATA_DIR);

    std::cout << "\n============================\n";
    std::cout << "PASSED: " << gPassed << "  FAILED: " << gFailed << "\n";
    return gFailed == 0 ? 0 : 1;
}
