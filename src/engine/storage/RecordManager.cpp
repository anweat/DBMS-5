#include "RecordManager.h"
#include "TableManager.h"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <stdexcept>

namespace fs = std::filesystem;

// ============================================================
// 固定大小记录格式
// 记录布局：[1 byte: status(0=active,1=deleted)]
//           对每列：[1 byte: null_flag] [N bytes: data]
// 列数据大小：
//   INTEGER -> 8  (int64_t, LE)
//   DOUBLE  -> 8  (double)
//   BOOL    -> 1
//   VARCHAR(N) -> 4(uint32_t actual_len) + N(data padded)
//   DATETIME-> 20 (fixed string)
// ============================================================

static size_t colDataSize(const ColumnDefinition& col) {
    switch (col.type) {
        case FieldType::INTEGER:  return 8;
        case FieldType::DOUBLE:   return 8;
        case FieldType::BOOL:     return 1;
        case FieldType::VARCHAR:  return 4 + static_cast<size_t>(col.length > 0 ? col.length : 255);
        case FieldType::DATETIME: return 20;
    }
    return 8;
}

static size_t calcRecordSize(const TableDefinition& def) {
    size_t sz = 1; // status byte
    for (const auto& col : def.columns)
        sz += 1 + colDataSize(col);
    return sz;
}

static void serializeField(const ColumnDefinition& col, const FieldValue& val, char* buf) {
    size_t dsz = colDataSize(col);
    if (std::holds_alternative<std::monostate>(val)) {
        buf[0] = 1; // null
        std::memset(buf + 1, 0, dsz);
        return;
    }
    buf[0] = 0; // not null
    char* d = buf + 1;
    switch (col.type) {
        case FieldType::INTEGER: {
            int64_t v = std::get<int64_t>(val);
            std::memcpy(d, &v, 8);
            break;
        }
        case FieldType::DOUBLE: {
            double v = std::get<double>(val);
            std::memcpy(d, &v, 8);
            break;
        }
        case FieldType::BOOL: {
            d[0] = std::get<bool>(val) ? 1 : 0;
            break;
        }
        case FieldType::VARCHAR: {
            const std::string& s = std::get<std::string>(val);
            size_t cap = dsz - 4;
            uint32_t len = static_cast<uint32_t>(s.size() < cap ? s.size() : cap);
            std::memcpy(d, &len, 4);
            std::memset(d + 4, 0, cap);
            std::memcpy(d + 4, s.c_str(), len);
            break;
        }
        case FieldType::DATETIME: {
            const std::string& s = std::get<std::string>(val);
            std::memset(d, 0, 20);
            size_t len = s.size() < 19 ? s.size() : 19;
            std::memcpy(d, s.c_str(), len);
            break;
        }
    }
}

static FieldValue deserializeField(const ColumnDefinition& col, const char* buf) {
    if (static_cast<unsigned char>(buf[0])) return std::monostate{};
    const char* d = buf + 1;
    switch (col.type) {
        case FieldType::INTEGER: {
            int64_t v;
            std::memcpy(&v, d, 8);
            return v;
        }
        case FieldType::DOUBLE: {
            double v;
            std::memcpy(&v, d, 8);
            return v;
        }
        case FieldType::BOOL:
            return static_cast<bool>(d[0] != 0);
        case FieldType::VARCHAR: {
            uint32_t len;
            std::memcpy(&len, d, 4);
            return std::string(d + 4, len);
        }
        case FieldType::DATETIME:
            return std::string(d); // null-terminated string
    }
    return std::monostate{};
}

static std::string trdPath(const std::string& dataDir,
                            const std::string& db,
                            const std::string& table) {
    return (fs::path(dataDir) / db / (table + ".trd")).string();
}

// ============================================================
// RecordManager 实现
// ============================================================

RecordManager::RecordManager(const std::string& dataDir, TableManager& tblMgr)
    : dataDir_(dataDir), tblMgr_(tblMgr) {}

