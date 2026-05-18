#include "ExprEvaluator.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <regex>

// ============================================================
// 辅助：FieldValue 比较
// ============================================================

static double toNumber(const std::string& s) {
    try { return std::stod(s); }
    catch (...) { return 0.0; }
}

// 将 FieldValue 统一转换为 double（用于数值比较）
static double toDouble(const FieldValue& v) {
    if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))  return std::get<double>(v);
    if (std::holds_alternative<bool>(v))    return std::get<bool>(v) ? 1.0 : 0.0;
    if (std::holds_alternative<std::string>(v)) return toNumber(std::get<std::string>(v));
    return 0.0;
}

static std::string toString(const FieldValue& v) {
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    if (std::holds_alternative<int64_t>(v))     return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))      return std::to_string(std::get<double>(v));
    if (std::holds_alternative<bool>(v))        return std::get<bool>(v) ? "true" : "false";
    return "";
}

// -1 / 0 / +1 比较，NULL 视为 false
static int compareValues(const FieldValue& a, const FieldValue& b) {
    bool aNul = std::holds_alternative<std::monostate>(a);
    bool bNul = std::holds_alternative<std::monostate>(b);
    if (aNul || bNul) return -2;

    bool aStr = std::holds_alternative<std::string>(a);
    bool bStr = std::holds_alternative<std::string>(b);
    if (aStr && bStr) {
        const auto& sa = std::get<std::string>(a);
        const auto& sb = std::get<std::string>(b);
        if (sa < sb) return -1;
        if (sa > sb) return  1;
        return 0;
    }

    double da = toDouble(a), db = toDouble(b);
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

// LIKE 模式匹配（% = 任意字符串，_ = 任意单字符）
static bool likeMatch(const std::string& text, const std::string& pattern) {
    // 转换为正则
    std::string re = "^";
    for (char c : pattern) {
        if (c == '%') re += ".*";
        else if (c == '_') re += ".";
        else if (std::string("^$.|?*+()[]{}\\").find(c) != std::string::npos)
            re += std::string("\\") + c;
        else re += c;
    }
    re += "$";
    try {
        std::regex rx(re, std::regex::icase);
        return std::regex_match(text, rx);
    } catch (...) {
        return false;
    }
}

// ============================================================
// ExprEvaluator::lookupColumn
// ============================================================

FieldValue ExprEvaluator::lookupColumn(const std::string& tableAlias,
                                        const std::string& columnName,
                                        const std::map<std::string, FieldValue>& row) const {
    // 1. 如果有 table/alias 前缀，优先查找 "alias.column"
    if (!tableAlias.empty()) {
        std::string qualKey = tableAlias + "." + columnName;
        auto it = row.find(qualKey);
        if (it != row.end())
            return it->second;
    }

    // 2. 查找无前缀的 "column"（单表或唯一列）
    auto it = row.find(columnName);
    if (it != row.end())
        return it->second;

    // 3. 如果未找到，尝试在所有 qualified keys 中查找匹配的列名
    //    如果找到多个，说明有歧义，返回 NULL
    //    如果找到一个，使用它
    const FieldValue* found = nullptr;
    int count = 0;
    for (const auto& [key, val] : row) {
        auto dotPos = key.find('.');
        if (dotPos != std::string::npos) {
            std::string colPart = key.substr(dotPos + 1);
            if (colPart == columnName) {
                found = &val;
                ++count;
                if (count > 1)
                    break; // 歧义，停止查找
            }
        }
    }

    if (count == 1 && found)
        return *found;

    // 未找到或歧义 → 返回 NULL
    return std::monostate{};
}

// ============================================================
// ExprEvaluator::evaluate
// ============================================================

bool ExprEvaluator::evaluate(const WhereExpr& expr,
                              const std::map<std::string, FieldValue>& row) {
    switch (expr.kind) {
        case WhereExpr::Kind::LITERAL:
            // 字面量单独出现不应发生，作 true 处理
            return !std::holds_alternative<std::monostate>(expr.value);

        case WhereExpr::Kind::COLUMN_REF:
            // 不应单独出现
            return false;

        case WhereExpr::Kind::LOGICAL:
            switch (expr.op) {
                case ExprOp::AND:
                    return evaluate(*expr.left, row) && evaluate(*expr.right, row);
                case ExprOp::OR:
                    return evaluate(*expr.left, row) || evaluate(*expr.right, row);
                case ExprOp::NOT:
                    return !evaluate(*expr.left, row);
                default:
                    return false;
            }

        case WhereExpr::Kind::COMPARISON: {
            // 取左侧列值（使用 qualified lookup）
            FieldValue lval = std::monostate{};
            if (expr.left && expr.left->kind == WhereExpr::Kind::COLUMN_REF) {
                lval = lookupColumn(expr.left->tableAlias, expr.left->columnName, row);
            }

            // IS NULL / IS NOT NULL
            if (expr.op == ExprOp::IS_NULL)
                return std::holds_alternative<std::monostate>(lval);
            if (expr.op == ExprOp::IS_NOT_NULL)
                return !std::holds_alternative<std::monostate>(lval);

            // IN
            if (expr.op == ExprOp::IN) {
                for (const auto& v : expr.inList) {
                    int cmp = compareValues(lval, v);
                    if (cmp == 0) return true;
                }
                return false;
            }

            // LIKE
            if (expr.op == ExprOp::LIKE) {
                if (!expr.right) return false;
                FieldValue rval;
                if (expr.right->kind == WhereExpr::Kind::LITERAL) {
                    rval = expr.right->value;
                } else if (expr.right->kind == WhereExpr::Kind::COLUMN_REF) {
                    rval = lookupColumn(expr.right->tableAlias, expr.right->columnName, row);
                }
                std::string text    = toString(lval);
                std::string pattern = toString(rval);
                return likeMatch(text, pattern);
            }

            // 普通比较（支持 column-to-column）
            if (!expr.right) return false;
            FieldValue rval;
            if (expr.right->kind == WhereExpr::Kind::LITERAL) {
                rval = expr.right->value;
            } else if (expr.right->kind == WhereExpr::Kind::COLUMN_REF) {
                rval = lookupColumn(expr.right->tableAlias, expr.right->columnName, row);
            }

            int cmp = compareValues(lval, rval);
            if (cmp == -2) return false; // NULL 参与比较 → false
            switch (expr.op) {
                case ExprOp::EQ:  return cmp == 0;
                case ExprOp::NEQ: return cmp != 0;
                case ExprOp::LT:  return cmp <  0;
                case ExprOp::LE:  return cmp <= 0;
                case ExprOp::GT:  return cmp >  0;
                case ExprOp::GE:  return cmp >= 0;
                default:          return false;
            }
        }
    }
    return false;
}
