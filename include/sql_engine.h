#ifndef SQL_ENGINE_H
#define SQL_ENGINE_H

#include "table_manager.h"
#include "sql_parser.h"
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

struct QueryResult {
    bool        success = true;
    std::string message;
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    double      elapsedSeconds = 0.0;
    bool        isExit = false;

    std::string formatAsciiGrid() const {
        std::ostringstream oss;
        if (!success) {
            oss << message << "\n";
            return oss.str();
        }

        if (headers.empty()) {
            if (!message.empty()) {
                oss << message << " (" << std::fixed << std::setprecision(2) << elapsedSeconds << " sec)\n";
            }
            return oss.str();
        }

        if (rows.empty()) {
            oss << "Empty set (" << std::fixed << std::setprecision(2) << elapsedSeconds << " sec)\n";
            return oss.str();
        }

        size_t cols = headers.size();
        std::vector<size_t> colWidths(cols, 0);

        for (size_t c = 0; c < cols; c++) {
            colWidths[c] = std::max(colWidths[c], headers[c].size());
        }
        for (const auto& r : rows) {
            for (size_t c = 0; c < cols && c < r.size(); c++) {
                colWidths[c] = std::max(colWidths[c], r[c].size());
            }
        }

        auto printSeparator = [&]() {
            oss << "+";
            for (size_t c = 0; c < cols; c++) {
                oss << std::string(colWidths[c] + 2, '-') << "+";
            }
            oss << "\n";
        };

        // Header
        printSeparator();
        oss << "|";
        for (size_t c = 0; c < cols; c++) {
            oss << " " << std::left << std::setw(colWidths[c]) << headers[c] << " |";
        }
        oss << "\n";
        printSeparator();

        // Data rows
        for (const auto& r : rows) {
            oss << "|";
            for (size_t c = 0; c < cols; c++) {
                std::string cell = (c < r.size()) ? r[c] : "";
                oss << " " << std::left << std::setw(colWidths[c]) << cell << " |";
            }
            oss << "\n";
        }
        printSeparator();

        // Footer summary
        if (rows.size() == 1) {
            oss << "1 row in set (" << std::fixed << std::setprecision(2) << elapsedSeconds << " sec)\n";
        } else {
            oss << rows.size() << " rows in set (" << std::fixed << std::setprecision(2) << elapsedSeconds << " sec)\n";
        }

        return oss.str();
    }
};

class SqlEngine {
public:
    explicit SqlEngine(const std::string& baseDir = "data")
        : tableMgr_(baseDir) {}

