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
#include <cmath>
#include <chrono>

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

// ── 14. 多表 JOIN 查询 ────────────────────────────────────────────────────────
static void test_multi_table_join() {
    std::cout << "[test_multi_table_join]\n";

    // 创建两个表
    exec("CREATE TABLE users ("
         "  uid INT PRIMARY KEY AUTO_INCREMENT,"
         "  uname VARCHAR(30) NOT NULL"
         ")");
    exec("CREATE TABLE orders ("
         "  oid INT PRIMARY KEY AUTO_INCREMENT,"
         "  user_id INT,"
         "  amount DOUBLE"
         ")");

    exec("INSERT INTO users (uname) VALUES ('Alice')");
    exec("INSERT INTO users (uname) VALUES ('Bob')");
    exec("INSERT INTO users (uname) VALUES ('Carol')");

    exec("INSERT INTO orders (user_id, amount) VALUES (1, 100.5)");
    exec("INSERT INTO orders (user_id, amount) VALUES (1, 200.0)");
    exec("INSERT INTO orders (user_id, amount) VALUES (2, 150.0)");

    // 多表查询：用户和订单
    auto r = exec("SELECT u.uname, o.amount FROM users u, orders o WHERE u.uid = o.user_id");
    ASSERT_EQ(r.type, QueryResult::Type::SELECT);
    ASSERT_EQ(r.rows.size(), (size_t)3);  // Alice:100.5, Alice:200, Bob:150

    // 验证结果
    ASSERT_TRUE(std::get<std::string>(r.rows[0][0]) == "Alice");
    ASSERT_TRUE(std::abs(std::get<double>(r.rows[0][1]) - 100.5) < 0.01);

    // 测试表别名和 qualified 列名
    r = exec("SELECT users.uname, orders.amount FROM users, orders WHERE users.uid = orders.user_id");
    ASSERT_EQ(r.rows.size(), (size_t)3);

    // 空结果：不匹配的 WHERE
    r = exec("SELECT u.uname, o.amount FROM users u, orders o WHERE u.uid = 999");
    ASSERT_EQ(r.rows.size(), (size_t)0);

    // 清理
    exec("DROP TABLE orders");
    exec("DROP TABLE users");
}

// ── 15. 列别名和 qualified 投影 ──────────────────────────────────────────────
static void test_qualified_projection() {
    std::cout << "[test_qualified_projection]\n";

    exec("CREATE TABLE t1 (id INT, val VARCHAR(10))");
    exec("CREATE TABLE t2 (id INT, val VARCHAR(10))");

    exec("INSERT INTO t1 (id, val) VALUES (1, 'A')");
    exec("INSERT INTO t2 (id, val) VALUES (1, 'B')");

    // Qualified projection
    auto r = exec("SELECT t1.id, t1.val, t2.val FROM t1, t2");
    ASSERT_EQ(r.type, QueryResult::Type::SELECT);
    ASSERT_EQ(r.columns.size(), (size_t)3);
    ASSERT_EQ(r.rows.size(), (size_t)1);

    // 列别名
    r = exec("SELECT t1.id AS tid, t1.val AS v1, t2.val AS v2 FROM t1, t2");
    ASSERT_EQ(r.columns[0].name, "tid");
    ASSERT_EQ(r.columns[1].name, "v1");
    ASSERT_EQ(r.columns[2].name, "v2");

    // 未限定重复列名应明确报错，避免前端展示错误数据
    r = execExpectError("SELECT id FROM t1, t2");
    ASSERT_TRUE(r.message.find("Ambiguous") != std::string::npos);

    exec("DROP TABLE t1");
    exec("DROP TABLE t2");
}

