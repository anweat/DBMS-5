#include "UserManager.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <random>
#include <chrono>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================
// 紧凑 SHA-256 实现（标准算法，公共领域）
// ============================================================

static constexpr uint32_t SHA256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

std::string UserManager::sha256(const std::string& data) {
    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    std::vector<uint8_t> msg(data.begin(), data.end());
    size_t orig = msg.size();
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0);
    uint64_t bitlen = static_cast<uint64_t>(orig) * 8;
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>(bitlen >> (i * 8)));

    for (size_t ci = 0; ci < msg.size(); ci += 64) {
        uint32_t w[64];
        for (int j = 0; j < 16; ++j) {
            w[j] = (uint32_t(msg[ci+j*4  ]) << 24)
                 | (uint32_t(msg[ci+j*4+1]) << 16)
                 | (uint32_t(msg[ci+j*4+2]) <<  8)
                 |  uint32_t(msg[ci+j*4+3]);
        }
        for (int j = 16; j < 64; ++j) {
            uint32_t s0 = ((w[j-15]>>7)|(w[j-15]<<25)) ^ ((w[j-15]>>18)|(w[j-15]<<14)) ^ (w[j-15]>>3);
            uint32_t s1 = ((w[j-2] >>17)|(w[j-2] <<15)) ^ ((w[j-2] >>19)|(w[j-2] <<13)) ^ (w[j-2] >>10);
            w[j] = w[j-16] + s0 + w[j-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hv=h[7];
        for (int j = 0; j < 64; ++j) {
            uint32_t S1  = ((e>>6)|(e<<26)) ^ ((e>>11)|(e<<21)) ^ ((e>>25)|(e<<7));
            uint32_t ch  = (e&f) ^ (~e&g);
            uint32_t t1  = hv + S1 + ch + SHA256_K[j] + w[j];
            uint32_t S0  = ((a>>2)|(a<<30)) ^ ((a>>13)|(a<<19)) ^ ((a>>22)|(a<<10));
            uint32_t maj = (a&b) ^ (a&c) ^ (b&c);
            uint32_t t2  = S0 + maj;
            hv=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hv;
    }

    std::ostringstream oss;
    for (int i = 0; i < 8; ++i)
        oss << std::hex << std::setfill('0') << std::setw(8) << h[i];
    return oss.str();
}

// ============================================================
// 盐值生成
// ============================================================

std::string UserManager::generateSalt() {
    std::mt19937_64 rng(
        static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        )
    );
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << rng()
        << std::setw(16) << rng();
    return oss.str();
}

std::string UserManager::hashPassword(const std::string& salt,
                                       const std::string& password) {
    return sha256(salt + password);
}

// ============================================================
// 权限序列化
// ============================================================

std::string UserManager::privToStr(Privilege p) {
    switch (p) {
        case Privilege::SELECT: return "SELECT";
        case Privilege::INSERT: return "INSERT";
        case Privilege::UPDATE: return "UPDATE";
        case Privilege::DELETE: return "DELETE";
        case Privilege::ALL:    return "ALL";
    }
    return "SELECT";
}

Privilege UserManager::strToPriv(const std::string& s) {
    if (s == "INSERT") return Privilege::INSERT;
    if (s == "UPDATE") return Privilege::UPDATE;
    if (s == "DELETE") return Privilege::DELETE;
    if (s == "ALL")    return Privilege::ALL;
    return Privilege::SELECT;
}

// Serialize privilege list to a single string:
// "db1.table1:SELECT,INSERT|db2.*:ALL"
std::string UserManager::serializePrivs(const std::vector<UserPrivilege>& privs) {
    std::ostringstream oss;
    bool firstEntry = true;
    for (const auto& p : privs) {
        if (!firstEntry) oss << '|';
        firstEntry = false;
        oss << p.database << '.' << p.table << ':';
        bool firstPriv = true;
        for (Privilege pr : p.privs) {
            if (!firstPriv) oss << ',';
            firstPriv = false;
            oss << privToStr(pr);
        }
    }
    return oss.str();
}

std::vector<UserPrivilege> UserManager::deserializePrivs(const std::string& s) {
    std::vector<UserPrivilege> result;
    if (s.empty()) return result;

    std::istringstream entryStream(s);
    std::string entry;
    while (std::getline(entryStream, entry, '|')) {
        if (entry.empty()) continue;
        auto colonPos = entry.find(':');
        if (colonPos == std::string::npos) continue;
        std::string dbTable = entry.substr(0, colonPos);
        std::string privStr = entry.substr(colonPos + 1);

        auto dotPos = dbTable.find('.');
        UserPrivilege up;
        if (dotPos != std::string::npos) {
            up.database = dbTable.substr(0, dotPos);
            up.table    = dbTable.substr(dotPos + 1);
        } else {
            up.database = dbTable;
            up.table    = "*";
        }

        std::istringstream privStream(privStr);
        std::string p;
        while (std::getline(privStream, p, ',')) {
            if (!p.empty()) up.privs.insert(strToPriv(p));
        }
        result.push_back(std::move(up));
    }
    return result;
}

// ============================================================
// 持久化：加载 / 保存
// ============================================================

std::string UserManager::usersFilePath() const {
    return (fs::path(dataDir_) / "_users.dat").string();
}

void UserManager::load() {
    users_.clear();
    std::ifstream f(usersFilePath());
    if (!f) return;  // 文件不存在时保持空

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string username, salt, hash, privsStr;
        if (!std::getline(ss, username, '\t')) continue;
        if (!std::getline(ss, salt,     '\t')) continue;
        if (!std::getline(ss, hash,     '\t')) continue;
        std::getline(ss, privsStr, '\t');  // 可选

        UserRecord rec;
        rec.username     = username;
        rec.salt         = salt;
        rec.passwordHash = hash;
        rec.privileges   = deserializePrivs(privsStr);
        users_.push_back(std::move(rec));
    }
}

