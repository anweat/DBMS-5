#include "../src/engine/lexer/Lexer.h"
#include "../src/engine/parser/Parser.h"
#include <iostream>

int main() {
    std::cout << "Testing JOIN syntax parsing...\n";

    Lexer lexer;
    Parser parser;

    // Test 1: INNER JOIN
    try {
        auto tokens = lexer.tokenize("SELECT u.name FROM users u INNER JOIN orders o ON u.id = o.uid");
        auto ast = parser.parse(tokens);
        std::cout << "OK: INNER JOIN parsed successfully\n";
    } catch (const std::exception& e) {
        std::cout << "FAIL: INNER JOIN failed: " << e.what() << "\n";
        return 1;
    }

    // Test 2: JOIN (implicit INNER)
    try {
        auto tokens = lexer.tokenize("SELECT u.name FROM users u JOIN orders o ON u.id = o.uid");
        auto ast = parser.parse(tokens);
        std::cout << "OK: JOIN parsed successfully\n";
    } catch (const std::exception& e) {
        std::cout << "FAIL: JOIN failed: " << e.what() << "\n";
        return 1;
    }

    // Test 3: Qualified wildcard
    try {
        auto tokens = lexer.tokenize("SELECT u.*, o.amount FROM users u, orders o WHERE u.id = o.uid");
        auto ast = parser.parse(tokens);
        std::cout << "OK: Qualified wildcard parsed successfully\n";
    } catch (const std::exception& e) {
        std::cout << "FAIL: Qualified wildcard failed: " << e.what() << "\n";
        return 1;
    }

    // Test 4: LEFT JOIN should fail
    try {
        auto tokens = lexer.tokenize("SELECT * FROM a LEFT JOIN b ON a.id = b.id");
        auto ast = parser.parse(tokens);
        std::cout << "FAIL: LEFT JOIN should have failed but didn't\n";
        return 1;
    } catch (const DBException& e) {
        if (std::string(e.what()).find("not supported") != std::string::npos) {
            std::cout << "OK: LEFT JOIN correctly rejected\n";
        } else {
            std::cout << "FAIL: LEFT JOIN failed with wrong error: " << e.what() << "\n";
            return 1;
        }
    }

    std::cout << "\nAll parsing tests passed!\n";
    return 0;
}
