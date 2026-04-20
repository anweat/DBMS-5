/**
 * 综合全面测试
 *
 * 覆盖：用户注册/登录、SQL注入防御、WAL崩溃恢复、事务提交/回滚、
 *       B树索引查询、权限控制、约束检验、DDL/DML全覆盖、聚合/分组、
 *       特殊字符编码鲁棒性、边界条件
 */

#include "../src/engine/DBEngine.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <cassert>

namespace fs = std::filesystem;

// ── 简易测试框架 ─────────────────────────────────────────────────────────────
static int gPassed = 0, gFailed = 0;

#define ASSERT_TRUE(expr) do { \
    if (expr) { ++gPassed; } \
    else { ++gFailed; \
        std::cerr << "  FAIL [" #expr "] at " __FILE__ ":" << __LINE__ << "\n"; } \
} while(0)
#define ASSERT_EQ(a,b)  ASSERT_TRUE((a)==(b))
#define ASSERT_NE(a,b)  ASSERT_TRUE((a)!=(b))

// ── 全局引擎 ─────────────────────────────────────────────────────────────────
static const std::string DATA_DIR = "./test_data_comprehensive";
static DBEngine* gEng = nullptr;
static Session   rootSess;   // root 会话（所有权限）

static QueryResult execOk(const std::string& sql, Session& sess) {
    auto r = gEng->execute(sql, sess);
    if (r.type == QueryResult::Type::ERROR)
        std::cerr << "  Unexpected error: " << r.message << "  SQL=[" << sql << "]\n";
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);
    return r;
}
static QueryResult execErr(const std::string& sql, Session& sess) {
    auto r = gEng->execute(sql, sess);
    if (r.type != QueryResult::Type::ERROR)
        std::cerr << "  Expected error but got OK. SQL=[" << sql << "]\n";
    ASSERT_TRUE(r.type == QueryResult::Type::ERROR);
    return r;
}
static std::string fvs(const FieldValue& v) {
    if (std::holds_alternative<std::monostate>(v)) return "NULL";
    if (std::holds_alternative<int64_t>(v))  return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))   return std::to_string(std::get<double>(v));
    if (std::holds_alternative<bool>(v))     return std::get<bool>(v) ? "true" : "false";
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    return "?";
}

// ════════════════════════════════════════════════════════════════════════════
// 0. 环境初始化
// ════════════════════════════════════════════════════════════════════════════
static void setup() {
    rootSess.user = "root";  // root 拥有所有权限
    auto r = gEng->execute("CREATE DATABASE compdb", rootSess);
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);
    rootSess.currentDatabase = "compdb";
}

// ════════════════════════════════════════════════════════════════════════════
// 1. 用户注册 / 登录
// ════════════════════════════════════════════════════════════════════════════
static void test_user_registration_login() {
    std::cout << "[test_user_registration_login]\n";

    // 注册新用户
    execOk("CREATE USER 'alice' IDENTIFIED BY 'alice123'", rootSess);
    execOk("CREATE USER 'bob'   IDENTIFIED BY 'bob456'",   rootSess);

    // 重复注册应报错
    execErr("CREATE USER 'alice' IDENTIFIED BY 'other'", rootSess);

    // SHOW DATABASES 确认引擎正常运行（SHOW USERS 非本引擎语法）
    auto r = execOk("SHOW DATABASES", rootSess);
    ASSERT_TRUE(r.rowCount >= 1);

    // 正确密码登录
    Session aliceSess;
    auto ra = gEng->execute("CONNECT 'alice' IDENTIFIED BY 'alice123'", aliceSess);
    ASSERT_TRUE(ra.type != QueryResult::Type::ERROR);
    ASSERT_EQ(aliceSess.user, std::string("alice"));

    // 错误密码应报错
    Session badSess;
    execErr("CONNECT 'alice' IDENTIFIED BY 'wrong'", badSess);
    ASSERT_TRUE(badSess.user.empty());

    // 不存在的用户登录应报错
    Session noSess;
    execErr("CONNECT 'nobody' IDENTIFIED BY 'pass'", noSess);

    // 删除用户后无法登录
    execOk("DROP USER 'bob'", rootSess);
    Session bobSess2;
    execErr("CONNECT 'bob' IDENTIFIED BY 'bob456'", bobSess2);

    // 删除不存在用户应报错
    execErr("DROP USER 'bob'", rootSess);
}

