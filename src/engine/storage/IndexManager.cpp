#include "IndexManager.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

// FieldValue to index key string (same encoding as before, unchanged)
static std::string fvToKey(const FieldValue& v) {
    if (std::holds_alternative<std::monostate>(v)) return "\x00NULL";
    if (std::holds_alternative<int64_t>(v))
        return "I:" + std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))
        return "D:" + std::to_string(std::get<double>(v));
    if (std::holds_alternative<bool>(v))
        return std::string("B:") + (std::get<bool>(v) ? "1" : "0");
    if (std::holds_alternative<std::string>(v))
        return "S:" + std::get<std::string>(v);
    return "";
}

IndexManager::IndexManager(const std::string& dataDir) : dataDir_(dataDir) {}

std::string IndexManager::cacheKey(const std::string& db, const std::string& tbl,
                                     const std::string& name) const {
    return db + '\t' + tbl + '\t' + name;
}

std::string IndexManager::tixPath(const std::string& db, const std::string& tbl,
                                    const std::string& name) const {
    return dataDir_ + "/" + db + "/" + tbl + "__" + name + ".tix";
}

std::string IndexManager::makeKeyStr(const std::vector<std::string>& cols,
                                      const std::map<std::string, FieldValue>& rec) const {
    std::string key;
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) key += '|';
        auto it = rec.find(cols[i]);
        key += (it != rec.end()) ? fvToKey(it->second) : "\x00NULL";
    }
    return key;
}

std::vector<std::string> IndexManager::listIndexNames(const std::string& db,
                                                        const std::string& tbl) const {
    std::vector<std::string> names;
    fs::path dir = dataDir_ + "/" + db;
    if (!fs::exists(dir)) return names;
    std::string prefix = tbl + "__";
    std::string suffix = ".tix";
    for (const auto& entry : fs::directory_iterator(dir)) {
        std::string fname = entry.path().filename().string();
        if (fname.size() > prefix.size() + suffix.size()
            && fname.substr(0, prefix.size()) == prefix
            && fname.substr(fname.size() - suffix.size()) == suffix) {
            names.push_back(fname.substr(prefix.size(),
                fname.size() - prefix.size() - suffix.size()));
        }
    }
    return names;
}

// ── File format (.tix) ───────────────────────────────────────────────────────
// Line 1 : UNIQUE=0|1
// Line 2 : COLUMNS=col1,col2,...
// Line 3+: <key>\t<off1>,<off2>,...  (B-tree in-order dump)

void IndexManager::loadIndex(const std::string& db, const std::string& tbl,
                               const std::string& name) {
    std::string ck   = cacheKey(db, tbl, name);
    std::string path = tixPath(db, tbl, name);

    IndexEntry entry;
    std::ifstream f(path);
    if (!f) { cache_[ck] = std::move(entry); return; }

    std::string line;
    if (std::getline(f, line) && line.substr(0, 7) == "UNIQUE=")
        entry.unique = (line[7] == '1');
    if (std::getline(f, line) && line.substr(0, 8) == "COLUMNS=") {
        std::stringstream ss(line.substr(8));
        std::string col;
        while (std::getline(ss, col, ','))
            if (!col.empty()) entry.columns.push_back(col);
    }

    // Bulk-insert all key-offset pairs into the B-tree
    while (std::getline(f, line)) {
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string k      = line.substr(0, tab);
        std::string offStr = line.substr(tab + 1);
        std::stringstream ss(offStr);
        std::string tok;
        while (std::getline(ss, tok, ','))
            if (!tok.empty())
                entry.btree.insert(k, std::stoll(tok));
    }
    cache_[ck] = std::move(entry);
}

void IndexManager::saveIndex(const std::string& db, const std::string& tbl,
                               const std::string& name) {
    std::string ck = cacheKey(db, tbl, name);
    auto it = cache_.find(ck);
    if (it == cache_.end()) return;
    const auto& entry = it->second;

    std::ofstream f(tixPath(db, tbl, name), std::ios::trunc);
    f << "UNIQUE=" << (entry.unique ? "1" : "0") << "\n";
    f << "COLUMNS=";
    for (size_t i = 0; i < entry.columns.size(); ++i) {
        if (i > 0) f << ",";
        f << entry.columns[i];
    }
    f << "\n";

    // Dump B-tree in-order (ascending key)
    entry.btree.inorder([&f](const std::string& k, const std::vector<int64_t>& offs) {
        f << k << "\t";
        for (size_t i = 0; i < offs.size(); ++i) {
            if (i > 0) f << ",";
            f << offs[i];
        }
        f << "\n";
    });
}