// ── 16. ORDER BY with qualified names ─────────────────────────────────────────
static void test_order_by_qualified() {
    std::cout << "[test_order_by_qualified]\n";

    exec("CREATE TABLE items (id INT, name VARCHAR(20))");
    exec("INSERT INTO items (id, name) VALUES (3, 'Zebra')");
    exec("INSERT INTO items (id, name) VALUES (1, 'Apple')");
    exec("INSERT INTO items (id, name) VALUES (2, 'Banana')");

    auto r = exec("SELECT items.name FROM items ORDER BY items.id ASC");
    ASSERT_EQ(r.rows.size(), (size_t)3);
    ASSERT_EQ(std::get<std::string>(r.rows[0][0]), "Apple");
    ASSERT_EQ(std::get<std::string>(r.rows[1][0]), "Banana");
    ASSERT_EQ(std::get<std::string>(r.rows[2][0]), "Zebra");

    exec("DROP TABLE items");
}

// ── 17. CREATE INDEX backfill test ────────────────────────────────────────────
static void test_index_backfill() {
    std::cout << "[test_index_backfill]\n";

    exec("CREATE TABLE inventory (item VARCHAR(20), qty INT)");
    exec("INSERT INTO inventory (item, qty) VALUES ('Widget', 10)");
    exec("INSERT INTO inventory (item, qty) VALUES ('Gadget', 20)");

    // 创建索引应该回填现有记录
    auto r = exec("CREATE INDEX idx_item ON inventory (item)");
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);
    ASSERT_TRUE(r.message.find("backfilled") != std::string::npos);

    // 插入新记录后索引应该更新
    exec("INSERT INTO inventory (item, qty) VALUES ('Doodad', 30)");

    exec("DROP TABLE inventory");
}

// ── 17. Explicit INNER JOIN syntax ────────────────────────────────────────────
static void test_explicit_inner_join() {
    std::cout << "[test_explicit_inner_join]\n";

    exec("CREATE TABLE authors (aid INT PRIMARY KEY, aname VARCHAR(30))");
    exec("CREATE TABLE books (bid INT PRIMARY KEY, title VARCHAR(50), author_id INT)");

    exec("INSERT INTO authors (aid, aname) VALUES (1, 'Tolkien')");
    exec("INSERT INTO authors (aid, aname) VALUES (2, 'Rowling')");
    exec("INSERT INTO books (bid, title, author_id) VALUES (1, 'The Hobbit', 1)");
    exec("INSERT INTO books (bid, title, author_id) VALUES (2, 'LOTR', 1)");
    exec("INSERT INTO books (bid, title, author_id) VALUES (3, 'HP1', 2)");

    // Test INNER JOIN with ON
    auto r = exec("SELECT a.aname, b.title FROM authors a INNER JOIN books b ON a.aid = b.author_id");
    ASSERT_EQ(r.type, QueryResult::Type::SELECT);
    ASSERT_EQ(r.rows.size(), (size_t)3);

    // Test JOIN (implicit INNER) with ON
    r = exec("SELECT a.aname, b.title FROM authors a JOIN books b ON a.aid = b.author_id");
    ASSERT_EQ(r.rows.size(), (size_t)3);

    // Test ORDER BY with JOIN
    r = exec("SELECT a.aname, b.title FROM authors a JOIN books b ON a.aid = b.author_id ORDER BY b.title");
    ASSERT_EQ(r.rows.size(), (size_t)3);
    ASSERT_TRUE(std::get<std::string>(r.rows[0][1]) == "HP1");

    exec("DROP TABLE books");
    exec("DROP TABLE authors");
}

