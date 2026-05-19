# DBMS-5 — 轻量级关系型数据库管理系统

一个用 **C++17** 从零实现的关系型 DBMS，支持完整 SQL 子集、B 树索引、ACID 事务与崩溃恢复（WAL）、用户权限体系。

---

## 最终交付材料

课程验收相关报告和汇报材料统一放在 `docs/final/`：

| 材料 | 路径 |
|---|---|
| 交付文档目录 | `docs/final/00_交付文档目录.md` |
| 启动报告 / 关闭报告 / 需求分析 / 设计 / 测试 / 用户手册 | `docs/final/` |
| 汇报 PPT | `docs/final/DBMS-5_项目汇报.pptx` |
| Qt 演示录屏 | `demos/qt_admin_workflow_demo_fullscreen.mp4` |

---

## 功能特性

| 类别 | 功能 |
|------|------|
| **DDL** | `CREATE / DROP DATABASE`、`CREATE / DROP / ALTER TABLE`、`CREATE / DROP INDEX` |
| **DML** | `INSERT`、`SELECT`（隐式内连接 / `INNER JOIN ... ON` / WHERE / GROUP BY / ORDER BY / LIMIT / DISTINCT / 聚合）、`UPDATE`、`DELETE` |
| **约束** | `PRIMARY KEY`、`NOT NULL`、`UNIQUE`、`DEFAULT`、`AUTO_INCREMENT`、`FOREIGN KEY`（级联检查） |
| **索引** | B 树索引（最小度 T=4，CLRS 实现）、唯一索引、多列复合索引 |
| **事务** | `BEGIN / COMMIT / ROLLBACK`、内存 Undo Log 运行时回滚 |
| **崩溃恢复** | Write-Ahead Log（WAL）；进程崩溃后重启自动 Undo 未提交事务 |
| **认证与权限** | `CONNECT` 用户登录、`CREATE / DROP USER`、`GRANT / REVOKE`（SELECT / INSERT / UPDATE / DELETE / ALL） |
| **CLI** | 交互式 REPL；分号 `;` 或空行均可提交语句；`\help` / `\quit` 等元命令 |

---

## 快速上手

### 1. 编译

**Windows（MSYS2 UCRT64，推荐）**

```powershell
cmake --preset windows-msys2-ucrt64
cmake --build --preset windows-msys2-ucrt64
ctest --preset windows-msys2-ucrt64 --output-on-failure
```

> Windows 下推荐使用 preset。它会通过 `tools/mingw-gpp-wrapper.cmd` 固定 MSYS2 UCRT64 的 PATH，并把 CMake/Ninja 传给 `g++` 的反斜杠路径转换为正斜杠路径，避免 `cc1plus.exe` 因找不到运行时 DLL 或路径格式异常而无诊断失败。若 MSYS2 安装在非默认位置，可设置 `DBMS_MINGW_GXX` 指向实际的 `g++.exe`。

**Linux / macOS**

```bash
cmake -B build
cmake --build build
```

> 依赖：CMake ≥ 3.25（使用 preset），GCC ≥ 11（C++17）或 MSVC 2019+。项目本身最低 CMake 版本仍为 3.15。

---

### 2. 启动 REPL

```bash
./build/dbms          # Linux/macOS
.\build\dbms.exe      # Windows（需先将 MSYS2 加入 PATH）
```

启动后显示提示符 `dbms> `。

---

### 3. 登录

**所有 SQL 操作均需先登录。** 系统初始化时自动创建超级用户 `root`（密码 `root`）。

```sql
CONNECT 'root' IDENTIFIED BY 'root';
```

或使用元命令：

```
\connect root root
```

登录后提示符变为 `dbms [当前数据库]> `。

---

### 4. 完整操作流程示例