// ════════════════════════════════════════════════════════════════════════════
// 2. SQL 注入防御
// ════════════════════════════════════════════════════════════════════════════
static void test_sql_injection() {
    std::cout << "[test_sql_injection]\n";

    execOk("CREATE TABLE injection_test (id INT PRIMARY KEY AUTO_INCREMENT, payload VARCHAR(200))", rootSess);

    // 典型注入字符串应作为字面量存储，不执行
    execOk("INSERT INTO injection_test (payload) VALUES ('; DROP TABLE injection_test; --')", rootSess);
    execOk("INSERT INTO injection_test (payload) VALUES ('OR 1=1 --')", rootSess);
    execOk("INSERT INTO injection_test (payload) VALUES ('UNION SELECT password FROM users')", rootSess);
    execOk("INSERT INTO injection_test (payload) VALUES ('UNION SELECT * FROM injection_test')", rootSess);
    // 表应依然存在，行数 = 4
    auto r = execOk("SELECT * FROM injection_test", rootSess);
    ASSERT_EQ(r.rowCount, 4);

    // WAL 特殊字符（|, %, ;, =, 换行）作为字符串值存储后能正确读回
    execOk("INSERT INTO injection_test (payload) VALUES ('pipe|semi;equals=pct%newline')", rootSess);
    r = execOk("SELECT * FROM injection_test WHERE payload = 'pipe|semi;equals=pct%newline'", rootSess);
    ASSERT_EQ(r.rowCount, 1);
    if (!r.rows.empty() && r.rows[0].size() >= 2)
        ASSERT_EQ(fvs(r.rows[0][1]), std::string("pipe|semi;equals=pct%newline"));

    // 注入式 CONNECT 用户名（含特殊字符），应登录失败
    Session injSess;
    execErr("CONNECT 'admin OR 1=1' IDENTIFIED BY 'x'", injSess);
    ASSERT_TRUE(injSess.user.empty());

    // 多语句注入尝试（以 ; 分隔）应报语法错误
    Session s2;
    auto r2 = gEng->execute("SELECT 1; DROP TABLE injection_test", s2);
    // 若多语句不支持，则应出错；表仍然存在
    r = execOk("SELECT * FROM injection_test", rootSess);
    ASSERT_TRUE(r.rowCount >= 4); // 表未被注入删除

    execOk("DROP TABLE injection_test", rootSess);
}

// ════════════════════════════════════════════════════════════════════════════
// 3. DDL 全覆盖
// ════════════════════════════════════════════════════════════════════════════
static void test_ddl_comprehensive() {
    std::cout << "[test_ddl_comprehensive]\n";

    // SHOW DATABASES
    auto r = execOk("SHOW DATABASES", rootSess);
    ASSERT_TRUE(r.rowCount >= 1);

    // CREATE + DROP DATABASE
    execOk("CREATE DATABASE tempdb2", rootSess);
    execErr("CREATE DATABASE tempdb2", rootSess);   // 重复创建
    execOk("DROP DATABASE tempdb2", rootSess);
    execErr("DROP DATABASE tempdb2", rootSess);     // 重复删除

    // USE 不存在数据库
    Session badDbSess;
    badDbSess.user = "root";
    execErr("USE nonexistent_db", badDbSess);

    // CREATE TABLE - 各种数据类型
    execOk(
        "CREATE TABLE all_types ("
        "  id       INT PRIMARY KEY AUTO_INCREMENT,"
        "  name     VARCHAR(100) NOT NULL,"
        "  score    FLOAT,"
        "  active   BOOLEAN DEFAULT TRUE,"
        "  notes    VARCHAR(1000)"
        ")",
        rootSess);
    execErr("CREATE TABLE all_types (id INT)", rootSess);  // 重复创建

    // DESCRIBE
    r = execOk("DESCRIBE all_types", rootSess);
    ASSERT_EQ(r.rowCount, 5);

    // SHOW TABLES
    r = execOk("SHOW TABLES", rootSess);
    ASSERT_TRUE(r.rowCount >= 1);

    // ALTER TABLE ADD COLUMN
    execOk("ALTER TABLE all_types ADD COLUMN created_at VARCHAR(30) DEFAULT 'now'", rootSess);
    r = execOk("DESCRIBE all_types", rootSess);
    ASSERT_EQ(r.rowCount, 6);

    // ALTER TABLE DROP COLUMN
    execOk("ALTER TABLE all_types DROP COLUMN notes", rootSess);
    r = execOk("DESCRIBE all_types", rootSess);
    ASSERT_EQ(r.rowCount, 5);

    // ALTER TABLE MODIFY COLUMN
    execOk("ALTER TABLE all_types MODIFY COLUMN name VARCHAR(200)", rootSess);

    // DROP TABLE
    execOk("DROP TABLE all_types", rootSess);
    execErr("DROP TABLE all_types", rootSess);  // 已删除
}