// ── 18. Chained INNER JOIN ────────────────────────────────────────────────────
static void test_chained_join() {
    std::cout << "[test_chained_join]\n";

    exec("CREATE TABLE customers (cid INT PRIMARY KEY, cname VARCHAR(30))");
    exec("CREATE TABLE orders2 (oid INT PRIMARY KEY, customer_id INT, total DOUBLE)");
    exec("CREATE TABLE items2 (iid INT PRIMARY KEY, order_id INT, iname VARCHAR(30))");

    exec("INSERT INTO customers (cid, cname) VALUES (1, 'Alice')");
    exec("INSERT INTO orders2 (oid, customer_id, total) VALUES (1, 1, 100.0)");
    exec("INSERT INTO items2 (iid, order_id, iname) VALUES (1, 1, 'Widget')");

    // Three-way JOIN
    auto r = exec("SELECT c.cname, o.total, i.iname FROM customers c "
                  "JOIN orders2 o ON c.cid = o.customer_id "
                  "JOIN items2 i ON o.oid = i.order_id");
    ASSERT_EQ(r.type, QueryResult::Type::SELECT);
    ASSERT_EQ(r.rows.size(), (size_t)1);
    ASSERT_TRUE(std::get<std::string>(r.rows[0][0]) == "Alice");
    ASSERT_TRUE(std::get<std::string>(r.rows[0][2]) == "Widget");

    exec("DROP TABLE items2");
    exec("DROP TABLE orders2");
    exec("DROP TABLE customers");
}

// ── 19. Qualified wildcard (alias.*) ──────────────────────────────────────────
static void test_qualified_wildcard() {
    std::cout << "[test_qualified_wildcard]\n";

    exec("CREATE TABLE products2 (pid INT, pname VARCHAR(20))");
    exec("CREATE TABLE stock (sid INT, product_id INT, qty INT)");

    exec("INSERT INTO products2 (pid, pname) VALUES (1, 'Laptop')");
    exec("INSERT INTO products2 (pid, pname) VALUES (2, 'Mouse')");
    exec("INSERT INTO stock (sid, product_id, qty) VALUES (1, 1, 10)");

    // Test p.* (qualified wildcard)
    auto r = exec("SELECT p.*, s.qty FROM products2 p, stock s WHERE p.pid = s.product_id");
    ASSERT_EQ(r.type, QueryResult::Type::SELECT);
    ASSERT_EQ(r.columns.size(), (size_t)3); // p.pid, p.pname, s.qty
    ASSERT_EQ(r.rows.size(), (size_t)1);
    ASSERT_TRUE(r.columns[0].name.find(".pid") != std::string::npos);
    ASSERT_TRUE(r.columns[1].name.find(".pname") != std::string::npos);

    // Test mixed wildcard and columns
    r = exec("SELECT s.*, p.pname FROM stock s, products2 p WHERE s.product_id = p.pid");
    ASSERT_EQ(r.columns.size(), (size_t)4); // s.sid, s.product_id, s.qty, p.pname
    ASSERT_EQ(r.rows.size(), (size_t)1);

    exec("DROP TABLE stock");
    exec("DROP TABLE products2");
}

// ── 20. Error: LEFT JOIN not supported ────────────────────────────────────────
static void test_left_join_unsupported() {
    std::cout << "[test_left_join_unsupported]\n";

    exec("CREATE TABLE ta (id INT)");
    exec("CREATE TABLE tb (id INT)");

    // LEFT JOIN should throw error
    auto r = execExpectError("SELECT * FROM ta LEFT JOIN tb ON ta.id = tb.id");
    ASSERT_TRUE(r.message.find("not supported") != std::string::npos);

    exec("DROP TABLE tb");
    exec("DROP TABLE ta");
}