IndexManager::IndexEntry* IndexManager::getEntry(const std::string& db,
                                                   const std::string& tbl,
                                                   const std::string& name) {
    std::string ck = cacheKey(db, tbl, name);
    if (!cache_.count(ck))
        loadIndex(db, tbl, name);
    auto it = cache_.find(ck);
    return (it != cache_.end()) ? &it->second : nullptr;
}

// ─── DDL ────────────────────────────────────────────────────────────────────

void IndexManager::createIndex(const std::string& db, const std::string& tbl,
                                 const std::string& name,
                                 const std::vector<std::string>& cols, bool unique) {
    std::string ck = cacheKey(db, tbl, name);
    IndexEntry entry;
    entry.unique  = unique;
    entry.columns = cols;
    cache_[ck] = std::move(entry);
    saveIndex(db, tbl, name);
}

void IndexManager::dropIndex(const std::string& db, const std::string& tbl,
                               const std::string& name) {
    std::string ck = cacheKey(db, tbl, name);
    cache_.erase(ck);
    fs::path p = tixPath(db, tbl, name);
    if (fs::exists(p)) fs::remove(p);
}

// ─── DML maintenance ────────────────────────────────────────────────────────

void IndexManager::onInsert(const std::string& db, const std::string& tbl,
                              const std::map<std::string, FieldValue>& record,
                              int64_t offset) {
    for (const auto& name : listIndexNames(db, tbl)) {
        auto* entry = getEntry(db, tbl, name);
        if (!entry) continue;
        std::string k = makeKeyStr(entry->columns, record);
        if (entry->unique && !entry->btree.search(k).empty()) {
            throw DBException(ErrorCode::DUPLICATE_KEY,
                "Duplicate index entry in unique index '" + name + "'");
        }
        entry->btree.insert(k, offset);
        saveIndex(db, tbl, name);
    }
}

void IndexManager::onDelete(const std::string& db, const std::string& tbl,
                              const std::map<std::string, FieldValue>& record,
                              int64_t offset) {
    for (const auto& name : listIndexNames(db, tbl)) {
        auto* entry = getEntry(db, tbl, name);
        if (!entry) continue;
        std::string k = makeKeyStr(entry->columns, record);
        entry->btree.remove(k, offset);
        saveIndex(db, tbl, name);
    }
}

void IndexManager::onUpdate(const std::string& db, const std::string& tbl,
                              const std::map<std::string, FieldValue>& oldRec,
                              const std::map<std::string, FieldValue>& newRec,
                              int64_t offset) {
    for (const auto& name : listIndexNames(db, tbl)) {
        auto* entry = getEntry(db, tbl, name);
        if (!entry) continue;
        std::string oldK = makeKeyStr(entry->columns, oldRec);
        std::string newK = makeKeyStr(entry->columns, newRec);
        if (oldK == newK) continue;
        entry->btree.remove(oldK, offset);
        if (entry->unique && !entry->btree.search(newK).empty()) {
            throw DBException(ErrorCode::DUPLICATE_KEY,
                "Duplicate index entry in unique index '" + name + "'");
        }
        entry->btree.insert(newK, offset);
        saveIndex(db, tbl, name);
    }
}

// ─── Query ──────────────────────────────────────────────────────────────────

std::vector<int64_t> IndexManager::lookup(const std::string& db, const std::string& tbl,
                                            const std::string& name, const FieldValue& key) {
    auto* entry = getEntry(db, tbl, name);
    if (!entry) return {};
    return entry->btree.search(fvToKey(key));
}

std::vector<IndexDefinition> IndexManager::listIndexes(const std::string& db,
                                                         const std::string& tbl) {
    std::vector<IndexDefinition> result;
    for (const auto& name : listIndexNames(db, tbl)) {
        auto* entry = getEntry(db, tbl, name);
        if (!entry) continue;
        IndexDefinition def;
        def.name    = name;
        def.columns = entry->columns;
        def.unique  = entry->unique;
        result.push_back(std::move(def));
    }
    return result;
}