void UserManager::save() const {
    std::ofstream f(usersFilePath(), std::ios::trunc);
    if (!f)
        throw DBException(ErrorCode::FILE_IO_ERROR,
                          "Cannot write users file: " + usersFilePath());
    for (const auto& u : users_) {
        f << u.username << '\t'
          << u.salt     << '\t'
          << u.passwordHash << '\t'
          << serializePrivs(u.privileges) << '\n';
    }
}

// ============================================================
// 查找用户（可变 / 只读）
// ============================================================

UserRecord* UserManager::findUser(const std::string& username) {
    for (auto& u : users_)
        if (u.username == username) return &u;
    return nullptr;
}

const UserRecord* UserManager::findUser(const std::string& username) const {
    for (const auto& u : users_)
        if (u.username == username) return &u;
    return nullptr;
}

// ============================================================
// 公开接口
// ============================================================

UserManager::UserManager(const std::string& dataDir)
    : dataDir_(dataDir) {}

void UserManager::init() {
    load();
    if (users_.empty()) {
        // 首次初始化：创建 root 用户（密码 root），拥有所有权限
        std::string salt = generateSalt();
        UserRecord root;
        root.username     = "root";
        root.salt         = salt;
        root.passwordHash = hashPassword(salt, "root");
        // ALL ON *.*
        UserPrivilege ap;
        ap.database = "*"; ap.table = "*";
        ap.privs = { Privilege::ALL };
        root.privileges.push_back(ap);
        users_.push_back(std::move(root));
        save();
    }
}

bool UserManager::userExists(const std::string& username) const {
    return findUser(username) != nullptr;
}

std::vector<UserRecord> UserManager::listUsers() const {
    return users_;
}

void UserManager::createUser(const std::string& username,
                              const std::string& password) {
    if (username.empty())
        throw DBException(ErrorCode::DB_NAME_INVALID,
                          "User name cannot be empty");
    if (userExists(username))
        throw DBException(ErrorCode::DUPLICATE_KEY,
                          "User '" + username + "' already exists");
    std::string salt = generateSalt();
    UserRecord rec;
    rec.username     = username;
    rec.salt         = salt;
    rec.passwordHash = hashPassword(salt, password);
    users_.push_back(std::move(rec));
    save();
}

void UserManager::dropUser(const std::string& username) {
    if (username == "root")
        throw DBException(ErrorCode::PERMISSION_DENIED,
                          "Cannot drop the root user");
    auto it = std::find_if(users_.begin(), users_.end(),
        [&](const UserRecord& u){ return u.username == username; });
    if (it == users_.end())
        throw DBException(ErrorCode::DB_NOT_FOUND,
                          "User '" + username + "' does not exist");
    users_.erase(it);
    save();
}

bool UserManager::authenticate(const std::string& username,
                                const std::string& password) const {
    const UserRecord* u = findUser(username);
    if (!u) return false;
    return u->passwordHash == hashPassword(u->salt, password);
}

void UserManager::grantPrivilege(const std::string& username,
                                  const std::string& database,
                                  const std::string& table,
                                  const std::vector<Privilege>& privs) {
    UserRecord* u = findUser(username);
    if (!u)
        throw DBException(ErrorCode::DB_NOT_FOUND,
                          "User '" + username + "' does not exist");
    // 找或创建对应 db.table 的权限记录
    UserPrivilege* target = nullptr;
    for (auto& p : u->privileges) {
        if (p.database == database && p.table == table) {
            target = &p;
            break;
        }
    }
    if (!target) {
        UserPrivilege np;
        np.database = database;
        np.table    = table;
        u->privileges.push_back(std::move(np));
        target = &u->privileges.back();
    }
    for (Privilege p : privs)
        target->privs.insert(p);
    save();
}

void UserManager::revokePrivilege(const std::string& username,
                                   const std::string& database,
                                   const std::string& table,
                                   const std::vector<Privilege>& privs) {
    UserRecord* u = findUser(username);
    if (!u)
        throw DBException(ErrorCode::DB_NOT_FOUND,
                          "User '" + username + "' does not exist");
    for (auto& p : u->privileges) {
        if (p.database == database && p.table == table) {
            for (Privilege pr : privs)
                p.privs.erase(pr);
            break;
        }
    }
    save();
}

bool UserManager::hasPrivilege(const std::string& username,
                                const std::string& database,
                                const std::string& table,
                                Privilege priv) const {
    if (username == "root") return true;  // root 始终有所有权限

    const UserRecord* u = findUser(username);
    if (!u) return false;

    auto check = [&](const UserPrivilege& p) -> bool {
        bool dbMatch  = (p.database == "*" || p.database == database);
        bool tblMatch = (p.table    == "*" || p.table    == table);
        if (!dbMatch || !tblMatch) return false;
        return p.privs.count(Privilege::ALL) > 0 || p.privs.count(priv) > 0;
    };

    for (const auto& p : u->privileges)
        if (check(p)) return true;
    return false;
}