// ── 21. JOIN boundary errors ─────────────────────────────────────────────────
static void test_join_boundary_errors() {
    std::cout << "[test_join_boundary_errors]\n";

    exec("CREATE TABLE ja (id INT, code INT, label VARCHAR(20))");
    exec("CREATE TABLE jb (id INT, a_code INT, label VARCHAR(20))");
    exec("INSERT INTO ja (id, code, label) VALUES (1, 10, 'A')");
    exec("INSERT INTO jb (id, a_code, label) VALUES (1, 10, 'B')");

    auto r = execExpectError("SELECT ja.id FROM ja, jb WHERE id = 1");
    ASSERT_TRUE(r.message.find("Ambiguous") != std::string::npos);

    r = execExpectError("SELECT missing.id FROM ja, jb WHERE ja.code = jb.a_code");
    ASSERT_TRUE(r.message.find("Unknown table or alias") != std::string::npos);

    r = execExpectError("SELECT ja.id FROM ja JOIN jb ON ja.missing = jb.a_code");
    ASSERT_TRUE(r.message.find("Unknown column") != std::string::npos);

    r = execExpectError("SELECT a.id FROM ja a JOIN jb a ON a.code = a.a_code");
    ASSERT_TRUE(r.message.find("Duplicate table alias") != std::string::npos);

    r = exec("SELECT ja.id, jb.label FROM ja JOIN jb ON ja.code = jb.a_code");
    ASSERT_EQ(r.rows.size(), (size_t)1);

    exec("DROP TABLE jb");
    exec("DROP TABLE ja");
}

// ── 22. JOIN benchmark baseline ──────────────────────────────────────────────
static void test_join_benchmark_baseline() {
    std::cout << "[test_join_benchmark_baseline]\n";

    exec("CREATE TABLE bench_users (uid INT PRIMARY KEY, name VARCHAR(20))");
    exec("CREATE TABLE bench_orders (oid INT PRIMARY KEY, user_id INT, amount INT)");

    int oid = 1;
    for (int user = 1; user <= 40; ++user) {
        exec("INSERT INTO bench_users (uid, name) VALUES (" + std::to_string(user) + ", 'U" + std::to_string(user) + "')");
        for (int order = 1; order <= 10; ++order) {
            exec("INSERT INTO bench_orders (oid, user_id, amount) VALUES ("
                 + std::to_string(oid++) + ", "
                 + std::to_string(user) + ", "
                 + std::to_string(order * 10) + ")");
        }
    }

    auto start = std::chrono::steady_clock::now();
    auto r = exec("SELECT u.uid, o.oid, o.amount FROM bench_users u "
                  "JOIN bench_orders o ON u.uid = o.user_id "
                  "WHERE o.amount >= 50 ORDER BY o.oid ASC");
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_EQ(r.type, QueryResult::Type::SELECT);
    ASSERT_EQ(r.rows.size(), (size_t)240);
    ASSERT_TRUE(elapsed < 5000);

    exec("DROP TABLE bench_orders");
    exec("DROP TABLE bench_users");
}

// ── 23. RESTORE DATABASE test ─────────────────────────────────────────────────
static void test_restore() {
    std::cout << "[test_restore]\n";

    // 创建测试数据库和表
    exec("CREATE DATABASE testdb2");
    exec("USE testdb2");
    exec("CREATE TABLE sample (id INT, txt VARCHAR(20))");
    exec("INSERT INTO sample (id, txt) VALUES (1, 'Hello')");
    exec("INSERT INTO sample (id, txt) VALUES (2, 'World')");

    // 备份到文件
    std::string backupFile = DATA_DIR + "/backup_test.sql";
    exec("BACKUP DATABASE testdb2 TO '" + backupFile + "'");
    ASSERT_TRUE(fs::exists(backupFile));

    // 删除表和数据库
    exec("DROP TABLE sample");
    exec("DROP DATABASE testdb2");

    // 从备份恢复
    auto r = exec("RESTORE DATABASE testdb2 FROM '" + backupFile + "'");
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);

    // 验证数据已恢复
    exec("USE testdb2");
    r = exec("SELECT * FROM sample");
    ASSERT_EQ(r.rows.size(), (size_t)2);

    // 清理
    exec("DROP TABLE sample");
    exec("DROP DATABASE testdb2");
    fs::remove(backupFile);
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
        test_multi_table_join();
        test_qualified_projection();
        test_order_by_qualified();
        test_index_backfill();
        test_explicit_inner_join();
        test_chained_join();
        test_qualified_wildcard();
        test_left_join_unsupported();
        test_join_boundary_errors();
        test_join_benchmark_baseline();
        test_restore();
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
