#pragma once

#include "../../types.h"
#include "../parser/AST.h"
#include <map>
#include <string>

class ExprEvaluator {
public:
    /**
     * 对单条记录求值 WHERE 表达式
     * @param expr  WHERE 子句根节点
     * @param row   字段名 → 值 的映射（由 RecordManager 提供）
     *              支持 "column"、"table.column"、"alias.column" 等形式的键
     * @returns     该行是否满足条件
     */
    bool evaluate(const WhereExpr& expr,
                  const std::map<std::string, FieldValue>& row);

private:
    // 从 row 中查找列值，支持 qualified lookup
    // 优先查找 tableAlias.column，其次 column（如果唯一）
    FieldValue lookupColumn(const std::string& tableAlias,
                            const std::string& columnName,
                            const std::map<std::string, FieldValue>& row) const;
};
