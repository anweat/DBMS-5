# Qt 前端模块说明

`src/ui/` 用于放置简单 Qt Widgets 前端代码。本周目标是完成可演示的图形界面，不改变 DBMS 核心引擎。

前端开发拆为 4 个组员组件 + 1 个主窗口总装。组员只交付独立 QWidget/接口；主窗口由负责人统一布局和接线。

## 目标页面

- 登录/连接信息区：复用现有 `CONNECT` 语句或会话状态。
- SQL 输入区：使用多行输入控件编写 SQL。
- 执行按钮：点击后调用 `DBEngine::execute(sql, session)`。
- 结果表格：将 `QueryResult.columns` 和 `QueryResult.rows` 渲染为表格。
- 状态/错误提示：显示执行耗时、影响行数、错误信息。

## 建议文件

```text
src/ui/
├── MainWindow.h
├── MainWindow.cpp
├── QtSessionAdapter.h
├── QtSessionAdapter.cpp
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

- 左侧窄栏：`SessionPanel` + 元信息/状态入口。
- 中上区域：`SqlEditorPanel`。
- 中下区域：`ResultTablePanel`。
- 底部状态栏或右侧面板：`StatusMetaPanel`。

## 约束

- Qt 层不得直接读写 `data/` 下的数据文件。
- Qt 层不得重新解析 SQL，应统一调用 `DBEngine::execute()`。
- UI 保持简单清晰，优先保证联表查询演示可用。