    QueryResult execute(const std::string& sql) {
        QueryResult res;
        auto start = std::chrono::high_resolution_clock::now();

        try {
            SqlParser parser(sql);
            std::unique_ptr<Statement> stmt = parser.parse();

            if (!stmt) {
                res.success = true;
                return res;
            }

            switch (stmt->type) {
                case StatementType::CREATE_TABLE: {
                    auto* cStmt = static_cast<CreateTableStmt*>(stmt.get());
                    std::string err;
                    if (tableMgr_.tableExists(cStmt->tableName)) {
                        if (cStmt->ifNotExists) {
                            res.message = "Query OK, 0 rows affected, 1 warning (table already exists)";
                            break;
                        }
                    }
                    if (!tableMgr_.createTable(cStmt->schema, err)) {
                        res.success = false;
                        res.message = err;
                    } else {
                        res.message = "Query OK, 0 rows affected";
                    }
                    break;
                }
                case StatementType::DROP_TABLE: {
                    auto* dStmt = static_cast<DropTableStmt*>(stmt.get());
                    std::string err;
                    if (!tableMgr_.tableExists(dStmt->tableName)) {
                        if (dStmt->ifExists) {
                            res.message = "Query OK, 0 rows affected, 1 warning (table doesn't exist)";
                            break;
                        }
                    }
                    if (!tableMgr_.dropTable(dStmt->tableName, err)) {
                        res.success = false;
                        res.message = err;
                    } else {
                        res.message = "Query OK, 0 rows affected";
                    }
                    break;
                }
                case StatementType::SHOW_TABLES: {
                    auto tables = tableMgr_.listTables();
                    res.headers = {"Tables_in_db"};
                    for (const auto& t : tables) {
                        res.rows.push_back({t});
                    }
                    break;
                }
                case StatementType::DESCRIBE_TABLE: {
                    auto* descStmt = static_cast<DescribeStmt*>(stmt.get());
                    auto table = tableMgr_.getTable(descStmt->tableName);
                    if (!table) {
                        res.success = false;
                        res.message = "ERROR 1146 (42S02): Table '" + descStmt->tableName + "' doesn't exist";
                        break;
                    }
                    res.headers = {"Field", "Type", "Null", "Key", "Default", "Extra"};
                    const auto& schema = table->getSchema();
                    for (const auto& col : schema.columns) {
                        res.rows.push_back({
                            col.name,
                            col.typeString(),
                            col.notNull ? "NO" : "YES",
                            col.isPrimaryKey ? "PRI" : "",
                            "NULL",
                            ""
                        });
                    }
                    break;
                }
                case StatementType::INSERT: {
                    auto* iStmt = static_cast<InsertStmt*>(stmt.get());
                    auto table = tableMgr_.getTable(iStmt->tableName);
                    if (!table) {
                        res.success = false;
                        res.message = "ERROR 1146 (42S02): Table '" + iStmt->tableName + "' doesn't exist";
                        break;
                    }

                    const auto& schema = table->getSchema();
                    int insertedCount = 0;

                    for (const auto& rawRow : iStmt->rows) {
                        std::vector<std::string> mappedRow(schema.columns.size());

                        if (iStmt->columns.empty()) {
                            // Direct mapping
                            mappedRow = rawRow;
                        } else {
                            // Column names specified
                            for (size_t c = 0; c < iStmt->columns.size() && c < rawRow.size(); c++) {
                                int colIdx = schema.getColumnIndex(iStmt->columns[c]);
                                if (colIdx >= 0) {
                                    mappedRow[colIdx] = rawRow[c];
                                } else {
                                    res.success = false;
                                    res.message = "ERROR 1054 (42S22): Unknown column '" + iStmt->columns[c] + "' in 'field list'";
                                    return res;
                                }
                            }
                        }

                        std::string err;
                        if (!table->insertRow(mappedRow, err)) {
                            res.success = false;
                            res.message = err;
                            return res;
                        }
                        insertedCount++;
                    }

                    res.message = "Query OK, " + std::to_string(insertedCount) + 
                                  ((insertedCount == 1) ? " row affected" : " rows affected");
                    break;
                }
                case StatementType::SELECT: {
                    auto* sStmt = static_cast<SelectStmt*>(stmt.get());
                    auto table = tableMgr_.getTable(sStmt->tableName);
                    if (!table) {
                        res.success = false;
                        res.message = "ERROR 1146 (42S02): Table '" + sStmt->tableName + "' doesn't exist";
                        break;
                    }

                    const auto& schema = table->getSchema();
                    std::vector<std::vector<std::string>> matchedRows;

                    if (!sStmt->hasWhere) {
                        // Full table scan
                        matchedRows = table->selectAll();
                    } else {
                        int pkIdx = schema.primaryKeyIndex;
                        int whereColIdx = schema.getColumnIndex(sStmt->where.column);

                        if (whereColIdx == -1) {
                            res.success = false;
                            res.message = "ERROR 1054 (42S22): Unknown column '" + sStmt->where.column + "' in 'where clause'";
                            break;
                        }

                        if (whereColIdx == pkIdx) {
                            // Primary Key index query
                            if (sStmt->where.op == "=") {
                                std::vector<std::string> outRow;
                                if (table->selectPoint(sStmt->where.value1, outRow)) {
                                    matchedRows.push_back(outRow);
                                }
                            } else if (sStmt->where.op == "BETWEEN") {
                                matchedRows = table->selectRange(sStmt->where.value1, sStmt->where.value2);
                            } else {
                                // Other operators, scan all and filter
                                auto allRows = table->selectAll();
                                for (const auto& r : allRows) {
                                    if (evalCondition(r[whereColIdx], sStmt->where.op, sStmt->where.value1, schema.columns[whereColIdx].type)) {
                                        matchedRows.push_back(r);
                                    }
                                }
                            }
                        } else {
                            // Non-PK filter: scan and evaluate
                            auto allRows = table->selectAll();
                            for (const auto& r : allRows) {
                                if (evalCondition(r[whereColIdx], sStmt->where.op, sStmt->where.value1, schema.columns[whereColIdx].type)) {
                                    matchedRows.push_back(r);
                                }
                            }
                        }
                    }

                    // Column projection
                    if (sStmt->selectAllColumns) {
                        for (const auto& col : schema.columns) {
                            res.headers.push_back(col.name);
                        }
                        res.rows = matchedRows;
                    } else {
                        std::vector<int> colIndices;
                        for (const auto& reqCol : sStmt->projectedColumns) {
                            int idx = schema.getColumnIndex(reqCol);
                            if (idx == -1) {
                                res.success = false;
                                res.message = "ERROR 1054 (42S22): Unknown column '" + reqCol + "' in 'field list'";
                                return res;
                            }
                            colIndices.push_back(idx);
                            res.headers.push_back(schema.columns[idx].name);
                        }
                        for (const auto& r : matchedRows) {
                            std::vector<std::string> projRow;
                            for (int ci : colIndices) {
                                projRow.push_back(ci < static_cast<int>(r.size()) ? r[ci] : "");
                            }
                            res.rows.push_back(projRow);
                        }
                    }
                    break;
                }
                case StatementType::DELETE: {
                    auto* dStmt = static_cast<DeleteStmt*>(stmt.get());
                    auto table = tableMgr_.getTable(dStmt->tableName);
                    if (!table) {
                        res.success = false;
                        res.message = "ERROR 1146 (42S02): Table '" + dStmt->tableName + "' doesn't exist";
                        break;
                    }

                    if (!dStmt->hasWhere) {
                        res.success = false;
                        res.message = "ERROR: DELETE without WHERE clause is not permitted in safe mode";
                        break;
                    }

                    const auto& schema = table->getSchema();
                    int pkIdx = schema.primaryKeyIndex;
                    int whereColIdx = schema.getColumnIndex(dStmt->where.column);

                    if (whereColIdx != pkIdx || dStmt->where.op != "=") {
                        res.success = false;
                        res.message = "ERROR: Currently DELETE only supports WHERE <primary_key> = <value>";
                        break;
                    }

                    bool deleted = table->deletePoint(dStmt->where.value1);
                    res.message = "Query OK, " + std::string(deleted ? "1 row affected" : "0 rows affected");
                    break;
                }
                case StatementType::PRINT_TREE: {
                    auto* ptStmt = static_cast<PrintTreeStmt*>(stmt.get());
                    auto table = tableMgr_.getTable(ptStmt->tableName);
                    if (!table) {
                        res.success = false;
                        res.message = "ERROR 1146 (42S02): Table '" + ptStmt->tableName + "' doesn't exist";
                        break;
                    }
                    table->printTree();
                    res.message = "Query OK, 0 rows affected";
                    break;
                }
                case StatementType::HELP: {
                    res.headers = {"B-Tree Storage MySQL Query Language", "Description"};
                    res.rows = {
                        {"CREATE TABLE <name> (<col> <type> PRIMARY KEY, ...);", "Create a new table (stored in data/<name>/)"},
                        {"DROP TABLE <name>;", "Drop a table and its disk pages"},
                        {"SHOW TABLES;", "List all tables in data/ directory"},
                        {"DESCRIBE <name>;", "Display table columns, data types, and primary key"},
                        {"INSERT INTO <name> VALUES (...);", "Insert a record into table"},
                        {"SELECT * FROM <name>;", "Full scan of all rows"},
                        {"SELECT * FROM <name> WHERE <pk> = <val>;", "Point lookup via B+ tree search"},
                        {"SELECT * FROM <name> WHERE <pk> BETWEEN <v1> AND <v2>;", "Range scan over leaf pages"},
                        {"DELETE FROM <name> WHERE <pk> = <val>;", "Delete record by primary key"},
                        {"PRINT TREE <name>;", "Visualize multi-level B+ tree layout"},
                        {"EXIT; or QUIT;", "Exit shell"}
                    };
                    break;
                }
                case StatementType::EXIT: {
                    res.isExit = true;
                    res.message = "Bye";
                    break;
                }
                default:
                    res.success = false;
                    res.message = "ERROR: Unimplemented statement";
                    break;
            }
        } catch (const std::exception& e) {
            res.success = false;
            res.message = e.what();
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        res.elapsedSeconds = diff.count();
        return res;
    }

private:
    TableManager tableMgr_;

    bool evalCondition(const std::string& cell, const std::string& op, const std::string& target, DataType type) {
        if (type == DataType::INT || type == DataType::BIGINT) {
            int64_t v1 = std::stoll(cell);
            int64_t v2 = std::stoll(target);
            if (op == "=")  return v1 == v2;
            if (op == "!=" || op == "<>") return v1 != v2;
            if (op == "<")  return v1 < v2;
            if (op == "<=") return v1 <= v2;
            if (op == ">")  return v1 > v2;
            if (op == ">=") return v1 >= v2;
        } else if (type == DataType::FLOAT || type == DataType::DOUBLE) {
            double v1 = std::stod(cell);
            double v2 = std::stod(target);
            if (op == "=")  return v1 == v2;
            if (op == "!=" || op == "<>") return v1 != v2;
            if (op == "<")  return v1 < v2;
            if (op == "<=") return v1 <= v2;
            if (op == ">")  return v1 > v2;
            if (op == ">=") return v1 >= v2;
        } else {
            if (op == "=")  return cell == target;
            if (op == "!=" || op == "<>") return cell != target;
            if (op == "<")  return cell < target;
            if (op == "<=") return cell <= target;
            if (op == ">")  return cell > target;
            if (op == ">=") return cell >= target;
        }
        return false;
    }
};

#endif // SQL_ENGINE_H
