#include "WalManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>

// ============================================================
// Construction
// ============================================================

WalManager::WalManager(const std::string& dataDir)
    : walPath_(dataDir + "/wal.log")
{}

// ============================================================
// Percent-encoding helpers
// ============================================================

std::string WalManager::pctEncode(const std::string& s) const {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c == '%' || c == '|' || c == ';' || c == '=' || c == '\n' || c == '\r') {
            out += '%';
            char buf[3];
            std::snprintf(buf, sizeof(buf), "%02X", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::string WalManager::pctDecode(const std::string& s) const {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            unsigned int v = 0;
            std::sscanf(s.c_str() + i + 1, "%02X", &v);
            out += static_cast<char>(v);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// ============================================================
// FieldValue encoding
// ============================================================

std::string WalManager::encodeVal(const FieldValue& v) const {
    if (std::holds_alternative<std::monostate>(v)) return "N";
    if (std::holds_alternative<int64_t>(v))
        return "I:" + std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v)) {
        std::ostringstream os;
        os << std::setprecision(17) << std::get<double>(v);
        return "D:" + os.str();
    }
    if (std::holds_alternative<bool>(v))
        return std::string("B:") + (std::get<bool>(v) ? "1" : "0");
    if (std::holds_alternative<std::string>(v))
        return "S:" + pctEncode(std::get<std::string>(v));
    return "N";
}

FieldValue WalManager::decodeVal(const std::string& s) const {
    if (s.empty() || s == "N") return std::monostate{};
    if (s.size() >= 2 && s[1] == ':') {
        char t = s[0];
        std::string body = s.substr(2);
        switch (t) {
            case 'I': try { return static_cast<int64_t>(std::stoll(body)); } catch(...) {}
                      return std::monostate{};
            case 'D': try { return std::stod(body); } catch(...) {}
                      return std::monostate{};
            case 'B': return body == "1";
            case 'S': return pctDecode(body);
        }
    }
    return std::monostate{};
}

// ============================================================
// Record encoding: col1=TYPE:val1;col2=TYPE:val2;...
// ============================================================

std::string WalManager::encodeRecord(const std::map<std::string, FieldValue>& rec) const {
    std::string out;
    bool first = true;
    for (const auto& [col, val] : rec) {
        if (!first) out += ';';
        first = false;
        out += pctEncode(col) + '=' + encodeVal(val);
    }
    return out;
}

std::map<std::string, FieldValue>
WalManager::decodeRecord(const std::string& s) const {
    std::map<std::string, FieldValue> rec;
    if (s.empty()) return rec;
    std::istringstream ss(s);
    std::string field;
    while (std::getline(ss, field, ';')) {
        auto eq = field.find('=');
        if (eq == std::string::npos) continue;
        std::string col = pctDecode(field.substr(0, eq));
        std::string val = field.substr(eq + 1);
        rec[col] = decodeVal(val);
    }
    return rec;
}

// ============================================================
// Append one line to WAL (with immediate flush for durability)
// ============================================================

void WalManager::append(const std::string& line) {
    std::ofstream f(walPath_, std::ios::app);
    if (f) {
        f << line << '\n';
        f.flush();
    }
}

// ============================================================
// Logging API
// ============================================================

void WalManager::logBegin(const std::string& txId) {
    append("BEGIN|" + txId);
}

void WalManager::logInsert(const std::string& txId,
                             const std::string& db, const std::string& table,
                             int64_t offset) {
    append("INS|" + txId + '|' + db + '|' + table + '|'
           + std::to_string(offset));
}

void WalManager::logUpdate(const std::string& txId,
                             const std::string& db, const std::string& table,
                             int64_t offset,
                             const std::map<std::string, FieldValue>& oldRecord) {
    append("UPD|" + txId + '|' + db + '|' + table + '|'
           + std::to_string(offset) + '|' + encodeRecord(oldRecord));
}

void WalManager::logDelete(const std::string& txId,
                             const std::string& db, const std::string& table,
                             int64_t offset,
                             const std::map<std::string, FieldValue>& oldRecord) {
    append("DEL|" + txId + '|' + db + '|' + table + '|'
           + std::to_string(offset) + '|' + encodeRecord(oldRecord));
}

void WalManager::logCommit(const std::string& txId) {
    append("CMT|" + txId);
}

void WalManager::logRollback(const std::string& txId) {
    append("RBK|" + txId);
}

// ============================================================
// Crash recovery
// ============================================================

std::map<std::string, std::vector<UndoOp>> WalManager::recover() {
    std::ifstream f(walPath_);
    if (!f) return {};   // no WAL file → nothing to recover

    // txId → ordered undo ops
    std::map<std::string, std::vector<UndoOp>> undoMap;
    // txId → whether it was committed/rolled-back
    std::map<std::string, bool> completed;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;

        // Split on '|' — first field is the record type
        std::vector<std::string> parts;
        {
            std::istringstream ss(line);
            std::string tok;
            while (std::getline(ss, tok, '|'))
                parts.push_back(tok);
        }
        if (parts.empty()) continue;

        const std::string& type = parts[0];

        if (type == "BEGIN" && parts.size() >= 2) {
            undoMap[parts[1]]; // create entry
        } else if (type == "INS" && parts.size() >= 5) {
            UndoOp op;
            op.type   = UndoOpType::INSERT_UNDO;
            op.db     = parts[2];
            op.table  = parts[3];
            op.offset = std::stoll(parts[4]);
            undoMap[parts[1]].push_back(std::move(op));
        } else if ((type == "UPD" || type == "DEL") && parts.size() >= 6) {
            // Remaining '|' delimiters might appear inside the encoded record
            // (they were percent-encoded), so parts[5] is the full encoded record
            // but the split above ate them — reconstruct from position of 6th '|'
            std::string encodedRec;
            {
                // Find 5th '|' in original line, rest is the encoded record
                size_t count = 0, pos = 0;
                while (pos < line.size()) {
                    if (line[pos] == '|') {
                        ++count;
                        if (count == 5) { encodedRec = line.substr(pos + 1); break; }
                    }
                    ++pos;
                }
            }
            UndoOp op;
            op.type      = (type == "UPD") ? UndoOpType::UPDATE_UNDO
                                            : UndoOpType::DELETE_UNDO;
            op.db        = parts[2];
            op.table     = parts[3];
            op.offset    = std::stoll(parts[4]);
            op.oldRecord = decodeRecord(encodedRec);
            undoMap[parts[1]].push_back(std::move(op));
        } else if (type == "CMT" && parts.size() >= 2) {
            completed[parts[1]] = true;
        } else if (type == "RBK" && parts.size() >= 2) {
            completed[parts[1]] = true;
        }
    }

    // Return only incomplete transactions (no CMT/RBK)
    std::map<std::string, std::vector<UndoOp>> result;
    for (auto& [txId, ops] : undoMap) {
        if (!completed.count(txId) && !ops.empty()) {
            // Reverse so the caller can replay in undo order
            std::reverse(ops.begin(), ops.end());
            result[txId] = std::move(ops);
        }
    }
    return result;
}