// ════════════════════════════════════════════════════════════════════════════
// 4. DML 全覆盖（INSERT / SELECT / UPDATE / DELETE）
// ════════════════════════════════════════════════════════════════════════════
static void test_dml_comprehensive() {
    std::cout << "[test_dml_comprehensive]\n";

    execOk(
        "CREATE TABLE products ("
        "  id    INT PRIMARY KEY AUTO_INCREMENT,"
        "  name  VARCHAR(60) NOT NULL,"
        "  cat   VARCHAR(30),"
        "  price FLOAT,"
        "  stock INT DEFAULT 0"
        ")",
        rootSess);

    // 多行 INSERT
    execOk("INSERT INTO products (name, cat, price, stock) VALUES ('Apple',  'fruit', 1.5, 100)", rootSess);
    execOk("INSERT INTO products (name, cat, price, stock) VALUES ('Banana', 'fruit', 0.5, 200)", rootSess);
    execOk("INSERT INTO products (name, cat, price, stock) VALUES ('Carrot', 'veg',   0.8, 150)", rootSess);
    execOk("INSERT INTO products (name, cat, price, stock) VALUES ('Daikon', 'veg',   1.2,  80)", rootSess);
    execOk("INSERT INTO products (name, cat, price, stock) VALUES ('Eggplant','veg',  2.0,  50)", rootSess);

    // SELECT *
    auto r = execOk("SELECT * FROM products", rootSess);
    ASSERT_EQ(r.rowCount, 5);

    // WHERE 等于
    r = execOk("SELECT * FROM products WHERE cat = 'fruit'", rootSess);
    ASSERT_EQ(r.rowCount, 2);

    // WHERE 不等于
    r = execOk("SELECT * FROM products WHERE cat != 'fruit'", rootSess);
    ASSERT_EQ(r.rowCount, 3);

    // WHERE 比较运算符
    r = execOk("SELECT * FROM products WHERE price > 1.0", rootSess);
    ASSERT_EQ(r.rowCount, 3);   // 1.5, 1.2, 2.0

    r = execOk("SELECT * FROM products WHERE price <= 0.8", rootSess);
    ASSERT_EQ(r.rowCount, 2);   // 0.5, 0.8

    // WHERE AND / OR
    r = execOk("SELECT * FROM products WHERE cat = 'veg' AND price < 1.5", rootSess);
    ASSERT_EQ(r.rowCount, 2);   // Carrot, Daikon

    r = execOk("SELECT * FROM products WHERE cat = 'fruit' OR price > 1.9", rootSess);
    ASSERT_EQ(r.rowCount, 3);   // Apple, Banana, Eggplant

    // ORDER BY ASC / DESC
    r = execOk("SELECT name FROM products ORDER BY price ASC", rootSess);
    ASSERT_EQ(r.rowCount, 5);
    if (!r.rows.empty()) ASSERT_EQ(fvs(r.rows[0][0]), std::string("Banana"));

    r = execOk("SELECT name FROM products ORDER BY price DESC", rootSess);
    if (!r.rows.empty()) ASSERT_EQ(fvs(r.rows[0][0]), std::string("Eggplant"));

    // LIMIT
    r = execOk("SELECT * FROM products ORDER BY id ASC LIMIT 2", rootSess);
    ASSERT_EQ(r.rowCount, 2);

    // UPDATE
    execOk("UPDATE products SET stock = 999 WHERE name = 'Apple'", rootSess);
    r = execOk("SELECT stock FROM products WHERE name = 'Apple'", rootSess);
    ASSERT_EQ(r.rowCount, 1);
    if (!r.rows.empty() && !r.rows[0].empty())
        ASSERT_EQ(fvs(r.rows[0][0]), std::string("999"));

    // UPDATE 多列
    execOk("UPDATE products SET price = 3.0, stock = 10 WHERE name = 'Eggplant'", rootSess);
    r = execOk("SELECT price FROM products WHERE name = 'Eggplant'", rootSess);
    if (!r.rows.empty() && !r.rows[0].empty())
        ASSERT_TRUE(fvs(r.rows[0][0]).find("3") != std::string::npos);

    // DELETE
    execOk("DELETE FROM products WHERE name = 'Daikon'", rootSess);
    r = execOk("SELECT * FROM products", rootSess);
    ASSERT_EQ(r.rowCount, 4);

    // DELETE WHERE 不匹配 - 影响0行但不出错
    execOk("DELETE FROM products WHERE name = 'NoSuchProduct'", rootSess);
    r = execOk("SELECT * FROM products", rootSess);
    ASSERT_EQ(r.rowCount, 4);
}

