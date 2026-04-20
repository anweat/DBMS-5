# DBMS-5 — 轻量级关系型数据库管理系统

一个用 **C++17** 从零实现的关系型 DBMS，支持完整 SQL 子集、B 树索引、事务与崩溃恢复（WAL）。

---

## 功能特性

| 类别 | 功能 |
|------|------|
| **DDL** | CREATE / DROP DATABASE、CREATE / DROP / ALTER TABLE、CREATE / DROP INDEX |
| **DML** | INSERT、SELECT（JOIN / WHERE / GROUP BY / ORDER BY / LIMIT / DISTINCT / 聚合函数）、UPDATE、DELETE |
| **约束** | PRIMARY KEY、NOT NULL、UNIQUE、DEFAULT、AUTO_INCREMENT、FOREIGN KEY（级联检查） |
| **索引** | B 树索引（最小度 T=4，CLRS 实现）、唯一索引、多列复合索引 |
| **事务** | BEGIN / COMMIT / ROLLBACK、基于内存 Undo Log 的回滚 |
| **崩溃恢复** | Write-Ahead Log（WAL）；进程崩溃后重启自动 Undo 未提交事务 |
| **用户权限** | CREATE / DROP USER、GRANT / REVOKE（SELECT / INSERT / UPDATE / DELETE / ALL） |
| **CLI** | 交互式 REPL；支持多行语句、`.exit` / `.help` 元命令 |

---

## 项目结构

```
DBMS-5/
├── src/
│   ├── main.cpp                        # 程序入口
│   ├── types.h                         # 公共类型（FieldValue、QueryResult …）
│   ├── cli/
│   │   ├── Repl.cpp/.h                 # 交互式 REPL
│   │   ├── Formatter.cpp/.h            # 结果表格格式化
│   │   └── MetaCommands.cpp/.h         # 元命令（.help、.exit …）
│   └── engine/
│       ├── DBEngine.cpp/.h             # 顶层引擎（解析 → 执行 → 结果）
│       ├── lexer/
│       │   └── Lexer.cpp/.h            # 词法分析器（Token 流）
│       ├── parser/
│       │   ├── Parser.cpp/.h           # 递归下降解析器
│       │   └── AST.h                   # 抽象语法树节点定义
│       ├── executor/
│       │   ├── Executor.cpp/.h         # SQL 执行器（DDL / DML / TCL / DCL）
│       │   └── ExprEvaluator.cpp/.h    # WHERE / HAVING 表达式求值
│       └── storage/
│           ├── DatabaseManager.cpp/.h  # 数据库（目录）管理
│           ├── TableManager.cpp/.h     # 表定义（.tbl 文件）持久化
│           ├── RecordManager.cpp/.h    # 行记录（堆文件 .dat）读写
│           ├── IndexManager.cpp/.h     # 索引文件（.tix）管理，调用 BTreeIndex
│           ├── BTreeIndex.cpp/.h       # B 树索引（T=4，CLRS 算法）
│           ├── TransactionManager.cpp/.h # 内存 Undo Log（运行时回滚）
│           ├── WalManager.cpp/.h       # WAL 日志写入与崩溃恢复
│           └── UserManager.cpp/.h      # 用户/权限管理（.usr 文件）
├── tests/
│   ├── test_lexer.cpp                  # 词法单元测试（63 项）
│   ├── test_integration.cpp            # 集成测试（62 项）
│   └── test_security.cpp               # 权限安全测试（95 项）
├── data/                               # 运行时数据目录（自动创建）
└── CMakeLists.txt
```

---

## 核心模块说明

### B 树索引（BTreeIndex）

- 最小度 **T = 4**，每个非根节点持有 3–7 个键，根节点持有 1–7 个键。
- 键类型 `std::string`，值类型 `std::vector<int64_t>`（行在 .dat 文件中的偏移量）。
- 支持**同键多偏移**（非唯一索引）；唯一索引由调用方在插入前检查。
- 完整实现 CLRS 第 18 章算法：`splitChild`、`insertNonFull`、`deleteKey`（情况 1/2/3）、`fill`、`borrowFromPrev`/`Next`、`merge`。
- **持久化**：`IndexManager` 在关闭时通过 `inorder()` 将 B 树按序转储到 `.tix` 文件，下次启动时批量插入重建。

### Write-Ahead Log（WAL）

WAL 文件位于 `<dataDir>/wal.log`，每行一条记录：

```
BEGIN|<txId>
INS|<txId>|<db>|<table>|<offset>
UPD|<txId>|<db>|<table>|<offset>|<encoded_record>
DEL|<txId>|<db>|<table>|<offset>|<encoded_record>
CMT|<txId>
RBK|<txId>
```

- **写入时机**：INSERT 在数据写入后记 INS；UPDATE / DELETE 在数据修改**前**记旧值（UPD/DEL）。
- **崩溃恢复**：引擎启动时调用 `WalManager::recover()`，对所有只有 BEGIN 没有 CMT/RBK 的事务按逆序执行 Undo，然后调用 `compact()` 清理 WAL。
- **编码**：记录字段使用 percent-encoding，保证含 `|` `\n` 等特殊字符的字符串值不会破坏行格式。

### 事务管理

| 操作 | 内存 Undo Log | WAL |
|------|--------------|-----|
| `BEGIN` | 分配 txId | 写 BEGIN 行 |
| `INSERT` | 记录 offset | 写 INS 行（数据写入后） |
| `UPDATE` | 记录旧行 | 写 UPD 行（数据修改前） |
| `DELETE` | 记录旧行 | 写 DEL 行（数据删除前） |
| `COMMIT` | 清除 Undo Log | 写 CMT 行 + compact |
| `ROLLBACK` | 逆序回放 Undo | 写 RBK 行 + compact |
| **崩溃重启** | —（内存已丢失） | WAL recover → Undo 回放 |