int64_t RecordManager::insert(const std::string& database,
                               const std::string& table,
                               const std::map<std::string, FieldValue>& record) {
    auto defOpt = tblMgr_.describeTable(database, table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND, "Unknown table '" + table + "'");
    const auto& def = *defOpt;
    size_t recSz = calcRecordSize(def);
    std::vector<char> buf(recSz, 0);
    buf[0] = 0; // active
    size_t offset = 1;
    for (const auto& col : def.columns) {
        auto it = record.find(col.name);
        FieldValue val = (it != record.end()) ? it->second : std::monostate{};
        serializeField(col, val, buf.data() + offset);
        offset += 1 + colDataSize(col);
    }

    auto path = trdPath(dataDir_, database, table);
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (!f)
        throw DBException(ErrorCode::FILE_IO_ERROR,
                          "Cannot open record file: " + path);
    int64_t pos = static_cast<int64_t>(fs::file_size(path));
    f.write(buf.data(), static_cast<std::streamsize>(recSz));
    lastOffset_ = pos;
    return pos;
}

std::vector<std::pair<int64_t, Row>> RecordManager::scanWithOffsets(
    const std::string& database, const std::string& table) {
    std::vector<std::pair<int64_t, Row>> result;
    auto defOpt = tblMgr_.describeTable(database, table);
    if (!defOpt) return result;
    const auto& def = *defOpt;
    size_t recSz = calcRecordSize(def);
    if (recSz == 0) return result;

    auto path = trdPath(dataDir_, database, table);
    if (!fs::exists(path)) return result;
    std::ifstream f(path, std::ios::binary);
    if (!f) return result;

    std::vector<char> buf(recSz);
    int64_t pos = 0;
    while (f.read(buf.data(), static_cast<std::streamsize>(recSz))) {
        if (static_cast<unsigned char>(buf[0]) == 0) { // not deleted
            Row row;
            size_t off = 1;
            for (const auto& col : def.columns) {
                row.push_back(deserializeField(col, buf.data() + off));
                off += 1 + colDataSize(col);
            }
            result.emplace_back(pos, std::move(row));
        }
        pos += static_cast<int64_t>(recSz);
    }
    return result;
}

std::vector<Row> RecordManager::scan(const std::string& database,
                                      const std::string& table) {
    auto withOffsets = scanWithOffsets(database, table);
    std::vector<Row> result;
    result.reserve(withOffsets.size());
    for (auto& [off, row] : withOffsets)
        result.push_back(std::move(row));
    return result;
}

void RecordManager::update(const std::string& database, const std::string& table,
                            int64_t offset,
                            const std::map<std::string, FieldValue>& record) {
    auto defOpt = tblMgr_.describeTable(database, table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND, "Unknown table '" + table + "'");
    const auto& def = *defOpt;
    size_t recSz = calcRecordSize(def);
    std::vector<char> buf(recSz, 0);
    buf[0] = 0;
    size_t off = 1;
    for (const auto& col : def.columns) {
        auto it = record.find(col.name);
        FieldValue val = (it != record.end()) ? it->second : std::monostate{};
        serializeField(col, val, buf.data() + off);
        off += 1 + colDataSize(col);
    }
    auto path = trdPath(dataDir_, database, table);
    std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) throw DBException(ErrorCode::FILE_IO_ERROR, "Cannot open record file");
    f.seekp(offset);
    f.write(buf.data(), static_cast<std::streamsize>(recSz));
}

void RecordManager::remove(const std::string& database, const std::string& table,
                            int64_t offset) {
    auto path = trdPath(dataDir_, database, table);
    std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) throw DBException(ErrorCode::FILE_IO_ERROR, "Cannot open record file");
    f.seekp(offset);
    char deleted = 1;
    f.write(&deleted, 1);
}

void RecordManager::replaceAll(const std::string& database, const std::string& table,
                               const std::vector<std::map<std::string, FieldValue>>& records) {
    auto defOpt = tblMgr_.describeTable(database, table);
    if (!defOpt)
        throw DBException(ErrorCode::TABLE_NOT_FOUND, "Unknown table '" + table + "'");

    auto path = trdPath(dataDir_, database, table);
    std::ofstream truncate(path, std::ios::binary | std::ios::trunc);
    if (!truncate)
        throw DBException(ErrorCode::FILE_IO_ERROR,
                          "Cannot rewrite record file: " + path);
    truncate.close();

    lastOffset_ = -1;
    for (const auto& record : records)
        insert(database, table, record);
}
