/**
 * 安全性测试（用户系统 + 事务系统）
 *
 * 测试覆盖：
 *   用户管理   – CREATE USER / DROP USER / 重复创建
 *   身份认证   – CONNECT 正确 / 错误密码 / 不存在用户
 *   权限检查   – 未授权拒绝 / GRANT 后允许 / REVOKE 后拒绝
 *   权限范围   – 通配符 *.* / db.* / db.table 三级覆盖
 *   事务提交   – BEGIN→INSERT→COMMIT 数据持久
 *   事务回滚   – BEGIN→INSERT→ROLLBACK 数据消除
 *   UPDATE回滚 – BEGIN→UPDATE→ROLLBACK 旧值恢复
 *   DELETE回滚 – BEGIN→DELETE→ROLLBACK 行恢复
 *   嵌套 BEGIN – 重复 BEGIN 应报错
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
        std::cerr << "FAIL [" #expr "] at " __FILE__ ":" << __LINE__ << "\n"; } \
} while(0)
#define ASSERT_EQ(a,b)  ASSERT_TRUE((a)==(b))
#define ASSERT_NE(a,b)  ASSERT_TRUE((a)!=(b))

// ── 全局引擎 ─────────────────────────────────────────────────────────────────
static const std::string DATA_DIR = "./test_data_security";
static DBEngine* gEng = nullptr;

// root 会话（无权限限制）
static Session rootSess;
// 受限用户会话
static Session aliceSess;
static Session bobSess;

// 辅助：在指定会话中执行 SQL
static QueryResult exec(const std::string& sql, Session& sess) {
    auto r = gEng->execute(sql, sess);
    return r;
}
// 辅助：执行 SQL 并断言不出错
static QueryResult execOk(const std::string& sql, Session& sess) {
    auto r = gEng->execute(sql, sess);
    if (r.type == QueryResult::Type::ERROR)
        std::cerr << "  Unexpected error: " << r.message << "  SQL: " << sql << "\n";
    ASSERT_TRUE(r.type != QueryResult::Type::ERROR);
    return r;
}
// 辅助：执行 SQL 并断言出错
static QueryResult execErr(const std::string& sql, Session& sess) {
    auto r = gEng->execute(sql, sess);
    ASSERT_TRUE(r.type == QueryResult::Type::ERROR);
    return r;
}

// ── FieldValue 转字符串（用于断言）────────────────────────────────────────────
static std::string fvs(const FieldValue& v) {
    if (std::holds_alternative<std::monostate>(v)) return "NULL";
    if (std::holds_alternative<int64_t>(v))  return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))   return std::to_string(std::get<double>(v));
    if (std::holds_alternative<bool>(v))     return std::get<bool>(v) ? "true" : "false";
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    return "?";
}

// ════════════════════════════════════════════════════════════════════════════
// 1. 用户管理测试
// ════════════════════════════════════════════════════════════════════════════
static void test_user_management() {
    std::cout << "[test_user_management]\n";

    // 创建用户 alice
    auto r = execOk("CREATE USER 'alice' IDENTIFIED BY 'alice123'", rootSess);
    ASSERT_TRUE(r.message.find("alice") != std::string::npos);

    // 再次创建同名用户 → 应报错（DUPLICATE_KEY）
    r = execErr("CREATE USER 'alice' IDENTIFIED BY 'pass'", rootSess);
    ASSERT_TRUE(r.error.has_value());
    ASSERT_EQ(r.error->code, ErrorCode::DUPLICATE_KEY);

    // 创建用户 bob
    execOk("CREATE USER 'bob' IDENTIFIED BY 'bob456'", rootSess);

    // 删除 bob
    execOk("DROP USER 'bob'", rootSess);

    // 删除不存在的用户 → 应报错
    execErr("DROP USER 'bob'", rootSess);

    // 尝试删除 root → 应报错
    execErr("DROP USER 'root'", rootSess);
}

// ════════════════════════════════════════════════════════════════════════════
// 2. 身份认证测试
// ════════════════════════════════════════════════════════════════════════════
static void test_authentication() {
    std::cout << "[test_authentication]\n";

    // 正确密码登录
    auto r = execOk("CONNECT 'alice' IDENTIFIED BY 'alice123'", aliceSess);
    ASSERT_EQ(aliceSess.user, std::string("alice"));
    ASSERT_TRUE(r.message.find("alice") != std::string::npos);

    // 错误密码 → 应报错
    Session tmpSess;
    r = execErr("CONNECT 'alice' IDENTIFIED BY 'wrong'", tmpSess);
    ASSERT_EQ(tmpSess.user, std::string(""));

    // 不存在的用户 → 应报错
    r = execErr("CONNECT 'nobody' IDENTIFIED BY 'pass'", tmpSess);

    // root 登录
    Session rootLoginSess;
    execOk("CONNECT 'root' IDENTIFIED BY 'root'", rootLoginSess);
    ASSERT_EQ(rootLoginSess.user, std::string("root"));
}

// ════════════════════════════════════════════════════════════════════════════
// 3. 权限测试
// ════════════════════════════════════════════════════════════════════════════
static void test_permissions() {
    std::cout << "[test_permissions]\n";

    // 先以 root 建库建表
    execOk("CREATE DATABASE secdb", rootSess);
    execOk("USE secdb", rootSess);
    execOk("CREATE TABLE items (id INT PRIMARY KEY AUTO_INCREMENT, name VARCHAR(50) NOT NULL, qty INT DEFAULT 0)", rootSess);
    execOk("INSERT INTO items (name, qty) VALUES ('apple', 10)", rootSess);
    execOk("INSERT INTO items (name, qty) VALUES ('banana', 20)", rootSess);

    // alice 尚未被授权任何权限
    aliceSess.currentDatabase = "secdb";

    // alice 尝试 SELECT → PERMISSION_DENIED
    execErr("SELECT * FROM items", aliceSess);

    // alice 尝试 INSERT → PERMISSION_DENIED
    execErr("INSERT INTO items (name, qty) VALUES ('cherry', 5)", aliceSess);

    // ── 授予 SELECT 权限 ────────────────────────────────────────────────
    execOk("GRANT SELECT ON secdb.items TO 'alice'", rootSess);

    // alice 现在可以 SELECT
    auto r = execOk("SELECT * FROM items", aliceSess);
    ASSERT_EQ(r.type, QueryResult::Type::SELECT);
    ASSERT_EQ(r.rowCount, 2);

    // alice 仍不能 INSERT
    execErr("INSERT INTO items (name, qty) VALUES ('cherry', 5)", aliceSess);

    // ── 授予 INSERT 权限 ────────────────────────────────────────────────
    execOk("GRANT INSERT ON secdb.items TO 'alice'", rootSess);

    // alice 现在可以 INSERT
    execOk("INSERT INTO items (name, qty) VALUES ('cherry', 5)", aliceSess);

    // alice 不能 UPDATE
    execErr("UPDATE items SET qty = 99 WHERE name = 'apple'", aliceSess);

    // alice 不能 DELETE
    execErr("DELETE FROM items WHERE name = 'cherry'", aliceSess);

    // ── 撤销 SELECT 权限 ────────────────────────────────────────────────
    execOk("REVOKE SELECT ON secdb.items FROM 'alice'", rootSess);

    // alice 现在不能 SELECT
    execErr("SELECT * FROM items", aliceSess);

    // ── 授予 ALL 权限 ON *.* ────────────────────────────────────────────
    // 创建第二个用户 charlie 并赋予全局权限
    execOk("CREATE USER 'charlie' IDENTIFIED BY 'charlie789'", rootSess);
    Session charlieSess;
    charlieSess.user = "charlie";
    charlieSess.currentDatabase = "secdb";
    // 先确认无权限
    execErr("SELECT * FROM items", charlieSess);

    execOk("GRANT ALL ON *.* TO 'charlie'", rootSess);
    // charlie 现在可以做所有操作
    r = execOk("SELECT * FROM items", charlieSess);
    ASSERT_EQ(r.type, QueryResult::Type::SELECT);
    execOk("UPDATE items SET qty = 50 WHERE name = 'apple'", charlieSess);
    execOk("DELETE FROM items WHERE name = 'banana'", charlieSess);

    // 验证操作生效
    r = execOk("SELECT * FROM items WHERE name = 'apple'", charlieSess);
    ASSERT_EQ(r.rowCount, 1);
    if (!r.rows.empty() && r.rows[0].size() >= 3)
        ASSERT_EQ(fvs(r.rows[0][2]), std::string("50"));
}

// ════════════════════════════════════════════════════════════════════════════
// 4. 非 root 用户无法管理其他用户
// ════════════════════════════════════════════════════════════════════════════
static void test_privilege_escalation() {
    std::cout << "[test_privilege_escalation]\n";

    // alice 不能创建用户
    execErr("CREATE USER 'hacker' IDENTIFIED BY 'hack'", aliceSess);

    // alice 不能 GRANT
    execErr("GRANT ALL ON *.* TO 'alice'", aliceSess);

    // alice 不能 DROP USER
    execErr("DROP USER 'charlie'", aliceSess);
}

// ════════════════════════════════════════════════════════════════════════════
// 5. 事务：COMMIT 持久化
// ════════════════════════════════════════════════════════════════════════════
static void test_transaction_commit() {
    std::cout << "[test_transaction_commit]\n";

    Session sess;
    sess.user = "root";
    sess.currentDatabase = "secdb";

    // 计算初始行数
    auto r = execOk("SELECT * FROM items", sess);
    int before = r.rowCount;

    execOk("BEGIN", sess);
    ASSERT_TRUE(!sess.transactionId.empty());

    execOk("INSERT INTO items (name, qty) VALUES ('mango', 7)", sess);
    execOk("INSERT INTO items (name, qty) VALUES ('grape', 3)", sess);

    execOk("COMMIT", sess);
    ASSERT_TRUE(sess.transactionId.empty());

    // 行数应增加 2
    r = execOk("SELECT * FROM items", sess);
    ASSERT_EQ(r.rowCount, before + 2);
}

// ════════════════════════════════════════════════════════════════════════════
// 6. 事务：ROLLBACK 取消 INSERT
// ════════════════════════════════════════════════════════════════════════════
static void test_transaction_rollback_insert() {
    std::cout << "[test_transaction_rollback_insert]\n";

    Session sess;
    sess.user = "root";
    sess.currentDatabase = "secdb";

    auto r = execOk("SELECT * FROM items", sess);
    int before = r.rowCount;

    execOk("BEGIN", sess);
    execOk("INSERT INTO items (name, qty) VALUES ('kiwi', 15)", sess);
    execOk("INSERT INTO items (name, qty) VALUES ('lemon', 2)", sess);

    execOk("ROLLBACK", sess);
    ASSERT_TRUE(sess.transactionId.empty());

    // 行数应恢复到 before
    r = execOk("SELECT * FROM items", sess);
    ASSERT_EQ(r.rowCount, before);

    // kiwi 不应存在
    r = execOk("SELECT * FROM items WHERE name = 'kiwi'", sess);
    ASSERT_EQ(r.rowCount, 0);
}

// ════════════════════════════════════════════════════════════════════════════
// 7. 事务：ROLLBACK 恢复 UPDATE
// ════════════════════════════════════════════════════════════════════════════
static void test_transaction_rollback_update() {
    std::cout << "[test_transaction_rollback_update]\n";

    Session sess;
    sess.user = "root";
    sess.currentDatabase = "secdb";

    // 查出 apple 当前 qty
    auto r = execOk("SELECT * FROM items WHERE name = 'apple'", sess);
    ASSERT_EQ(r.rowCount, 1);
    std::string origQty = (!r.rows.empty() && r.rows[0].size() >= 3)
                          ? fvs(r.rows[0][2]) : "-1";

    execOk("BEGIN", sess);
    execOk("UPDATE items SET qty = 9999 WHERE name = 'apple'", sess);

    // 事务内可见变更
    r = execOk("SELECT * FROM items WHERE name = 'apple'", sess);
    ASSERT_EQ(r.rowCount, 1);
    if (!r.rows.empty() && r.rows[0].size() >= 3)
        ASSERT_EQ(fvs(r.rows[0][2]), std::string("9999"));

    execOk("ROLLBACK", sess);

    // 回滚后恢复旧值
    r = execOk("SELECT * FROM items WHERE name = 'apple'", sess);
    ASSERT_EQ(r.rowCount, 1);
    if (!r.rows.empty() && r.rows[0].size() >= 3)
        ASSERT_EQ(fvs(r.rows[0][2]), origQty);
}

// ════════════════════════════════════════════════════════════════════════════
// 8. 事务：ROLLBACK 恢复 DELETE
// ════════════════════════════════════════════════════════════════════════════
static void test_transaction_rollback_delete() {
    std::cout << "[test_transaction_rollback_delete]\n";

    Session sess;
    sess.user = "root";
    sess.currentDatabase = "secdb";

    auto r = execOk("SELECT * FROM items", sess);
    int before = r.rowCount;

    // 确保 apple 存在
    r = execOk("SELECT * FROM items WHERE name = 'apple'", sess);
    ASSERT_EQ(r.rowCount, 1);

    execOk("BEGIN", sess);
    execOk("DELETE FROM items WHERE name = 'apple'", sess);

    // 事务内 apple 消失
    r = execOk("SELECT * FROM items WHERE name = 'apple'", sess);
    ASSERT_EQ(r.rowCount, 0);

    execOk("ROLLBACK", sess);

    // 回滚后 apple 恢复
    r = execOk("SELECT * FROM items WHERE name = 'apple'", sess);
    ASSERT_EQ(r.rowCount, 1);

    // 总行数不变
    r = execOk("SELECT * FROM items", sess);
    ASSERT_EQ(r.rowCount, before);
}

// ════════════════════════════════════════════════════════════════════════════
// 9. 重复 BEGIN 应报错
// ════════════════════════════════════════════════════════════════════════════
static void test_nested_begin() {
    std::cout << "[test_nested_begin]\n";

    Session sess;
    sess.user = "root";
    sess.currentDatabase = "secdb";

    execOk("BEGIN", sess);
    ASSERT_TRUE(!sess.transactionId.empty());

    // 再次 BEGIN 应出错
    auto r = execErr("BEGIN", sess);
    ASSERT_EQ(r.error->code, ErrorCode::TRANSACTION_CONFLICT);

    execOk("ROLLBACK", sess);
}

// ════════════════════════════════════════════════════════════════════════════
// main
// ════════════════════════════════════════════════════════════════════════════
int main() {
    // 清理旧测试数据
    if (fs::exists(DATA_DIR))
        fs::remove_all(DATA_DIR);

    gEng = new DBEngine(DATA_DIR);

    // root 会话直接用空 user（匿名 = 不检查权限），
    // 也可以 CONNECT 后使用 root 用户，两者均可
    rootSess.user = "root";  // root 拥有所有权限

    test_user_management();
    test_authentication();
    test_permissions();
    test_privilege_escalation();
    test_transaction_commit();
    test_transaction_rollback_insert();
    test_transaction_rollback_update();
    test_transaction_rollback_delete();
    test_nested_begin();

    delete gEng;

    std::cout << "\n===========================\n";
    std::cout << "PASSED: " << gPassed << "\n";
    std::cout << "FAILED: " << gFailed << "\n";
    std::cout << "===========================\n";

    return gFailed > 0 ? 1 : 0;
}
