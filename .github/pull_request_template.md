## 改动范围

-

## 测试

- [ ] `cmake -B build`
- [ ] `cmake --build build`
- [ ] `ctest --test-dir build --output-on-failure`

## 自查

- [ ] 没有直接读写不属于本模块的核心数据文件
- [ ] 涉及 Parser / AST / QueryResult 的改动已同步接口文档
- [ ] 涉及 Qt UI 的改动仍通过 `DBEngine::execute()` 调用引擎
- [ ] 联表查询改动覆盖字段限定名、别名、歧义字段和空结果

## 风险

-

## 关联 Issue

Closes #
