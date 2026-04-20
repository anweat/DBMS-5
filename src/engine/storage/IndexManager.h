#pragma once

#include "../../types.h"
#include "BTreeIndex.h"
#include <string>
#include <vector>
#include <map>

class IndexManager {
public:
    explicit IndexManager(const std::string& dataDir);

    // DDL
    void createIndex(const std::string& db, const std::string& table,
                     const std::string& indexName,
                     const std::vector<std::string>& columns, bool unique);
    void dropIndex(const std::string& db, const std::string& table,
                   const std::string& indexName);

    // DML maintenance — called by Executor after each insert/delete/update
    void onInsert(const std::string& db, const std::string& table,
                  const std::map<std::string, FieldValue>& record, int64_t offset);
    void onDelete(const std::string& db, const std::string& table,
                  const std::map<std::string, FieldValue>& record, int64_t offset);
    void onUpdate(const std::string& db, const std::string& table,
                  const std::map<std::string, FieldValue>& oldRec,
                  const std::map<std::string, FieldValue>& newRec,
                  int64_t offset);

    // Query
    std::vector<int64_t> lookup(const std::string& db, const std::string& table,
                                 const std::string& indexName, const FieldValue& key);
    std::vector<IndexDefinition> listIndexes(const std::string& db,
                                              const std::string& table);

private:
    std::string dataDir_;

    struct IndexEntry {
        bool                     unique = false;
        std::vector<std::string> columns;
        BTreeIndex               btree;   // B-tree backed index
    };

    // cache key: "db\tbl\tidxname"
    std::map<std::string, IndexEntry> cache_;

    std::string cacheKey(const std::string& db, const std::string& tbl,
                          const std::string& name) const;
    std::string tixPath(const std::string& db, const std::string& tbl,
                         const std::string& name) const;
    std::string makeKeyStr(const std::vector<std::string>& cols,
                            const std::map<std::string, FieldValue>& record) const;

    // Scan directory for all .tix files of a table; returns index names
    std::vector<std::string> listIndexNames(const std::string& db,
                                             const std::string& tbl) const;
    // Load index from .tix file into cache
    void loadIndex(const std::string& db, const std::string& tbl,
                   const std::string& name);
    // Persist index from cache to .tix file
    void saveIndex(const std::string& db, const std::string& tbl,
                   const std::string& name);
    // Get or load index entry from cache
    IndexEntry* getEntry(const std::string& db, const std::string& tbl,
                          const std::string& name);
};