// ════════════════════════════════════════════════════════════════════════════
// 5. 聚合 / 分组 / DISTINCT
// ════════════════════════════════════════════════════════════════════════════
static void test_aggregates_group_distinct() {
    std::cout << "[test_aggregates_group_distinct]\n";

    // 使用 products 表（已有4行：Apple/Banana fruit; Carrot/Eggplant veg）
    auto r = execOk("SELECT COUNT(*) FROM products", rootSess);
    ASSERT_EQ(r.rowCount, 1);
    if (!r.rows.empty() && !r.rows[0].empty())
        ASSERT_EQ(fvs(r.rows[0][0]), std::string("4"));

    r = execOk("SELECT SUM(stock) FROM products", rootSess);
    ASSERT_EQ(r.rowCount, 1);

    r = execOk("SELECT AVG(price) FROM products", rootSess);
    ASSERT_EQ(r.rowCount, 1);

    r = execOk("SELECT MAX(price) FROM products", rootSess);
    ASSERT_EQ(r.rowCount, 1);

    r = execOk("SELECT MIN(price) FROM products", rootSess);
    ASSERT_EQ(r.rowCount, 1);

    // GROUP BY
    r = execOk("SELECT cat, COUNT(*) FROM products GROUP BY cat", rootSess);
    ASSERT_EQ(r.rowCount, 2);  // fruit, veg

    // HAVING
    r = execOk("SELECT cat, COUNT(*) FROM products GROUP BY cat HAVING COUNT(*) >= 2", rootSess);
    ASSERT_EQ(r.rowCount, 2);

    r = execOk("SELECT cat, COUNT(*) FROM products GROUP BY cat HAVING COUNT(*) > 2", rootSess);
    ASSERT_EQ(r.rowCount, 0);

    // DISTINCT
    r = execOk("SELECT DISTINCT cat FROM products", rootSess);
    ASSERT_EQ(r.rowCount, 2);

    execOk("DROP TABLE products", rootSess);
}

// ════════════════════════════════════════════════════════════════════════════
// 6. 完整性约束
// ════════════════════════════════════════════════════════════════════════════
static void test_constraints() {
    std::cout << "[test_constraints]\n";

    execOk(
        "CREATE TABLE orders ("
        "  id       INT PRIMARY KEY,"
        "  customer VARCHAR(50) NOT NULL,"
        "  amount   FLOAT NOT NULL,"
        "  code     VARCHAR(20) UNIQUE"
        ")",
        rootSess);

    // NOT NULL 违反
    execErr("INSERT INTO orders (id, customer, amount) VALUES (1, NULL, 10.0)", rootSess);
    execErr("INSERT INTO orders (id, customer, amount) VALUES (2, 'Alice', NULL)", rootSess);

    // 正常插入（使用显式 ID 保证外键测试可预测）
    execOk("INSERT INTO orders (id, customer, amount, code) VALUES (1, 'Alice', 50.0, 'ORD-001')", rootSess);
    execOk("INSERT INTO orders (id, customer, amount, code) VALUES (2, 'Bob',   30.0, 'ORD-002')", rootSess);

    // UNIQUE 违反
    execErr("INSERT INTO orders (id, customer, amount, code) VALUES (3, 'Carol', 20.0, 'ORD-001')", rootSess);

    // 验证2行存在
    auto r = execOk("SELECT id FROM orders ORDER BY id ASC", rootSess);
    ASSERT_EQ(r.rowCount, 2);
    if (r.rowCount == 2)
        ASSERT_NE(fvs(r.rows[0][0]), fvs(r.rows[1][0]));

    // 外键约束
    execOk(
        "CREATE TABLE order_items ("
        "  item_id  INT PRIMARY KEY AUTO_INCREMENT,"
        "  order_id INT NOT NULL,"
        "  product  VARCHAR(50),"
        "  FOREIGN KEY (order_id) REFERENCES orders(id)"
        ")",
        rootSess);

    execOk("INSERT INTO order_items (order_id, product) VALUES (1, 'Widget')", rootSess);
    execErr("INSERT INTO order_items (order_id, product) VALUES (999, 'Ghost')", rootSess);

    execOk("DROP TABLE order_items", rootSess);
    execOk("DROP TABLE orders", rootSess);
}