```sql
-- ① 登录
CONNECT 'root' IDENTIFIED BY 'root';

-- ② 创建数据库并切换
CREATE DATABASE shop;
USE shop;

-- ③ 创建表
CREATE TABLE users (
    id    INTEGER PRIMARY KEY AUTO_INCREMENT,
    name  VARCHAR(64) NOT NULL,
    email VARCHAR(128) UNIQUE,
    age   INTEGER DEFAULT 0
);

CREATE TABLE orders (
    id      INTEGER PRIMARY KEY AUTO_INCREMENT,
    user_id INTEGER,
    amount  DOUBLE NOT NULL,
    CONSTRAINT fk_user FOREIGN KEY (user_id) REFERENCES users(id)
);

-- ④ 插入数据
INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30);
INSERT INTO users (name, email, age) VALUES ('Bob',   'bob@example.com',   25);
INSERT INTO orders (user_id, amount) VALUES (1, 199.99);
INSERT INTO orders (user_id, amount) VALUES (1, 88.00);
INSERT INTO orders (user_id, amount) VALUES (2, 320.50);

-- ⑤ 查询
SELECT * FROM users;
SELECT u.name, o.amount
  FROM users u, orders o
  WHERE u.id = o.user_id AND o.amount > 100
  ORDER BY o.amount DESC;

-- ⑥ 事务
BEGIN;
UPDATE users SET age = 31 WHERE name = 'Alice';
DELETE FROM orders WHERE amount < 90;
COMMIT;   -- 或 ROLLBACK;

-- ⑦ 索引
CREATE INDEX idx_age ON users(age);
DROP INDEX idx_age ON users;

-- ⑧ 创建普通用户并授权
CREATE USER alice IDENTIFIED BY 'alice123';
GRANT SELECT, INSERT ON shop.users TO alice;
REVOKE INSERT ON shop.users FROM alice;

-- ⑨ 以普通用户登录
CONNECT 'alice' IDENTIFIED BY 'alice123';
USE shop;
SELECT * FROM users;   -- OK（有 SELECT 权限）
```

---

## CLI 使用说明

### 提示符

| 提示符 | 含义 |
|--------|------|
| `dbms> ` | 已登录，未选择数据库 |
| `dbms [mydb]> ` | 已选择数据库 mydb |
| `dbms [mydb*]> ` | 活跃事务中 |
| `    -> ` | 多行输入继续中 |

### 语句提交方式

- **分号结尾**：输入语句并以 `;` 结束，回车后立即执行。
- **空行提交**：输入多行语句（不写 `;`），再按一次回车（空行）即可执行。
- 两种方式可混用。

### 元命令（以 `\` 开头）

| 命令 | 说明 |
|------|------|
| `\help` | 显示帮助 |
| `\quit` 或 `\q` | 退出 DBMS |
| `\status` | 查看当前用户、数据库、事务状态 |
| `\databases` | 列出所有数据库（等同 `SHOW DATABASES`） |
| `\tables` | 列出当前库的表（等同 `SHOW TABLES`） |
| `\use <db>` | 切换数据库（等同 `USE <db>`） |
| `\desc <table>` | 查看表结构（等同 `DESCRIBE <table>`） |
| `\connect <user> <password>` | 登录（等同 `CONNECT ... IDENTIFIED BY ...`） |
| `\history` | 查看命令历史 |
| `\clear` | 清屏 |
| `source <文件路径>` | 从 SQL 文件批量执行语句 |

---

## SQL 语法参考

### 认证

```sql
CONNECT 'username' IDENTIFIED BY 'password';
```

### 数据库

```sql
CREATE DATABASE mydb;
DROP DATABASE mydb;
USE mydb;
SHOW DATABASES;
BACKUP DATABASE mydb TO '/path/to/backup';
```

### 表

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
DESCRIBE users;       -- 或 \desc users
SHOW TABLES;

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

### 数据操作（DML）

```sql
-- 插入
INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30);

-- 查询
SELECT * FROM users WHERE age > 18 ORDER BY name ASC LIMIT 10;
SELECT name, COUNT(*) AS cnt FROM users GROUP BY name HAVING cnt > 1;
SELECT DISTINCT name FROM users;

-- 多表连接（隐式内连接）
SELECT u.name, o.amount
  FROM users u, orders o
  WHERE u.id = o.user_id AND o.amount > 100;

-- 显式 INNER JOIN
SELECT u.name, o.amount
  FROM users u INNER JOIN orders o ON u.id = o.user_id;

-- 当前尚不支持 LEFT/RIGHT/FULL OUTER JOIN 或 IN (SELECT ...) 子查询

-- 聚合函数：COUNT / SUM / AVG / MAX / MIN
SELECT COUNT(*), AVG(age), MAX(score) FROM users;

-- 更新
UPDATE users SET age = 31, score = 9.5 WHERE name = 'Alice';

-- 删除
DELETE FROM users WHERE age < 18;
```

