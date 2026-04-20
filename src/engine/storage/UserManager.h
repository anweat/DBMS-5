#pragma once

#include "../../types.h"
#include <string>
#include <vector>
#include <set>

// 单条权限记录：某用户在 database.table 上拥有的权限集合
struct UserPrivilege {
    std::string       database;  // "*" 表示所有库
    std::string       table;     // "*" 表示所有表
    std::set<Privilege> privs;
};

// 用户记录（内存中）
struct UserRecord {
    std::string                  username;
    std::string                  salt;
    std::string                  passwordHash;
    std::vector<UserPrivilege>   privileges;
};

class UserManager {
public:
    explicit UserManager(const std::string& dataDir);

    /** 初始化：若用户文件不存在则创建 root 用户（密码 root） */
    void init();

    bool userExists(const std::string& username) const;

    void createUser(const std::string& username, const std::string& password);
    void dropUser(const std::string& username);

    /** 验证用户名/密码，成功返回 true */
    bool authenticate(const std::string& username, const std::string& password) const;

    void grantPrivilege(const std::string& username,
                        const std::string& database,
                        const std::string& table,
                        const std::vector<Privilege>& privs);

    void revokePrivilege(const std::string& username,
                         const std::string& database,
                         const std::string& table,
                         const std::vector<Privilege>& privs);

    /** 检查用户是否拥有指定权限（root 始终返回 true） */
    bool hasPrivilege(const std::string& username,
                      const std::string& database,
                      const std::string& table,
                      Privilege priv) const;

private:
    std::string              dataDir_;
    std::vector<UserRecord>  users_;

    void load();
    void save() const;

    std::string usersFilePath() const;
    UserRecord* findUser(const std::string& username);
    const UserRecord* findUser(const std::string& username) const;

    static std::string sha256(const std::string& msg);
    static std::string generateSalt();
    static std::string hashPassword(const std::string& salt,
                                    const std::string& password);

    // 序列化/反序列化权限列表
    static std::string serializePrivs(const std::vector<UserPrivilege>& privs);
    static std::vector<UserPrivilege> deserializePrivs(const std::string& s);
    static std::string privToStr(Privilege p);
    static Privilege   strToPriv(const std::string& s);
};