// ════════════════════════════════════════════════════════════════════════════
// 7. B 树索引查询（大量数据）
// ════════════════════════════════════════════════════════════════════════════
static void test_btree_index() {
    std::cout << "[test_btree_index]\n";

    execOk(
        "CREATE TABLE employees ("
        "  id     INT PRIMARY KEY AUTO_INCREMENT,"
        "  name   VARCHAR(60) NOT NULL,"
        "  dept   VARCHAR(30),"
        "  salary INT"
        ")",
        rootSess);

    // 创建索引
    execOk("CREATE INDEX idx_dept   ON employees (dept)", rootSess);
    execOk("CREATE INDEX idx_salary ON employees (salary)", rootSess);

    // 插入足够多的行，使 B 树分裂为多层（T=4 → 超过 7 行时发生分裂）
    const char* depts[] = {"Eng","Mkt","HR","Eng","Mkt","Eng","HR","Eng","Mkt","HR",
                           "Eng","Mkt","Eng","HR","Mkt","Eng","Mkt","HR","Eng","Mkt"};
    for (int i = 0; i < 20; ++i) {
        std::string sql = "INSERT INTO employees (name, dept, salary) VALUES ('Emp"
                        + std::to_string(i+1) + "', '"
                        + depts[i] + "', "
                        + std::to_string(30000 + i * 1000) + ")";
        execOk(sql, rootSess);
    }

    auto r = execOk("SELECT * FROM employees", rootSess);
    ASSERT_EQ(r.rowCount, 20);

    // 索引列上的查询
    r = execOk("SELECT * FROM employees WHERE dept = 'Eng'", rootSess);
    ASSERT_TRUE(r.rowCount >= 7);  // 多个 Eng 员工

    r = execOk("SELECT * FROM employees WHERE salary > 40000", rootSess);
    ASSERT_TRUE(r.rowCount >= 9);  // salary 从 30000 开始每条 +1000

    // UNIQUE 索引 - 重复键应报错
    execOk(
        "CREATE TABLE unique_test (id INT PRIMARY KEY, code VARCHAR(20))",
        rootSess);
    execOk("CREATE UNIQUE INDEX uidx ON unique_test (code)", rootSess);
    execOk("INSERT INTO unique_test (id, code) VALUES (1, 'X100')", rootSess);
    execErr("INSERT INTO unique_test (id, code) VALUES (2, 'X100')", rootSess);

    // DROP INDEX
    execOk("DROP INDEX idx_dept   ON employees", rootSess);
    execOk("DROP INDEX idx_salary ON employees", rootSess);
    execOk("DROP INDEX uidx ON unique_test", rootSess);
    execOk("DROP TABLE unique_test", rootSess);
    execOk("DROP TABLE employees", rootSess);
}

// ════════════════════════════════════════════════════════════════════════════
// 8. 事务：COMMIT 持久性
// ════════════════════════════════════════════════════════════════════════════
static void test_transaction_commit() {
    std::cout << "[test_transaction_commit]\n";

    execOk("CREATE TABLE txtest (id INT PRIMARY KEY AUTO_INCREMENT, val VARCHAR(50))", rootSess);

    Session s;
    s.user = "root";
    s.currentDatabase = "compdb";

    execOk("BEGIN", s);
    execOk("INSERT INTO txtest (val) VALUES ('committed_row')", s);
    execOk("INSERT INTO txtest (val) VALUES ('second_committed')", s);
    execOk("COMMIT", s);

    auto r = execOk("SELECT * FROM txtest", rootSess);
    ASSERT_EQ(r.rowCount, 2);
}

