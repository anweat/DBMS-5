#pragma once

#include "../../types.h"
#include <string>
#include <vector>

class DatabaseManager {
public:
    explicit DatabaseManager(const std::string& dataDir);

    /** 引擎启动时调用，确保 data 目录存在 */
    void init();

    std::vector<std::string> listDatabases();
    void createDatabase(const std::string& name);
    void dropDatabase(const std::string& name);
    bool databaseExists(const std::string& name);

private:
    std::string dataDir_;
};