### 事务（TCL）

```sql
BEGIN;
INSERT INTO users (name, email) VALUES ('Bob', 'bob@example.com');
UPDATE users SET age = 25 WHERE name = 'Bob';
COMMIT;      -- 提交
-- 或
ROLLBACK;    -- 回滚
```

### 用户与权限（DCL）

```sql
-- root 用户操作
CREATE USER alice IDENTIFIED BY 'password123';
GRANT SELECT, INSERT ON mydb.users TO alice;
REVOKE INSERT ON mydb.users FROM alice;
DROP USER alice;
SHOW USERS;
```

> **权限说明**：`ALL` 授予完整权限；可按库/表精细授权；`root` 用户拥有所有权限，不可删除。所有非 `CONNECT` 操作都需要先登录。

---

## 项目结构

```
DBMS-5/
├── src/
│   ├── main.cpp                          # 程序入口
│   ├── types.h                           # 公共类型（FieldValue、QueryResult …）
│   ├── cli/
│   │   ├── Repl.cpp/.h                   # 交互式 REPL（分号/空行双触发）
│   │   ├── Formatter.cpp/.h              # 结果表格 + 颜色格式化
│   │   ├── MetaCommands.cpp/.h           # 元命令（\help、\quit、\status …）
│   │   └── Session.h                     # CLISession（历史、页大小、引擎会话）
│   ├── engine/
│   │   ├── DBEngine.cpp/.h               # 顶层引擎（解析 → 执行 → 返回结果）
│   │   ├── lexer/
│   │   │   └── Lexer.cpp/.h              # 词法分析器（Token 流）
│   │   ├── parser/
│   │   │   ├── Parser.cpp/.h             # 递归下降解析器
│   │   │   └── AST.h                     # 抽象语法树节点定义
│   │   ├── executor/
│   │   │   ├── Executor.cpp/.h           # SQL 执行器（DDL/DML/TCL/DCL + 权限检查）
│   │   │   └── ExprEvaluator.cpp/.h      # WHERE / HAVING 表达式求值
│   │   └── storage/
│   │       ├── DatabaseManager.cpp/.h    # 数据库（目录）管理
│   │       ├── TableManager.cpp/.h       # 表定义（.tbl 文件）持久化
│   │       ├── RecordManager.cpp/.h      # 行记录（堆文件 .dat）读写
│   │       ├── IndexManager.cpp/.h       # 索引文件（.tix）管理
│   │       ├── BTreeIndex.cpp/.h         # B 树索引（T=4，CLRS 第 18 章）
│   │       ├── TransactionManager.cpp/.h # 内存 Undo Log（运行时回滚）
│   │       ├── WalManager.cpp/.h         # WAL 日志写入与崩溃恢复
│   │       └── UserManager.cpp/.h        # 用户/权限管理（.usr 文件）
│   └── ui/
│       └── README.md                     # Qt Widgets 简单前端规划与接入说明
├── tests/
│   ├── test_lexer.cpp                    # 词法单元测试（63 项）
│   ├── test_integration.cpp              # 集成测试（62 项）
│   ├── test_security.cpp                 # 权限安全测试（95 项）
│   ├── test_comprehensive.cpp            # 综合行为测试（257 项）
│   └── test_cli.cpp                      # CLI 输入行为测试（11 项）
├── data/                                 # 运行时数据目录（自动创建）
├── docs/
│   └── GitHub提交策略.md                 # 分支、commit、PR、合并规范
└── CMakeLists.txt
```

### 当前一周开发重点

- **联表查询**：补齐逗号 `FROM` 隐式内连接、显式 `INNER JOIN ... ON`、表别名、限定字段名、字段歧义报错和集成测试。
- **DBMS 基建扩展**：`IN (SELECT ...)`、外连接、存储过程和触发器列入后端后续规划，避免前端本周范围被挤占。
- **Qt 前端**：使用简单 Qt Widgets UI，包含 SQL 输入、执行按钮、结果表格、错误提示，直接复用 `DBEngine::execute()` 和 `QueryResult`。
- **协作方式**：所有成员从 `feature/*` 分支提交 PR 到 `develop`，通过构建、相关测试和至少 1 人 Review 后合并。

