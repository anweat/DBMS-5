# GitHub 提交与合并策略

本文档用于规范小组成员提交代码、发起 PR、Review 和合并，避免联表查询和 Qt 前端并行开发时互相覆盖。

## 1. 分支模型

```
main
  └── develop
        ├── feature/join-parser
        ├── feature/join-executor
        ├── feature/qt-ui
        ├── feature/qt-engine-adapter
        └── docs/sprint-github-policy
```

| 分支 | 用途 | 规则 |
|---|---|---|
| `main` | 最终稳定版本 | 禁止直接 push，只接受从 `develop` 合并 |
| `develop` | 日常集成版本 | 通过 PR 合并，保持可编译 |
| `feature/*` | 新功能开发 | 每个功能一个分支 |
| `fix/*` | Bug 修复 | 修复完成后 PR 到 `develop` |
| `docs/*` | 文档修改 | 文档独立 PR，避免混入功能代码 |
| `test/*` | 测试补充 | 可与对应功能 PR 合并，也可独立 PR |

## 2. Commit Message 规范

格式：

```text
type(scope): 简短描述
```

示例：

```text
feat(parser): 支持联表查询表别名
feat(executor): 实现两表等值连接
feat(ui): 添加 Qt 查询结果表格
fix(executor): 修复同名字段歧义判断
docs(readme): 更新 12 周开发计划
test(join): 增加三表联查集成测试
```

常用 `type`：

| 类型 | 用途 |
|---|---|
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `docs` | 文档 |
| `test` | 测试 |
| `refactor` | 不改变行为的重构 |
| `build` | CMake、构建脚本、依赖配置 |
| `chore` | 维护性修改 |

## 3. PR 规范

每个 PR 应尽量只解决一个明确问题，例如：

- `feature/join-parser` 只改 Parser/AST 和解析测试
- `feature/join-executor` 只改 Executor/ExprEvaluator 和联表执行测试
- `feature/qt-ui` 只改 Qt UI 骨架和展示逻辑
- `feature/qt-engine-adapter` 只做 Qt 调用引擎和渲染 `QueryResult`

PR 描述必须包含：

```markdown
## 改动范围
-

## 测试
- [ ] cmake --build build
- [ ] 相关测试命令：

## 风险
-

## 关联 Issue
Closes #
```

## 4. 合并前检查

- 本地能编译：`cmake --build build`
- 后端功能改动必须运行相关测试
- 联表查询至少覆盖：
  - 两表等值连接
  - 表别名
  - 限定字段名，例如 `u.id`
  - 字段歧义报错
  - 空结果
- Qt 前端至少确认：
  - 窗口能启动
  - SQL 能执行
  - SELECT 结果能显示为表格
  - 错误信息能显示
- 涉及文档、启动方式、目录结构、接口变化时同步更新 README 或对应文档
- GitHub Actions 的 build/test 检查必须通过后才能合并

## 5. Review 与合并

- 每个 PR 至少 1 人 Review 后合并
- Parser 和 Executor 互相依赖的 PR，应由对方负责人参与 Review
- Qt 前端 PR 至少由赵杰雄或王凯确认没有绕过 `DBEngine`
- 合并方式优先使用 squash merge，保持 `develop` 历史清晰
- 合并后删除已完成的 feature 分支
- 对 `main` 和 `develop` 开启 branch protection 后，禁止绕过 PR 直接提交

## 6. 冲突处理

- 合并前先从 `develop` 更新自己的分支
- 只解决自己改动范围内的冲突
- 不随意格式化无关文件
- 发现接口冲突时先在 PR 评论中说明，由赵杰雄统一决定接口走向

## 7. GitHub 保护规则设置

在 GitHub 仓库页面进入 `Settings -> Branches -> Add branch protection rule`，分别给 `main` 和 `develop` 添加规则：

| 规则项 | main | develop |
|---|---|---|
| Require a pull request before merging | 开启 | 开启 |
| Required approvals | 至少 1 人 | 至少 1 人 |
| Dismiss stale pull request approvals when new commits are pushed | 开启 | 开启 |
| Require review from Code Owners | 开启 | 开启 |
| Require status checks to pass before merging | 开启 | 开启 |
| Required status check | `build-and-test` | `build-and-test` |
| Require branches to be up to date before merging | 开启 | 建议开启 |
| Restrict who can push to matching branches | 只允许管理员/负责人 | 只允许管理员/负责人 |
| Allow force pushes | 关闭 | 关闭 |
| Allow deletions | 关闭 | 关闭 |

> 当前环境没有可用的 `gh` CLI，GitHub 连接器也没有成功初始化，所以远端保护规则需要由仓库管理员在网页上执行。仓库内已经加入 PR 模板、CODEOWNERS 和 CI workflow，便于保护规则直接引用。
