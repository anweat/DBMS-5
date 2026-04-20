#include "engine/DBEngine.h"
#include "cli/Formatter.h"
#include "cli/Session.h"
#include "cli/Repl.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    Formatter::enableAnsi();

    const std::string dataDir = "data";

    DBEngine  engine(dataDir);
    CLISession session;
    // user 默认为匿名，需通过 CONNECT 'user' IDENTIFIED BY 'pass' 认证

    Repl repl(engine, session);

    if (argc > 1) {
        repl.runFile(argv[1]);
    } else {
        std::cout << Formatter::green("DBMS v1.0") << " — A lightweight relational DBMS\n";
        std::cout << "Type \\help for help, \\quit to exit.\n";
        std::cout << "Tip: use  CONNECT 'user' IDENTIFIED BY 'pass';  to authenticate.\n\n";
        repl.run();
    }

    return 0;
}