---

## 运行测试

```powershell
# Windows（先设置 PATH）
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
cmake --build build

.\build\test_lexer.exe           # 词法测试：       63 passed
.\build\test_integration.exe     # 集成测试：       62 passed
.\build\test_security.exe        # 权限安全测试：   95 passed
.\build\test_comprehensive.exe   # 综合行为测试：  257 passed
.\build\test_cli.exe             # CLI 输入测试：   11 passed
```

```bash
# Linux/macOS
./build/test_lexer
./build/test_integration
./build/test_security
./build/test_comprehensive
./build/test_cli
```

---

## 核心模块说明

### B 树索引（BTreeIndex）

- 最小度 **T = 4**，每个非根节点持有 3–7 个键，根节点持有 1–7 个键。
- 键类型 `std::string`，值类型 `std::vector<int64_t>`（行在 `.dat` 文件中的偏移量）。
- 支持**同键多偏移**（非唯一索引）；唯一索引由调用方在插入前检查。
- 完整实现 CLRS 第 18 章算法：`splitChild`、`insertNonFull`、`deleteKey`（情况 1/2/3）、`fill`、`borrowFromPrev`/`Next`、`merge`。
- **持久化**：`IndexManager` 在关闭时通过 `inorder()` 将 B 树按序转储到 `.tix` 文件，下次启动时批量插入重建。

### Write-Ahead Log（WAL）

WAL 文件位于 `<dataDir>/wal.log`，每行一条记录：

```
BEGIN|<txId>
INS|<txId>|<db>|<table>|<offset>
UPD|<txId>|<db>|<table>|<offset>|<encoded_old_record>
DEL|<txId>|<db>|<table>|<offset>|<encoded_old_record>
CMT|<txId>
RBK|<txId>
```

- **写入时机**：INSERT 在数据写入后记 INS；UPDATE / DELETE 在修改**前**记旧值（UPD/DEL）。
- **崩溃恢复**：引擎启动时调用 `WalManager::recover()`，对所有只有 `BEGIN` 没有 `CMT/RBK` 的事务按逆序执行 Undo，然后调用 `compact()` 清理 WAL。
- **编码**：记录字段使用 percent-encoding，保证含 `|`、`\n` 等特殊字符的值不破坏行格式。

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

### 权限模型

- **root** 用户自动创建，持有 `ALL ON *.*`，权限不可撤销。
- `CONNECT` 是唯一允许匿名执行的操作；其余所有操作（DDL / DML / TCL / DCL）均需登录。
- 权限粒度：`ALL / SELECT / INSERT / UPDATE / DELETE`，可精细到 `<database>.<table>` 级别。

---

## 文件格式

| 文件 | 说明 |
|------|------|
| `<db>/<table>.dat` | 堆文件，`\n` 分隔的 CSV 行记录（软删除置空行） |
| `<db>/<table>.tbl` | 表定义（列名、类型、约束、外键） |
| `<db>/<table>_<idx>.tix` | 索引文件，头部含 UNIQUE/COLUMNS 元数据，数据行为 `key\toff1,off2,...` |
| `users.usr` | 用户账户与权限序列化 |
| `wal.log` | WAL 日志（正常退出后已 compact，崩溃时保留未提交条目） |

---

## 设计决策

- **单文件堆存储**：记录以追加方式写入 `.dat`，删除仅将对应行置空（软删除），UPDATE 追加新行并清除旧偏移，减少随机写。
- **B 树 vs 哈希索引**：B 树天然支持范围扫描，与磁盘顺序读取相性好；T=4 在内存测试中单节点容量适中。
- **WAL 优先于内存 Undo Log**：内存 Undo Log 在进程崩溃时丢失，WAL 提供持久化的 Undo 记录，是关系型数据库可靠性的基础。
- **percent-encoding**：WAL 字段分隔符为 `|`，为保证含 `|`、换行等特殊字符的字符串值不破坏解析，对所有字符串字段做 percent-encoding。
- **空行触发执行**：REPL 支持分号或空行两种语句提交方式，降低交互输入门槛。
