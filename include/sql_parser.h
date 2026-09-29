#ifndef SQL_PARSER_H
#define SQL_PARSER_H

#include "schema.h"
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <stdexcept>

enum class StatementType {
    CREATE_TABLE,
    DROP_TABLE,
    SHOW_TABLES,
    DESCRIBE_TABLE,
    INSERT,
    SELECT,
    DELETE,
    PRINT_TREE,
    HELP,
    EXIT,
    UNKNOWN
};

struct Statement {
    StatementType type = StatementType::UNKNOWN;
    virtual ~Statement() = default;
};

struct CreateTableStmt : public Statement {
    CreateTableStmt() { type = StatementType::CREATE_TABLE; }
    std::string tableName;
    bool ifNotExists = false;
    TableSchema schema;
};

struct DropTableStmt : public Statement {
    DropTableStmt() { type = StatementType::DROP_TABLE; }
    std::string tableName;
    bool ifExists = false;
};

struct ShowTablesStmt : public Statement {
    ShowTablesStmt() { type = StatementType::SHOW_TABLES; }
};

struct DescribeStmt : public Statement {
    DescribeStmt() { type = StatementType::DESCRIBE_TABLE; }
    std::string tableName;
};

struct InsertStmt : public Statement {
    InsertStmt() { type = StatementType::INSERT; }
    std::string tableName;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};

struct WhereClause {
    std::string column;
    std::string op; // "=", ">", "<", ">=", "<=", "!=", "BETWEEN"
    std::string value1;
    std::string value2; // for BETWEEN or combined range
};

struct SelectStmt : public Statement {
    SelectStmt() { type = StatementType::SELECT; }
    std::string tableName;
    bool selectAllColumns = true;
    std::vector<std::string> projectedColumns;
    bool hasWhere = false;
    WhereClause where;
};

struct DeleteStmt : public Statement {
    DeleteStmt() { type = StatementType::DELETE; }
    std::string tableName;
    bool hasWhere = false;
    WhereClause where;
};

struct PrintTreeStmt : public Statement {
    PrintTreeStmt() { type = StatementType::PRINT_TREE; }
    std::string tableName;
};

struct HelpStmt : public Statement {
    HelpStmt() { type = StatementType::HELP; }
};

struct ExitStmt : public Statement {
    ExitStmt() { type = StatementType::EXIT; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Lexer / Tokenizer
// ─────────────────────────────────────────────────────────────────────────────
enum class TokenType {
    IDENTIFIER,
    KEYWORD,
    STRING_LITERAL,
    NUMBER_LITERAL,
    SYMBOL,
    EOF_TOKEN
};

struct Token {
    TokenType   type;
    std::string text;
    int         pos;
};

class SqlLexer {
public:
    explicit SqlLexer(const std::string& input) : src_(input), pos_(0) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (pos_ < src_.size()) {
            skipWhitespace();
            if (pos_ >= src_.size()) break;

            char c = src_[pos_];

            // Semicolon or single character symbols
            if (c == '(' || c == ')' || c == ',' || c == ';' || c == '*') {
                tokens.push_back({TokenType::SYMBOL, std::string(1, c), static_cast<int>(pos_)});
                pos_++;
                continue;
            }

            // Comparison operators: =, !=, <>, <, >, <=, >=
            if (c == '=' || c == '<' || c == '>' || c == '!') {
                int start = pos_;
                std::string op(1, c);
                pos_++;
                if (pos_ < src_.size()) {
                    char next = src_[pos_];
                    if ((c == '<' && next == '=') || (c == '>' && next == '=') ||
                        (c == '!' && next == '=') || (c == '<' && next == '>')) {
                        op += next;
                        pos_++;
                    }
                }
                tokens.push_back({TokenType::SYMBOL, op, start});
                continue;
            }

            // String literals: 'string' or "string"
            if (c == '\'' || c == '"') {
                tokens.push_back(readString(c));
                continue;
            }

            // Backtick identifiers: `table_name`
            if (c == '`') {
                tokens.push_back(readBacktickIdentifier());
                continue;
            }

            // Numbers: negative numbers or positive digits
            if (std::isdigit(c) || (c == '-' && pos_ + 1 < src_.size() && std::isdigit(src_[pos_ + 1]))) {
                tokens.push_back(readNumber());
                continue;
            }

            // Identifiers and Keywords
            if (std::isalpha(c) || c == '_' || c == '\\') {
                tokens.push_back(readIdentifierOrKeyword());
                continue;
            }

            // Unknown character, skip or record
            pos_++;
        }
        tokens.push_back({TokenType::EOF_TOKEN, "", static_cast<int>(pos_)});
        return tokens;
    }

private:
    std::string src_;
    size_t      pos_;

    void skipWhitespace() {
        while (pos_ < src_.size() && (std::isspace(src_[pos_]) || src_[pos_] == '\r' || src_[pos_] == '\n')) {
            pos_++;
        }
    }

    Token readString(char quote) {
        int start = pos_;
        pos_++; // skip open quote
        std::string s;
        while (pos_ < src_.size() && src_[pos_] != quote) {
            if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
                pos_++;
                s += src_[pos_];
            } else {
                s += src_[pos_];
            }
            pos_++;
        }
        if (pos_ < src_.size() && src_[pos_] == quote) {
            pos_++; // skip close quote
        }
        return {TokenType::STRING_LITERAL, s, start};
    }