// ════════════════════════════════════════════════════════════════════════════
// 9. 事务：INSERT 回滚
// ════════════════════════════════════════════════════════════════════════════
static void test_transaction_rollback_insert() {
    std::cout << "[test_transaction_rollback_insert]\n";

    auto before = execOk("SELECT * FROM txtest", rootSess).rowCount;

    Session s;
    s.user = "root";
    s.currentDatabase = "compdb";

    execOk("BEGIN", s);
    execOk("INSERT INTO txtest (val) VALUES ('rollback_ins_1')", s);
    execOk("INSERT INTO txtest (val) VALUES ('rollback_ins_2')", s);

    // 事务内可见
    auto r = execOk("SELECT * FROM txtest", s);
    ASSERT_EQ(r.rowCount, before + 2);

    execOk("ROLLBACK", s);

    r = execOk("SELECT * FROM txtest", rootSess);
    ASSERT_EQ(r.rowCount, before);  // 回滚后恢复
}

// ════════════════════════════════════════════════════════════════════════════
// 10. 事务：UPDATE 回滚
// ════════════════════════════════════════════════════════════════════════════
static void test_transaction_rollback_update() {
    std::cout << "[test_transaction_rollback_update]\n";

    auto orig = execOk("SELECT val FROM txtest WHERE val = 'committed_row'", rootSess);
    ASSERT_EQ(orig.rowCount, 1);

    Session s;
    s.user = "root";
    s.currentDatabase = "compdb";

    execOk("BEGIN", s);
    execOk("UPDATE txtest SET val = 'MODIFIED' WHERE val = 'committed_row'", s);

    // 事务内已修改
    auto r = execOk("SELECT * FROM txtest WHERE val = 'MODIFIED'", s);
    ASSERT_EQ(r.rowCount, 1);

    execOk("ROLLBACK", s);

    // 回滚后原值恢复
    r = execOk("SELECT * FROM txtest WHERE val = 'committed_row'", rootSess);
    ASSERT_EQ(r.rowCount, 1);
    r = execOk("SELECT * FROM txtest WHERE val = 'MODIFIED'", rootSess);
    ASSERT_EQ(r.rowCount, 0);
}

// ════════════════════════════════════════════════════════════════════════════
// 11. 事务：DELETE 回滚
// ════════════════════════════════════════════════════════════════════════════
static void test_transaction_rollback_delete() {
    std::cout << "[test_transaction_rollback_delete]\n";

    auto beforeR = execOk("SELECT * FROM txtest", rootSess);
    int before = beforeR.rowCount;

    Session s;
    s.user = "root";
    s.currentDatabase = "compdb";

    execOk("BEGIN", s);
    execOk("DELETE FROM txtest WHERE val = 'second_committed'", s);

    auto r = execOk("SELECT * FROM txtest", s);
    ASSERT_EQ(r.rowCount, before - 1);

    execOk("ROLLBACK", s);

    r = execOk("SELECT * FROM txtest", rootSess);
    ASSERT_EQ(r.rowCount, before);  // 行被恢复
    execOk("DROP TABLE txtest", rootSess);
}

// ════════════════════════════════════════════════════════════════════════════
// 12. 嵌套 BEGIN 报错
// ════════════════════════════════════════════════════════════════════════════
static void test_nested_begin() {
    std::cout << "[test_nested_begin]\n";

    Session s;
    s.user = "root";
    s.currentDatabase = "compdb";

    execOk("BEGIN", s);
    ASSERT_TRUE(!s.transactionId.empty());

    execErr("BEGIN", s);  // 已在事务中，应报错

    execOk("ROLLBACK", s);
    ASSERT_TRUE(s.transactionId.empty());
}

