#pragma once

#include "../../types.h"
#include "TransactionManager.h"
#include <string>
#include <map>
#include <vector>

/**
 * Write-Ahead Log (WAL) manager for crash recovery.
 *
 * Every DML operation inside a transaction is appended to
 * dataDir/wal.log BEFORE the actual data file is modified.
 * On startup, recover() reads the log and returns the undo
 * operations for any transactions that had no COMMIT / ROLLBACK
 * record — i.e., transactions that were in-flight when the
 * process crashed.
 *
 * WAL record format (one record per line):
 *
 *   BEGIN|<txId>
 *   INS|<txId>|<db>|<table>|<offset>
 *   UPD|<txId>|<db>|<table>|<offset>|<encoded_record>
 *   DEL|<txId>|<db>|<table>|<offset>|<encoded_record>
 *   CMT|<txId>
 *   RBK|<txId>
 *
 * encoded_record: col1=TYPE:val1;col2=TYPE:val2;...
 *   where special chars in column names and values are
 *   percent-encoded (%25 % | %7C ; %3B = %3D \n %0A).
 */
class WalManager {
public:
    explicit WalManager(const std::string& dataDir);

    // ── logging (call before or immediately after the data operation) ──
    void logBegin   (const std::string& txId);
    void logInsert  (const std::string& txId,
                     const std::string& db, const std::string& table,
                     int64_t offset);
    void logUpdate  (const std::string& txId,
                     const std::string& db, const std::string& table,
                     int64_t offset,
                     const std::map<std::string, FieldValue>& oldRecord);
    void logDelete  (const std::string& txId,
                     const std::string& db, const std::string& table,
                     int64_t offset,
                     const std::map<std::string, FieldValue>& oldRecord);
    void logCommit  (const std::string& txId);
    void logRollback(const std::string& txId);

    /**
     * Scan the WAL and return undo-ops for every incomplete transaction
     * (i.e. transactions with a BEGIN but no CMT/RBK).
     * The undo ops are returned in reverse order ready for replay.
     * Call once at engine startup, before any user SQL executes.
     */
    std::map<std::string, std::vector<UndoOp>> recover();

    /**
     * Remove WAL entries for committed/rolled-back transactions.
     * Call after commit or rollback to keep the file compact.
     */
    void compact();

private:
    std::string walPath_;

    // ── encoding helpers ──────────────────────────────────────────────
    std::string encodeRecord(const std::map<std::string, FieldValue>& rec) const;
    std::map<std::string, FieldValue> decodeRecord(const std::string& s) const;
    std::string encodeVal(const FieldValue& v) const;
    FieldValue  decodeVal(const std::string& s) const;
    std::string pctEncode(const std::string& s) const;
    std::string pctDecode(const std::string& s) const;

    void append(const std::string& line);
};