// ============================================================
// Compact: keep only records for still-active transactions
// ============================================================

void WalManager::compact() {
    std::ifstream in(walPath_);
    if (!in) return;

    // Collect all lines
    std::vector<std::string> lines;
    {
        std::string l;
        while (std::getline(in, l)) lines.push_back(l);
    }
    in.close();

    // Determine which txIds are completed
    std::map<std::string, bool> completed;
    for (const auto& line : lines) {
        if (line.size() >= 4 && (line.substr(0, 4) == "CMT|" || line.substr(0, 4) == "RBK|")) {
            auto sep = line.find('|');
            if (sep != std::string::npos)
                completed[line.substr(sep + 1)] = true;
        }
    }

    // Re-write only lines for incomplete transactions
    std::ofstream out(walPath_, std::ios::trunc);
    for (const auto& line : lines) {
        if (line.empty()) continue;
        auto sep = line.find('|');
        if (sep == std::string::npos) continue;
        // Extract txId (second pipe-field for DML, first for CMT/BEGIN)
        std::string type = line.substr(0, sep);
        std::string rest = line.substr(sep + 1);
        std::string txId;
        if (type == "BEGIN" || type == "CMT" || type == "RBK") {
            txId = rest.substr(0, rest.find('|'));
        } else {
            txId = rest.substr(0, rest.find('|'));
        }
        if (!completed.count(txId))
            out << line << '\n';
    }
}
