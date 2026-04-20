#pragma once

#include "../../types.h"
#include <string>
#include <vector>
#include <map>

class TableManager;

class RecordManager {
public:
    RecordManager(const std::string& dataDir, TableManager& tblMgr);

    /** 插入一行，返回该行在 .trd 文件中的物理字节偏移 */
    int64_t insert(const std::string& database, const std::string& table,
                   const std::map<std::string, FieldValue>& record);

    /** 全表顺序扫描，跳过已软删除行 */
    std::vector<Row> scan(const std::string& database, const std::string& table);

    /** 全表扫描，同时返回每行的物理偏移（用于 UPDATE / DELETE）*/
    std::vector<std::pair<int64_t, Row>> scanWithOffsets(
        const std::string& database, const std::string& table);

    /** 按物理偏移原地更新一行 */
    void update(const std::string& database, const std::string& table,
                int64_t offset, const std::map<std::string, FieldValue>& record);

    /** 按物理偏移标记软删除 */
    void remove(const std::string& database, const std::string& table,
                int64_t offset);

    /** 返回最近一次 insert 的物理偏移 */
    int64_t lastInsertOffset() const { return lastOffset_; }

private:
    std::string   dataDir_;
    TableManager& tblMgr_;
    int64_t       lastOffset_ = -1;
};
