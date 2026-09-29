#include "sql_engine.h"
#include <iostream>
#include <string>
#include <sstream>
#include <unistd.h>

inline std::string trimString(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

void printWelcome() {
    std::cout << "Welcome to the B-Tree Storage System MySQL monitor.  Commands end with ; or \\g.\n"
              << "Server version: 8.0-BTree (Persistent 4KB Page Engine)\n\n"
              << "Copyright (c) 2026 B-Tree Storage System Project.\n\n"
              << "Type 'help;' or '\\h' for help. Type 'exit;' or 'quit;' to exit.\n\n";
}

void executeScript(SqlEngine& engine, const std::string& input) {
    std::string current;
    bool inQuote = false;
    char quoteChar = 0;

    for (size_t i = 0; i < input.size(); i++) {
        char c = input[i];
        if ((c == '\'' || c == '"') && (i == 0 || input[i - 1] != '\\')) {
            if (!inQuote) {
                inQuote = true;
                quoteChar = c;
            } else if (quoteChar == c) {
                inQuote = false;
            }
        }

        if (c == ';' && !inQuote) {
            std::string trimmed = trimString(current);
            if (!trimmed.empty()) {
                QueryResult res = engine.execute(trimmed);
                std::cout << res.formatAsciiGrid();
            }
            current.clear();
        } else {
            current += c;
        }
    }

    std::string trimmed = trimString(current);
    if (!trimmed.empty()) {
        QueryResult res = engine.execute(trimmed);
        std::cout << res.formatAsciiGrid();
    }
}

int main(int argc, char* argv[]) {
    SqlEngine engine("data");

    // Support -e "SQL statement 1; SQL statement 2; ..."
    if (argc >= 3 && std::string(argv[1]) == "-e") {
        std::string query;
        for (int i = 2; i < argc; i++) {
            if (i > 2) query += " ";
            query += argv[i];
        }
        executeScript(engine, query);
        return 0;
    }

    bool isInteractive = isatty(fileno(stdin));
    if (isInteractive) {
        printWelcome();
    }

    std::string statementBuffer;
    std::string line;

    while (true) {
        if (isInteractive) {
            if (statementBuffer.empty()) {
                std::cout << "mysql> " << std::flush;
            } else {
                std::cout << "    -> " << std::flush;
            }
        }

        if (!std::getline(std::cin, line)) {
            break; // EOF
        }

        std::string trimmed = trimString(line);
        if (trimmed.empty()) continue;

        if (trimmed == "\\c") {
            statementBuffer.clear();
            continue;
        }

        if (!statementBuffer.empty()) {
            statementBuffer += " ";
        }
        statementBuffer += trimmed;

        // Check if statement ends with semicolon or \g or exit/quit
        std::string upperBuf = statementBuffer;
        std::transform(upperBuf.begin(), upperBuf.end(), upperBuf.begin(), ::toupper);

        if (statementBuffer.back() == ';' || 
            upperBuf == "EXIT" || upperBuf == "QUIT" || upperBuf == "\\Q" || upperBuf == "\\H" || upperBuf == "HELP") {
            
            QueryResult res = engine.execute(statementBuffer);
            statementBuffer.clear();

            if (res.isExit) {
                if (isInteractive) std::cout << "Bye\n";
                break;
            }

            std::cout << res.formatAsciiGrid();
        }
    }

    return 0;
}
