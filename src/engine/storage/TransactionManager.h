#pragma once

#include "../../types.h"
#include <string>
#include <vector>
#include <map>

// 撤销操作类型
enum class UndoOpType {
    INSERT_UNDO,  // 回滚时：软删除该行（status=1）
    UPDATE_UNDO,  // 回滚时：恢复旧行数据
    DELETE_UNDO   // 回滚时：恢复被删除的行（status=0 + 旧数据）
};

struct UndoOp {
    UndoOpType                          type;
    std::string                         db;
    std::string                         table;
    int64_t                             offset;    // 物理文件偏移
    std::map<std::string, FieldValue>   oldRecord; // UPDATE/DELETE undo 时有效
};

class TransactionManager {
public:
    /** 开始一个新事务，返回事务 ID */
    std::string begin();

    /** 该事务 ID 是否正在活跃 */
    bool isActive(const std::string& txId) const;

    // ── 记录 DML 的撤销日志 ─────────────────────────────────────────
    void logInsert(const std::string& txId,
                   const std::string& db, const std::string& table,
                   int64_t offset);

    void logUpdate(const std::string& txId,
                   const std::string& db, const std::string& table,
                   int64_t offset,
                   const std::map<std::string, FieldValue>& oldRecord);

    void logDelete(const std::string& txId,
                   const std::string& db, const std::string& table,
                   int64_t offset,
                   const std::map<std::string, FieldValue>& oldRecord);

    /** 获取撤销日志（用于 ROLLBACK） */
    const std::vector<UndoOp>& getUndoLog(const std::string& txId) const;

    /** 提交/回滚后清除日志 */
    void clear(const std::string& txId);

private:
    std::map<std::string, std::vector<UndoOp>> undoLogs_;
    int64_t nextId_ = 0;

    static const std::vector<UndoOp> emptyLog_;
};
