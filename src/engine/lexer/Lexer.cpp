#include "Lexer.h"
#include <cctype>
#include <unordered_map>
#include <algorithm>

// ============================================================
// 关键字表（大写）
// ============================================================
static const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"CREATE", TokenType::CREATE},   {"DROP", TokenType::DROP},
    {"DATABASE", TokenType::DATABASE}, {"DATABASES", TokenType::DATABASES},
    {"TABLE", TokenType::TABLE},     {"TABLES", TokenType::TABLES},
    {"INDEX", TokenType::INDEX},
    {"SHOW", TokenType::SHOW},       {"USE", TokenType::USE},
    {"DESCRIBE", TokenType::DESCRIBE}, {"ALTER", TokenType::ALTER},
    {"ADD", TokenType::ADD},         {"MODIFY", TokenType::MODIFY},
    {"COLUMN", TokenType::COLUMN},
    // DML
    {"INSERT", TokenType::INSERT},   {"INTO", TokenType::INTO},
    {"VALUES", TokenType::VALUES},   {"SELECT", TokenType::SELECT},
    {"DISTINCT", TokenType::DISTINCT},
    {"FROM", TokenType::FROM},       {"WHERE", TokenType::WHERE},
    {"UPDATE", TokenType::UPDATE},   {"SET", TokenType::SET},
    {"DELETE", TokenType::DELETE},
    // 约束 / 类型
    {"PRIMARY", TokenType::PRIMARY}, {"KEY", TokenType::KEY},
    {"FOREIGN", TokenType::FOREIGN}, {"REFERENCES", TokenType::REFERENCES},
    {"UNIQUE", TokenType::UNIQUE},   {"NOT", TokenType::NOT},
    {"NULL", TokenType::NULL_KW},    {"DEFAULT", TokenType::DEFAULT},
    {"AUTO_INCREMENT", TokenType::AUTO_INCREMENT},
    {"INT", TokenType::INT_KW},      {"INTEGER", TokenType::INTEGER_KW},
    {"DOUBLE", TokenType::DOUBLE_KW}, {"FLOAT", TokenType::FLOAT_KW},
    {"VARCHAR", TokenType::VARCHAR_KW}, {"BOOL", TokenType::BOOL_KW},
    {"BOOLEAN", TokenType::BOOL_KW}, {"DATETIME", TokenType::DATETIME_KW},
    {"CONSTRAINT", TokenType::CONSTRAINT},
    // 查询修饰
    {"ORDER", TokenType::ORDER},     {"BY", TokenType::BY},
    {"ASC", TokenType::ASC},         {"DESC", TokenType::DESC},
    {"LIMIT", TokenType::LIMIT},     {"OFFSET", TokenType::OFFSET},
    {"GROUP", TokenType::GROUP},     {"HAVING", TokenType::HAVING},
    {"AS", TokenType::AS},
    {"AND", TokenType::AND},         {"OR", TokenType::OR},
    {"IN", TokenType::IN},           {"LIKE", TokenType::LIKE},
    {"IS", TokenType::IS},           {"BETWEEN", TokenType::BETWEEN},
    {"COUNT", TokenType::COUNT},     {"SUM", TokenType::SUM},
    {"MAX", TokenType::MAX},         {"MIN", TokenType::MIN},
    {"AVG", TokenType::AVG},
    {"IF", TokenType::IF},           {"EXISTS", TokenType::EXISTS},
    // 布尔字面量（特殊处理）
    {"TRUE", TokenType::BOOL_LITERAL}, {"FALSE", TokenType::BOOL_LITERAL},
    // 事务
    {"BEGIN", TokenType::BEGIN},     {"COMMIT", TokenType::COMMIT},
    {"ROLLBACK", TokenType::ROLLBACK}, {"TRANSACTION", TokenType::TRANSACTION},
    // 安全
    {"GRANT", TokenType::GRANT},     {"REVOKE", TokenType::REVOKE},
    {"ON", TokenType::ON},           {"TO", TokenType::TO},
    {"WITH", TokenType::WITH},       {"OPTION", TokenType::OPTION},
    {"ALL", TokenType::ALL},         {"PRIVILEGES", TokenType::PRIVILEGES},
    {"USER", TokenType::USER},       {"PASSWORD", TokenType::PASSWORD},
};

// ============================================================
// 内部实现：字符扫描器
// ============================================================
namespace {

struct Scanner {
    const std::string& src;
    size_t pos  = 0;
    int    line = 1;
    int    col  = 1;

    bool   eof()  const { return pos >= src.size(); }
    char   peek() const { return eof() ? '\0' : src[pos]; }
    char   peek2()const { return (pos + 1 < src.size()) ? src[pos + 1] : '\0'; }

    char advance() {
        char c = src[pos++];
        if (c == '\n') { ++line; col = 1; } else { ++col; }
        return c;
    }

    void skipWhitespaceAndComments() {
        while (!eof()) {
            char c = peek();
            // 空白
            if (std::isspace(static_cast<unsigned char>(c))) { advance(); continue; }
            // 行注释 --
            if (c == '-' && peek2() == '-') {
                while (!eof() && peek() != '\n') advance();
                continue;
            }
            // 块注释 /* ... */
            if (c == '/' && peek2() == '*') {
                advance(); advance();  // consume /*
                while (!eof()) {
                    if (peek() == '*' && peek2() == '/') {
                        advance(); advance(); break;
                    }
                    advance();
                }
                continue;
            }
            break;
        }
    }

