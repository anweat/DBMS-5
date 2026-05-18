# Qt 前端模块说明

`src/ui/` 放置 Qt Widgets 前端代码。界面只通过 `QtSessionAdapter` 调用
`DBEngine::execute(sql, session)`，不直接读写存储文件，也不在 UI 层重新实现 SQL 解析。

## 目标页面

- 左侧对象树：展示数据库、表、列、索引占位、用户和权限，并提供常用对象右键操作。
- 表数据编辑：双击表后加载 `SELECT *`，支持新增行、删除行、保存单元格编辑。
- SQL 输入区：使用多行输入控件编写 SQL，保留联表查询示例。
- 结果表格：将 `QueryResult.columns` 和 `QueryResult.rows` 渲染为只读结果。
- 管理面板：跟随左侧树聚焦当前库/表，按 Object / Structure / Users 拆分，生成并执行建库建表、删除、列、索引、用户、权限相关基础 SQL。Object 页用字段表格展示当前表结构，也可通过 `+` / `-` 编辑字段行，再用 `Apply Columns` 统一提交列变更。
- 状态/错误提示：显示 affectedRows、错误信息、当前会话状态。

## 建议文件

```text
src/ui/
├── MainWindow.h
├── MainWindow.cpp
├── QtSessionAdapter.h
├── QtSessionAdapter.cpp
├── CatalogTypes.h
├── DatabaseTreePanel.h/.cpp # 对象树：数据库/表/列/用户/权限
├── TableEditorPanel.h/.cpp  # 表数据新增、删除、保存编辑
├── AdminPanel.h/.cpp        # Schema / User / Privilege SQL 生成
├── SessionPanel.h/.cpp       # 组员1：连接/会话状态
├── SqlEditorPanel.h/.cpp     # 组员2：SQL 输入、执行按钮、历史/示例
├── ResultTablePanel.h/.cpp   # 组员3：QueryResult 表格渲染
├── StatusMetaPanel.h/.cpp    # 组员4：错误、耗时、affectedRows、库表元信息
└── README.md
```

## 组件边界

| 组件 | 负责内容 | 不负责 |
|---|---|---|
| `SessionPanel` | 登录输入、当前用户/数据库/事务状态显示 | 不直接执行 SQL |
| `DatabaseTreePanel` | 展示 catalog 快照、双击表发出打开请求 | 不直接查询后端 |
| `TableEditorPanel` | 将表数据编辑转换为 INSERT/UPDATE/DELETE | 不做权限绕过或存储层修改 |
| `AdminPanel` | 根据对象树焦点生成 DDL/DCL SQL：库、表、删除、列、索引、用户、权限 | 不绕过 SQL 权限检查 |
| `SqlEditorPanel` | SQL 编辑、执行按钮、历史/示例 SQL | 不解析 SQL，不持有 DBEngine |
| `ResultTablePanel` | 渲染 `QueryResult::columns` 和 `QueryResult::rows` | 不访问存储文件 |
| `StatusMetaPanel` | 渲染错误、耗时、affectedRows、库表/表结构信息 | 不做后端权限判断 |
| `MainWindow` | 统一布局、信号槽连接、调用 `QtSessionAdapter` | 不实现具体组件内部 UI |

## 组员交付接口

| 组件 | 必须暴露的接口 | 必须处理的状态 |
|---|---|---|
| `SessionPanel` | `setSessionInfo(user, database, inTransaction)`、`connectRequested(user, password)` 信号 | 未登录、已登录、无当前库、事务中 |
| `SqlEditorPanel` | `executeRequested(sql)` 信号、`setExamples(QStringList)` | 空 SQL 禁用执行、多行 SQL、历史 SQL |
| `ResultTablePanel` | `renderResult(const QueryResult&)`、`clear()` | SELECT 空结果、普通结果、大结果列宽 |
| `StatusMetaPanel` | `showResultMeta(const QueryResult&)`、`showError(message)` | 成功、错误、affectedRows、elapsedMs |
| `MainWindow` | 接收全部信号并唯一调用 `QtSessionAdapter::executeSql()` | 统一错误分发、菜单/布局、Demo 流程 |

## 推荐布局

- 左侧：`SessionPanel` + `DatabaseTreePanel`。
- 中间标签页：`TableEditorPanel`、`ResultTablePanel`、`SqlEditorPanel`。
- 右侧标签页：`StatusMetaPanel`、`AdminPanel`。

## 构建方式

Qt 前端是可选目标，不影响默认 CLI/测试构建。

```bat
cmake --preset windows-msys2-ucrt64
cmake --build --preset windows-msys2-ucrt64
```

MSYS2 UCRT64 环境安装 Qt Widgets：

```bat
C:\msys64\usr\bin\pacman.exe -S --needed mingw-w64-ucrt-x86_64-qt6-base
```

启用 GUI 目标：

```bat
cmake --preset windows-msys2-ucrt64-qt
cmake --build --preset windows-msys2-ucrt64-qt
run_qt.bat
```

## 约束

- Qt 层不得直接读写 `data/` 下的数据文件。
- Qt 层不得重新解析 SQL，应统一调用 `DBEngine::execute()`。
- 表格编辑以第一列作为 UPDATE/DELETE 条件，适合当前基础演示；真实主键识别可后续从元信息增强。
- 非 root 用户刷新 catalog 时看不到 `SHOW USERS` 的内容，权限仍由后端统一判断。