// ════════════════════════════════════════════════════════════════════════════
// 13. WAL 崩溃恢复模拟
//     创建引擎→开启事务→插入数据→销毁引擎(模拟崩溃)→新引擎自动撤销
// ════════════════════════════════════════════════════════════════════════════
static void test_wal_crash_recovery() {
    std::cout << "[test_wal_crash_recovery]\n";

    std::string crashDir = DATA_DIR + "_crash";
    if (fs::exists(crashDir)) fs::remove_all(crashDir);

    // ─── 第一阶段：建立基础数据 + 开始未提交事务 ───
    {
        DBEngine eng1(crashDir);
        Session s;
        s.user = "root";
        eng1.execute("CREATE DATABASE crashdb", s);
        s.currentDatabase = "crashdb";
        eng1.execute("CREATE TABLE crash_log (id INT PRIMARY KEY AUTO_INCREMENT, msg VARCHAR(100))", s);
        eng1.execute("INSERT INTO crash_log (msg) VALUES ('persisted_row')", s);

        // 未提交事务（模拟进程崩溃时的中断事务）
        eng1.execute("BEGIN", s);
        eng1.execute("INSERT INTO crash_log (msg) VALUES ('undo_row_1')", s);
        eng1.execute("INSERT INTO crash_log (msg) VALUES ('undo_row_2')", s);
        // eng1 析构，未调用 COMMIT / ROLLBACK
    }

    // ─── 第二阶段：新引擎启动时自动进行 WAL 崩溃恢复 ───
    {
        DBEngine eng2(crashDir);
        Session s;
        s.user = "root";
        s.currentDatabase = "crashdb";

        auto r = eng2.execute("SELECT * FROM crash_log", s);
        ASSERT_TRUE(r.type != QueryResult::Type::ERROR);
        // 只有 persisted_row；undo_row_1/2 应被 WAL recover 撤销
        ASSERT_EQ(r.rowCount, 1);
        if (!r.rows.empty() && r.rows[0].size() >= 2)
            ASSERT_EQ(fvs(r.rows[0][1]), std::string("persisted_row"));
    }

    // 崩溃后再次正常写入不受影响
    {
        DBEngine eng3(crashDir);
        Session s;
        s.user = "root";
        s.currentDatabase = "crashdb";

        eng3.execute("INSERT INTO crash_log (msg) VALUES ('post_recovery_row')", s);
        auto r = eng3.execute("SELECT * FROM crash_log", s);
        ASSERT_TRUE(r.type != QueryResult::Type::ERROR);
        ASSERT_EQ(r.rowCount, 2);
    }

    if (fs::exists(crashDir)) fs::remove_all(crashDir);
}

// ════════════════════════════════════════════════════════════════════════════
// 14. 权限控制全覆盖
// ════════════════════════════════════════════════════════════════════════════
static void test_privilege_full() {
    std::cout << "[test_privilege_full]\n";

    execOk("CREATE TABLE priv_table (id INT PRIMARY KEY, data VARCHAR(50))", rootSess);
    execOk("INSERT INTO priv_table (id, data) VALUES (1, 'row1')", rootSess);

    // alice 登录
    Session aliceSess;
    gEng->execute("CONNECT 'alice' IDENTIFIED BY 'alice123'", aliceSess);
    aliceSess.currentDatabase = "compdb";

    // 未授权访问应报错
    execErr("SELECT * FROM priv_table", aliceSess);
    execErr("INSERT INTO priv_table (id, data) VALUES (2, 'x')", aliceSess);
    execErr("UPDATE priv_table SET data = 'y' WHERE id = 1", aliceSess);
    execErr("DELETE FROM priv_table WHERE id = 1", aliceSess);

    // GRANT SELECT
    execOk("GRANT SELECT ON compdb.priv_table TO 'alice'", rootSess);
    execOk("SELECT * FROM priv_table", aliceSess);   // 现在可以查询
    execErr("INSERT INTO priv_table (id, data) VALUES (3, 'z')", aliceSess); // 仍无 INSERT

    // GRANT INSERT
    execOk("GRANT INSERT ON compdb.priv_table TO 'alice'", rootSess);
    execOk("INSERT INTO priv_table (id, data) VALUES (4, 'alice_row')", aliceSess);

    // REVOKE SELECT
    execOk("REVOKE SELECT ON compdb.priv_table FROM 'alice'", rootSess);
    execErr("SELECT * FROM priv_table", aliceSess);  // SELECT 已撤销

    // 权限升级攻击：alice 不能给自己 GRANT
    execErr("GRANT ALL ON compdb.priv_table TO 'alice'", aliceSess);

    execOk("DROP TABLE priv_table", rootSess);
}