    Token makeToken(TokenType t, const std::string& val, int ln, int cl) {
        return {t, val, ln, cl};
    }

    // 读取带引号的字符串（单引号或双引号，支持转义 \'  \\）
    Token readString(char quote) {
        int ln = line, cl = col;
        advance();  // consume opening quote
        std::string val;
        while (!eof()) {
            char c = advance();
            if (c == '\\' && !eof()) {
                char esc = advance();
                switch (esc) {
                    case 'n':  val += '\n'; break;
                    case 't':  val += '\t'; break;
                    case '\\': val += '\\'; break;
                    default:   val += esc;  break;
                }
            } else if (c == quote) {
                break;
            } else {
                val += c;
            }
        }
        return makeToken(TokenType::STRING_LITERAL, val, ln, cl);
    }

    // 读取数字字面量（整数或浮点）
    Token readNumber() {
        int ln = line, cl = col;
        std::string val;
        bool isFloat = false;
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek())))
            val += advance();
        if (!eof() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peek2()))) {
            isFloat = true;
            val += advance();  // consume '.'
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek())))
                val += advance();
        }
        // 科学计数法 e/E
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            isFloat = true;
            val += advance();
            if (!eof() && (peek() == '+' || peek() == '-')) val += advance();
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek())))
                val += advance();
        }
        return makeToken(isFloat ? TokenType::DOUBLE_LITERAL : TokenType::INT_LITERAL,
                         val, ln, cl);
    }

    // 读取标识符或关键字
    Token readIdentOrKeyword() {
        int ln = line, cl = col;
        std::string raw;
        while (!eof()) {
            char c = peek();
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                raw += advance();
            } else {
                break;
            }
        }
        // 大写比对关键字
        std::string upper = raw;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c){ return std::toupper(c); });

        auto it = KEYWORDS.find(upper);
        if (it != KEYWORDS.end()) {
            // NULL 关键字特殊：存为 NULL_LITERAL 还是 NULL_KW 由 token type 决定
            return makeToken(it->second, upper, ln, cl);
        }
        return makeToken(TokenType::IDENTIFIER, raw, ln, cl);
    }

    // 读取带反引号的标识符（`table_name`）
    Token readBacktickIdent() {
        int ln = line, cl = col;
        advance();  // consume `
        std::string val;
        while (!eof() && peek() != '`') val += advance();
        if (!eof()) advance();  // consume closing `
        return makeToken(TokenType::IDENTIFIER, val, ln, cl);
    }
};

}  // namespace

// ============================================================
// Lexer::tokenize
// ============================================================
std::vector<Token> Lexer::tokenize(const std::string& sql) {
    Scanner sc{sql};
    std::vector<Token> tokens;

    while (true) {
        sc.skipWhitespaceAndComments();
        if (sc.eof()) break;

        int ln = sc.line, cl = sc.col;
        char c = sc.peek();

        // 字符串字面量
        if (c == '\'' || c == '"') {
            tokens.push_back(sc.readString(c));
            continue;
        }

        // 反引号标识符
        if (c == '`') {
            tokens.push_back(sc.readBacktickIdent());
            continue;
        }

        // 数字
        if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(sc.readNumber());
            continue;
        }

        // 标识符 / 关键字
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(sc.readIdentOrKeyword());
            continue;
        }

        // 双字符运算符
        sc.advance();
        char n = sc.peek();
        switch (c) {
            case '<':
                if (n == '=') { sc.advance(); tokens.push_back({TokenType::LE, "<=", ln, cl}); }
                else if (n == '>') { sc.advance(); tokens.push_back({TokenType::NEQ, "<>", ln, cl}); }
                else tokens.push_back({TokenType::LT, "<", ln, cl});
                break;
            case '>':
                if (n == '=') { sc.advance(); tokens.push_back({TokenType::GE, ">=", ln, cl}); }
                else tokens.push_back({TokenType::GT, ">", ln, cl});
                break;
            case '!':
                if (n == '=') { sc.advance(); tokens.push_back({TokenType::NEQ, "!=", ln, cl}); }
                else tokens.push_back({TokenType::UNKNOWN, "!", ln, cl});
                break;
            case '=': tokens.push_back({TokenType::EQ,        "=",  ln, cl}); break;
            case '+': tokens.push_back({TokenType::PLUS,       "+",  ln, cl}); break;
            case '-': tokens.push_back({TokenType::MINUS,      "-",  ln, cl}); break;
            case '*': tokens.push_back({TokenType::STAR,       "*",  ln, cl}); break;
            case '/': tokens.push_back({TokenType::SLASH,      "/",  ln, cl}); break;
            case '%': tokens.push_back({TokenType::PERCENT,    "%",  ln, cl}); break;
            case '(': tokens.push_back({TokenType::LPAREN,     "(",  ln, cl}); break;
            case ')': tokens.push_back({TokenType::RPAREN,     ")",  ln, cl}); break;
            case ',': tokens.push_back({TokenType::COMMA,      ",",  ln, cl}); break;
            case ';': tokens.push_back({TokenType::SEMICOLON,  ";",  ln, cl}); break;
            case '.': tokens.push_back({TokenType::DOT,        ".",  ln, cl}); break;
            default:  tokens.push_back({TokenType::UNKNOWN, std::string(1, c), ln, cl}); break;
        }
    }

    tokens.push_back({TokenType::EOF_TOKEN, "", sc.line, sc.col});
    return tokens;
}