    Token readBacktickIdentifier() {
        int start = pos_;
        pos_++;
        std::string s;
        while (pos_ < src_.size() && src_[pos_] != '`') {
            s += src_[pos_++];
        }
        if (pos_ < src_.size() && src_[pos_] == '`') pos_++;
        return {TokenType::IDENTIFIER, s, start};
    }

    Token readNumber() {
        int start = pos_;
        std::string s;
        if (src_[pos_] == '-') s += src_[pos_++];
        bool hasDot = false;
        while (pos_ < src_.size() && (std::isdigit(src_[pos_]) || (!hasDot && src_[pos_] == '.'))) {
            if (src_[pos_] == '.') hasDot = true;
            s += src_[pos_++];
        }
        return {TokenType::NUMBER_LITERAL, s, start};
    }

    Token readIdentifierOrKeyword() {
        int start = pos_;
        std::string s;
        while (pos_ < src_.size() && (std::isalnum(src_[pos_]) || src_[pos_] == '_' || src_[pos_] == '\\')) {
            s += src_[pos_++];
        }
        return {TokenType::IDENTIFIER, s, start};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SQL Parser
// ─────────────────────────────────────────────────────────────────────────────
class SqlParser {
public:
    explicit SqlParser(const std::string& sql) {
        SqlLexer lexer(sql);
        tokens_ = lexer.tokenize();
        idx_ = 0;
    }

    std::unique_ptr<Statement> parse() {
        if (isEnd()) return nullptr;

        std::string kw = peekUpper();

        if (kw == "CREATE") {
            return parseCreate();
        } else if (kw == "DROP") {
            return parseDrop();
        } else if (kw == "SHOW") {
            return parseShow();
        } else if (kw == "DESCRIBE" || kw == "DESC" || kw == "EXPLAIN") {
            return parseDescribe();
        } else if (kw == "INSERT") {
            return parseInsert();
        } else if (kw == "SELECT") {
            return parseSelect();
        } else if (kw == "DELETE") {
            return parseDelete();
        } else if (kw == "PRINT") {
            return parsePrintTree();
        } else if (kw == "HELP" || kw == "\\H") {
            consume();
            matchSemicolon();
            return std::make_unique<HelpStmt>();
        } else if (kw == "EXIT" || kw == "QUIT" || kw == "\\Q") {
            consume();
            matchSemicolon();
            return std::make_unique<ExitStmt>();
        }

        throw std::runtime_error("ERROR 1064 (42000): You have an error in your SQL syntax near '" + current().text + "'");
    }

private:
    std::vector<Token> tokens_;
    size_t             idx_;

    const Token& current() const {
        if (idx_ < tokens_.size()) return tokens_[idx_];
        return tokens_.back();
    }

    bool isEnd() const {
        return idx_ >= tokens_.size() || tokens_[idx_].type == TokenType::EOF_TOKEN;
    }

    std::string peekUpper() const {
        if (isEnd()) return "";
        std::string s = tokens_[idx_].text;
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return s;
    }

    Token consume() {
        Token t = current();
        if (idx_ < tokens_.size()) idx_++;
        return t;
    }

    bool matchUpper(const std::string& expected) {
        if (peekUpper() == expected) {
            consume();
            return true;
        }
        return false;
    }

    void expectUpper(const std::string& expected) {
        if (!matchUpper(expected)) {
            throw std::runtime_error("ERROR 1064 (42000): Expected '" + expected + "' but got '" + current().text + "'");
        }
    }

    void expectSymbol(const std::string& symbol) {
        if (current().type == TokenType::SYMBOL && current().text == symbol) {
            consume();
        } else {
            throw std::runtime_error("ERROR 1064 (42000): Expected '" + symbol + "' but got '" + current().text + "'");
        }
    }

    void matchSemicolon() {
        if (!isEnd() && current().type == TokenType::SYMBOL && current().text == ";") {
            consume();
        }
    }

    // ── CREATE TABLE ─────────────────────────────────────────────────────────
    std::unique_ptr<Statement> parseCreate() {
        consume(); // CREATE
        expectUpper("TABLE");

        auto stmt = std::make_unique<CreateTableStmt>();

        if (matchUpper("IF")) {
            expectUpper("NOT");
            expectUpper("EXISTS");
            stmt->ifNotExists = true;
        }

        stmt->tableName = consume().text;
        stmt->schema.tableName = stmt->tableName;

        expectSymbol("(");

        while (!isEnd() && current().text != ")") {
            std::string nameOrPk = current().text;
            std::string upperName = peekUpper();

            if (upperName == "PRIMARY") {
                consume(); // PRIMARY
                expectUpper("KEY");
                expectSymbol("(");
                std::string pkCol = consume().text;
                expectSymbol(")");

                int idx = stmt->schema.getColumnIndex(pkCol);
                if (idx >= 0) {
                    stmt->schema.columns[idx].isPrimaryKey = true;
                } else {
                    throw std::runtime_error("ERROR 1072 (42000): Key column '" + pkCol + "' doesn't exist in table");
                }
            } else {
                consume(); // column name
                ColumnDef col;
                col.name = nameOrPk;

                std::string typeStr = peekUpper();
                consume();

                col.type = stringToDataType(typeStr);

                if (col.type == DataType::VARCHAR || col.type == DataType::CHAR) {
                    if (current().text == "(") {
                        consume(); // (
                        col.length = std::stoul(consume().text);
                        expectSymbol(")");
                    } else {
                        col.length = (col.type == DataType::VARCHAR) ? 256 : 1;
                    }
                }

                // Parse column constraints: PRIMARY KEY, NOT NULL
                while (!isEnd() && current().text != "," && current().text != ")") {
                    std::string cUpper = peekUpper();
                    if (cUpper == "PRIMARY") {
                        consume();
                        expectUpper("KEY");
                        col.isPrimaryKey = true;
                    } else if (cUpper == "NOT") {
                        consume();
                        expectUpper("NULL");
                        col.notNull = true;
                    } else if (cUpper == "NULL") {
                        consume();
                    } else {
                        consume(); // skip other unrecognized constraints gracefully
                    }
                }

                stmt->schema.columns.push_back(col);
            }

            if (current().text == ",") {
                consume();
            } else {
                break;
            }
        }

        expectSymbol(")");
        matchSemicolon();
        return stmt;
    }

    // ── DROP TABLE ───────────────────────────────────────────────────────────
    std::unique_ptr<Statement> parseDrop() {
        consume(); // DROP
        expectUpper("TABLE");
        auto stmt = std::make_unique<DropTableStmt>();
        if (matchUpper("IF")) {
            expectUpper("EXISTS");
            stmt->ifExists = true;
        }
        stmt->tableName = consume().text;
        matchSemicolon();
        return stmt;
    }

    // ── SHOW TABLES / SHOW TREE ──────────────────────────────────────────────
    std::unique_ptr<Statement> parseShow() {
        consume(); // SHOW
        std::string next = peekUpper();
        if (next == "TABLES") {
            consume();
            if (matchUpper("FROM")) {
                consume(); // ignore db name
            }
            matchSemicolon();
            return std::make_unique<ShowTablesStmt>();
        } else if (next == "TREE") {
            consume();
            auto stmt = std::make_unique<PrintTreeStmt>();
            if (matchUpper("FROM")) {
                // optional FROM
            }
            stmt->tableName = consume().text;
            matchSemicolon();
            return stmt;
        }
        throw std::runtime_error("ERROR 1064 (42000): Unknown SHOW command near '" + next + "'");
    }

    // ── DESCRIBE ─────────────────────────────────────────────────────────────
    std::unique_ptr<Statement> parseDescribe() {
        consume(); // DESCRIBE / DESC / EXPLAIN
        auto stmt = std::make_unique<DescribeStmt>();
        stmt->tableName = consume().text;
        matchSemicolon();
        return stmt;
    }

    // ── INSERT INTO ──────────────────────────────────────────────────────────
    std::unique_ptr<Statement> parseInsert() {
        consume(); // INSERT
        expectUpper("INTO");
        auto stmt = std::make_unique<InsertStmt>();
        stmt->tableName = consume().text;

        // Optional column list: (col1, col2, ...)
        if (current().text == "(") {
            consume(); // (
            while (!isEnd() && current().text != ")") {
                stmt->columns.push_back(consume().text);
                if (current().text == ",") consume();
            }
            expectSymbol(")");
        }

        expectUpper("VALUES");

        // Values list: (val1, val2), (val3, val4)
        while (!isEnd() && current().text == "(") {
            consume(); // (
            std::vector<std::string> row;
            while (!isEnd() && current().text != ")") {
                row.push_back(consume().text);
                if (current().text == ",") consume();
            }
            expectSymbol(")");
            stmt->rows.push_back(row);
            if (current().text == ",") {
                consume();
            } else {
                break;
            }
        }

        matchSemicolon();
        return stmt;
    }

    // ── SELECT ───────────────────────────────────────────────────────────────
    std::unique_ptr<Statement> parseSelect() {
        consume(); // SELECT
        auto stmt = std::make_unique<SelectStmt>();

        if (current().text == "*") {
            consume();
            stmt->selectAllColumns = true;
        } else {
            stmt->selectAllColumns = false;
            while (!isEnd() && peekUpper() != "FROM") {
                stmt->projectedColumns.push_back(consume().text);
                if (current().text == ",") consume();
            }
        }

        expectUpper("FROM");
        stmt->tableName = consume().text;

        if (matchUpper("WHERE")) {
            stmt->hasWhere = true;
            parseWhereClause(stmt->where);
        }

        matchSemicolon();
        return stmt;
    }

    // ── DELETE ───────────────────────────────────────────────────────────────
    std::unique_ptr<Statement> parseDelete() {
        consume(); // DELETE
        expectUpper("FROM");
        auto stmt = std::make_unique<DeleteStmt>();
        stmt->tableName = consume().text;

        if (matchUpper("WHERE")) {
            stmt->hasWhere = true;
            parseWhereClause(stmt->where);
        }

        matchSemicolon();
        return stmt;
    }

    // ── PRINT TREE ───────────────────────────────────────────────────────────
    std::unique_ptr<Statement> parsePrintTree() {
        consume(); // PRINT
        expectUpper("TREE");
        auto stmt = std::make_unique<PrintTreeStmt>();
        if (matchUpper("FROM")) {
            // optional FROM
        }
        stmt->tableName = consume().text;
        matchSemicolon();
        return stmt;
    }

    // ── WHERE clause helper ──────────────────────────────────────────────────
    void parseWhereClause(WhereClause& w) {
        w.column = consume().text;

        std::string op = current().text;
        std::string opUpper = peekUpper();

        if (opUpper == "BETWEEN") {
            consume();
            w.op = "BETWEEN";
            w.value1 = consume().text;
            expectUpper("AND");
            w.value2 = consume().text;
            return;
        }

        // Standard comparison operator: =, >=, <=, >, <, !=
        consume();
        w.op = op;
        w.value1 = consume().text;

        // Check if there is a second range condition: AND col <= val
        if (peekUpper() == "AND") {
            consume(); // AND
            std::string col2 = consume().text;
            std::string op2 = consume().text;
            std::string val2 = consume().text;

            // Normalize [>= val1 AND <= val2] or [<= val2 AND >= val1]
            if ((w.op == ">=" || w.op == ">") && (op2 == "<=" || op2 == "<")) {
                w.op = "BETWEEN";
                w.value2 = val2;
            } else if ((w.op == "<=" || w.op == "<") && (op2 == ">=" || op2 == ">")) {
                w.op = "BETWEEN";
                w.value2 = w.value1;
                w.value1 = val2;
            }
        }
    }
};

#endif // SQL_PARSER_H
