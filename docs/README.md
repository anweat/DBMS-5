# DBMS-5 文档总目录

本文档用于统一说明项目文档、演示材料和发布交付物的位置。最终验收优先阅读 `docs/final/`，早期规划和技术参考作为补充材料保留。

## 1. 最终交付材料

| 编号 | 文档 | 说明 |
|---|---|---|
| 00 | [最终交付文档目录](final/00_交付文档目录.md) | 最终报告、PPT、脚本、视频的入口目录 |
| 01 | [启动报告](final/01_启动报告.md) | 项目目标、阶段计划、成员分工 |
| 02 | [关闭报告](final/02_关闭报告.md) | 最终功能、成果总结、成员心得 |
| 03 | [需求分析报告](final/03_需求分析报告.md) | 功能需求、用例、非功能需求 |
| 04 | [设计报告](final/04_设计报告.md) | 架构、流程、类图、界面设计 |
| 05 | [测试报告](final/05_测试报告.md) | 测试范围、测试用例、结果和风险 |
| 06 | [用户使用手册](final/06_用户使用手册.md) | 构建、启动、CLI 和 Qt 使用说明 |
| 07 | [验收演示方案](final/07_验收演示方案.md) | 演示表、SQL 顺序、Qt 镜头、成员讲解分工 |
| 08 | [录屏演示脚本](final/08_录屏演示脚本.md) | 录屏操作步骤、旁白和预期输出 |
| 09 | [发布说明](final/09_发布说明.md) | 发布包内容、验证结果和复现入口 |
| PPT | [DBMS-5_项目汇报.pptx](final/DBMS-5_项目汇报.pptx) | 按成员分工组织的最终汇报 PPT |

## 2. 阶段规划文档

| 编号 | 文档 | 说明 |
|---|---|---|
| P01 | [开发规划](planning/01_开发规划.md) | 12 周计划、阶段目标和风险应对 |
| P02 | [技术选型文档](planning/02_技术选型文档.md) | C++17、CMake、Qt、文件存储等选型说明 |
| P03 | [分工计划书](planning/03_分工计划书.md) | 成员职责、RACI 矩阵和任务拆分 |
| P04 | [接口文档](planning/04_接口文档.md) | 模块接口、数据结构和调用约定 |
| P05 | [原始需求分析说明书](planning/05_原始需求分析说明书.md) | 早期需求分析材料 |
| P06 | [课程需求原件](planning/06_课程需求原件_DBMS.pdf) | 课程项目需求 PDF 原件 |

## 3. 技术参考文档

| 编号 | 文档 | 说明 |
|---|---|---|
| R01 | [后端查询调用链路](reference/01_后端查询调用链路.md) | DBEngine、Parser、Executor、QueryResult 的完整执行链路 |
| R02 | [GitHub 提交策略](reference/02_GitHub提交策略.md) | 分支、Commit、PR、Review 和合并规范 |
| R03 | [GitHub 仓库保护设置](reference/03_GitHub仓库保护设置.md) | main/develop 保护规则和 CODEOWNERS 建议 |
| UI | [Qt 前端模块说明](../src/ui/README.md) | Qt Widgets 组件边界和接入方式 |

## 4. 演示与发布材料

| 类型 | 路径 | 用途 |
|---|---|---|
| Qt 全屏录屏 | `../demos/qt_admin_workflow_demo_fullscreen.mp4` | 最终界面演示视频 |
| Qt 普通录屏 | `../demos/qt_admin_workflow_demo.mp4` | 备用界面演示视频 |
| SQL 全覆盖脚本 | `../demos/full_acceptance_demo.sql` | 覆盖 DDL、DML、JOIN、事务、索引、权限 |
| CLI 逐句脚本 | `../demos/cli_step_demo.sql` | 配合 PPT 逐句讲 SQL 和结果 |
| 环境恢复脚本 | `../demos/reset_qt_demo.sql` | 演示前清理临时库表和用户 |
| PPT 截图资产 | `final/assets/` | PPT 中使用的 Qt 和 CLI 截图 |

## 5. 发布口径

- 最终验收以 `docs/final/00_交付文档目录.md` 为入口。
- 演示前先执行 `demos/reset_qt_demo.sql` 清理环境。
- 汇报按 `docs/final/DBMS-5_项目汇报.pptx` 的成员分工顺序讲解。
- 需要复现完整功能时，执行 `demos/full_acceptance_demo.sql`；需要配合 PPT 逐句解释时，执行 `demos/cli_step_demo.sql`。