---

## 编译与运行

### 依赖

- CMake ≥ 3.15
- GCC ≥ 11（C++17）或 MSVC 2019+
- **Windows**：推荐 [MSYS2 UCRT64](https://www.msys2.org/)（GCC 15.2.0 已验证）

### 构建（Windows / MSYS2）

```powershell
# 设置 MSYS2 编译器路径
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH

# 配置 + 编译
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER="C:/msys64/ucrt64/bin/g++.exe"
cmake --build build
```

### 构建（Linux / macOS）

```bash
cmake -B build
cmake --build build
```

### 运行 REPL

```bash
./build/dbms          # Linux/macOS
.\build\dbms.exe      # Windows
```

### 运行测试

```bash
.\build\test_lexer.exe          # 词法测试：63 passed
.\build\test_integration.exe    # 集成测试：62 passed
.\build\test_security.exe       # 安全测试：95 passed
```

---

## SQL 语法参考

### 数据库操作

```sql
CREATE DATABASE mydb;
DROP DATABASE mydb;
USE mydb;
SHOW DATABASES;
```

### 表操作

```sql
CREATE TABLE users (
    id      INTEGER PRIMARY KEY AUTO_INCREMENT,
    name    VARCHAR(64) NOT NULL,
    email   VARCHAR(128) UNIQUE,
    age     INTEGER DEFAULT 0,
    score   DOUBLE,
    active  BOOL DEFAULT TRUE
);

DROP TABLE users;
DESCRIBE users;
SHOW TABLES;

-- 修改表结构
ALTER TABLE users ADD COLUMN memo VARCHAR(256);
ALTER TABLE users DROP COLUMN memo;
ALTER TABLE users MODIFY COLUMN name VARCHAR(128) NOT NULL;
ALTER TABLE users RENAME TO members;
```

### 外键

```sql
CREATE TABLE orders (
    id      INTEGER PRIMARY KEY AUTO_INCREMENT,
    user_id INTEGER,
    amount  DOUBLE,
    CONSTRAINT fk_user FOREIGN KEY (user_id) REFERENCES users(id)
);
```

### 索引

```sql
CREATE INDEX idx_name ON users(name);
CREATE UNIQUE INDEX idx_email ON users(email);
DROP INDEX idx_name ON users;
```

### 数据操作

```sql
-- 插入
INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30);

-- 查询
SELECT * FROM users WHERE age > 18 ORDER BY name ASC LIMIT 10;
SELECT name, COUNT(*) AS cnt FROM users GROUP BY name HAVING cnt > 1;
SELECT DISTINCT name FROM users;

-- 多表查询（内连接）
SELECT u.name, o.amount
FROM users u, orders o
WHERE u.id = o.user_id AND o.amount > 100;

-- 聚合函数：COUNT, SUM, AVG, MAX, MIN
SELECT COUNT(*), AVG(age), MAX(score) FROM users;

-- 更新
UPDATE users SET age = 31, score = 9.5 WHERE name = 'Alice';

-- 删除
DELETE FROM users WHERE age < 18;
```

### 事务

```sql
BEGIN;
INSERT INTO users (name, email) VALUES ('Bob', 'bob@example.com');
UPDATE users SET age = 25 WHERE name = 'Bob';
COMMIT;     -- 或 ROLLBACK;
```

### 用户与权限

```sql
CREATE USER alice IDENTIFIED BY 'password123';
GRANT SELECT, INSERT ON mydb.users TO alice;
REVOKE INSERT ON mydb.users FROM alice;
DROP USER alice;
SHOW USERS;
```

---

## 文件格式

| 文件 | 说明 |
|------|------|
| `<db>/<table>.dat` | 堆文件，每行 `\n` 分隔的 CSV 格式行记录（软删除用空行） |
| `<db>/<table>.tbl` | 表定义（列名、类型、约束、外键） |
| `<db>/<table>_<idx>.tix` | 索引文件，头部含 UNIQUE/COLUMNS 元数据，数据行为 `key\toff1,off2,...` |
| `users.usr` | 用户账户与权限序列化 |
| `wal.log` | WAL 日志（进程正常退出后已 compact，崩溃时保留未提交条目） |

---

## 设计决策

- **单文件堆存储**：记录以追加方式写入 `.dat`，删除仅将对应行置空（软删除），UPDATE 追加新行并将旧偏移条目清空，减少随机写。
- **B 树 vs 哈希索引**：B 树天然支持范围扫描，且与磁盘顺序读取相性好；T=4 在内存测试中单节点容量适中。
- **WAL 先于 Undo Log 的原因**：内存 Undo Log 在进程崩溃时丢失，WAL 提供持久化的 Redo/Undo 记录，是关系型数据库可靠性的基础。
- **percent-encoding**：WAL 字段分隔符为 `|`，为保证含 `|`、换行等字符的字符串值不破坏解析，对所有字符串字段做 percent-encoding。

---

## 测试说明

| 测试套件 | 覆盖内容 | 用例数 |
|----------|----------|--------|
| `test_lexer` | Token 类型、关键字识别、数字/字符串字面量、边界 | 63 |
| `test_integration` | DDL/DML/WHERE/ORDER/GROUP BY/聚合/约束/索引/外键/ALTER | 62 |
| `test_security` | 用户创建、GRANT/REVOKE、权限隔离、越权访问拒绝 | 95 |
