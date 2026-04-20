#include "TransactionManager.h"

const std::vector<UndoOp> TransactionManager::emptyLog_;

std::string TransactionManager::begin() {
    std::string txId = "tx_" + std::to_string(++nextId_);
    undoLogs_[txId];  // 创建空日志
    return txId;
}

bool TransactionManager::isActive(const std::string& txId) const {
    return !txId.empty() && undoLogs_.count(txId) > 0;
}

void TransactionManager::logInsert(const std::string& txId,
                                    const std::string& db,
                                    const std::string& table,
                                    int64_t offset) {
    if (!isActive(txId)) return;
    UndoOp op;
    op.type   = UndoOpType::INSERT_UNDO;
    op.db     = db;
    op.table  = table;
    op.offset = offset;
    undoLogs_[txId].push_back(std::move(op));
}

void TransactionManager::logUpdate(const std::string& txId,
                                    const std::string& db,
                                    const std::string& table,
                                    int64_t offset,
                                    const std::map<std::string, FieldValue>& oldRecord) {
    if (!isActive(txId)) return;
    UndoOp op;
    op.type      = UndoOpType::UPDATE_UNDO;
    op.db        = db;
    op.table     = table;
    op.offset    = offset;
    op.oldRecord = oldRecord;
    undoLogs_[txId].push_back(std::move(op));
}

void TransactionManager::logDelete(const std::string& txId,
                                    const std::string& db,
                                    const std::string& table,
                                    int64_t offset,
                                    const std::map<std::string, FieldValue>& oldRecord) {
    if (!isActive(txId)) return;
    UndoOp op;
    op.type      = UndoOpType::DELETE_UNDO;
    op.db        = db;
    op.table     = table;
    op.offset    = offset;
    op.oldRecord = oldRecord;
    undoLogs_[txId].push_back(std::move(op));
}

const std::vector<UndoOp>& TransactionManager::getUndoLog(const std::string& txId) const {
    auto it = undoLogs_.find(txId);
    if (it == undoLogs_.end()) return emptyLog_;
    return it->second;
}

void TransactionManager::clear(const std::string& txId) {
    undoLogs_.erase(txId);
}