// ════════════════════════════════════════════════════════════════════════════
// 15. 边界与错误条件
// ════════════════════════════════════════════════════════════════════════════
static void test_edge_cases() {
    std::cout << "[test_edge_cases]\n";

    // 查询不存在的表应报错
    execErr("SELECT * FROM nonexistent_table", rootSess);
    execErr("INSERT INTO nonexistent_table (id) VALUES (1)", rootSess);
    execErr("UPDATE nonexistent_table SET id = 2", rootSess);
    execErr("DELETE FROM nonexistent_table", rootSess);

    // 对空表的聚合
    execOk("CREATE TABLE empty_t (x INT)", rootSess);
    auto r = execOk("SELECT COUNT(*) FROM empty_t", rootSess);
    ASSERT_EQ(r.rowCount, 1);
    if (!r.rows.empty() && !r.rows[0].empty())
        ASSERT_EQ(fvs(r.rows[0][0]), std::string("0"));

    // 全表删除后仍可 INSERT
    execOk("INSERT INTO empty_t (x) VALUES (42)", rootSess);
    execOk("DELETE FROM empty_t", rootSess);
    r = execOk("SELECT * FROM empty_t", rootSess);
    ASSERT_EQ(r.rowCount, 0);
    execOk("INSERT INTO empty_t (x) VALUES (99)", rootSess);
    r = execOk("SELECT * FROM empty_t", rootSess);
    ASSERT_EQ(r.rowCount, 1);
    execOk("DROP TABLE empty_t", rootSess);

    // IS NULL / IS NOT NULL
    execOk("CREATE TABLE null_test (id INT, note VARCHAR(50))", rootSess);
    execOk("INSERT INTO null_test (id, note) VALUES (1, 'has_note')", rootSess);
    execOk("INSERT INTO null_test (id) VALUES (2)", rootSess);  // note 为 NULL

    r = execOk("SELECT * FROM null_test WHERE note IS NULL", rootSess);
    ASSERT_EQ(r.rowCount, 1);
    r = execOk("SELECT * FROM null_test WHERE note IS NOT NULL", rootSess);
    ASSERT_EQ(r.rowCount, 1);
    execOk("DROP TABLE null_test", rootSess);

    // 极长字符串值
    std::string longVal(200, 'X');
    execOk("CREATE TABLE long_test (id INT, txt VARCHAR(300))", rootSess);
    execOk("INSERT INTO long_test (id, txt) VALUES (1, '" + longVal + "')", rootSess);
    r = execOk("SELECT txt FROM long_test WHERE id = 1", rootSess);
    ASSERT_EQ(r.rowCount, 1);
    if (!r.rows.empty() && !r.rows[0].empty())
        ASSERT_EQ(fvs(r.rows[0][0]), longVal);
    execOk("DROP TABLE long_test", rootSess);
}

// ════════════════════════════════════════════════════════════════════════════
// 清理
// ════════════════════════════════════════════════════════════════════════════
static void cleanup() {
    execOk("DROP USER 'alice'", rootSess);
    execOk("DROP DATABASE compdb", rootSess);
}

// ════════════════════════════════════════════════════════════════════════════
// main
// ════════════════════════════════════════════════════════════════════════════
int main() {
    if (fs::exists(DATA_DIR)) fs::remove_all(DATA_DIR);
    gEng = new DBEngine(DATA_DIR);

    try {
        setup();
        test_user_registration_login();
        test_sql_injection();
        test_ddl_comprehensive();
        test_dml_comprehensive();
        test_aggregates_group_distinct();
        test_constraints();
        test_btree_index();
        test_transaction_commit();
        test_transaction_rollback_insert();
        test_transaction_rollback_update();
        test_transaction_rollback_delete();
        test_nested_begin();
        test_wal_crash_recovery();
        test_privilege_full();
        test_edge_cases();
        cleanup();
    } catch (const std::exception& e) {
        std::cerr << "UNCAUGHT EXCEPTION: " << e.what() << "\n";
        ++gFailed;
    }

    delete gEng;
    if (fs::exists(DATA_DIR)) fs::remove_all(DATA_DIR);

    std::cout << "\n============================\n";
    std::cout << "PASSED: " << gPassed << "  FAILED: " << gFailed << "\n";
    std::cout << "============================\n";
    return gFailed == 0 ? 0 : 1;
}
