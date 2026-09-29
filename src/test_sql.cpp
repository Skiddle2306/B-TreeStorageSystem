#include "sql_engine.h"
#include <iostream>
#include <cassert>

void runSqlTests() {
    std::cout << "========================================\n";
    std::cout << "     RUNNING SQL QUERY ENGINE TESTS     \n";
    std::cout << "========================================\n\n";

    SqlEngine engine("data");

    // Clean up if already exists
    engine.execute("DROP TABLE IF EXISTS employees;");

    // 1. CREATE TABLE
    std::cout << "--- 1. Testing CREATE TABLE ---\n";
    {
        QueryResult res = engine.execute(
            "CREATE TABLE employees (\n"
            "    id INT PRIMARY KEY,\n"
            "    name VARCHAR(32),\n"
            "    salary FLOAT,\n"
            "    dept_id INT\n"
            ");"
        );
        std::cout << res.formatAsciiGrid();
        assert(res.success);
    }

    // 2. SHOW TABLES
    std::cout << "\n--- 2. Testing SHOW TABLES ---\n";
    {
        QueryResult res = engine.execute("SHOW TABLES;");
        std::cout << res.formatAsciiGrid();
        assert(res.success);
        bool found = false;
        for (const auto& r : res.rows) {
            if (r[0] == "employees") found = true;
        }
        assert(found);
    }

    // 3. DESCRIBE TABLE
    std::cout << "\n--- 3. Testing DESCRIBE employees ---\n";
    {
        QueryResult res = engine.execute("DESCRIBE employees;");
        std::cout << res.formatAsciiGrid();
        assert(res.success);
        assert(res.rows.size() == 4);
        assert(res.rows[0][0] == "id" && res.rows[0][3] == "PRI");
    }

    // 4. INSERT INTO
    std::cout << "\n--- 4. Testing INSERT INTO employees ---\n";
    {
        QueryResult r1 = engine.execute("INSERT INTO employees VALUES (101, 'Alice', 85000.5, 10);");
        std::cout << r1.formatAsciiGrid();
        assert(r1.success);

        QueryResult r2 = engine.execute("INSERT INTO employees VALUES (102, 'Bob', 62000.0, 20);");
        std::cout << r2.formatAsciiGrid();
        assert(r2.success);

        QueryResult r3 = engine.execute("INSERT INTO employees VALUES (103, 'Charlie', 74000.75, 10);");
        std::cout << r3.formatAsciiGrid();
        assert(r3.success);

        QueryResult r4 = engine.execute("INSERT INTO employees VALUES (104, 'Diana', 91000.0, 30);");
        std::cout << r4.formatAsciiGrid();
        assert(r4.success);

        // Duplicate key check
        QueryResult rDup = engine.execute("INSERT INTO employees VALUES (101, 'Duplicate', 0, 0);");
        std::cout << "Duplicate key test output: " << rDup.formatAsciiGrid();
        assert(!rDup.success);
    }

    // 5. SELECT * (Full table scan)
    std::cout << "\n--- 5. Testing SELECT * (Full Scan) ---\n";
    {
        QueryResult res = engine.execute("SELECT * FROM employees;");
        std::cout << res.formatAsciiGrid();
        assert(res.success);
        assert(res.rows.size() == 4);
    }

    // 6. SELECT point lookup (WHERE id = 102)
    std::cout << "\n--- 6. Testing Point SELECT WHERE id = 102 ---\n";
    {
        QueryResult res = engine.execute("SELECT id, name, salary FROM employees WHERE id = 102;");
        std::cout << res.formatAsciiGrid();
        assert(res.success);
        assert(res.rows.size() == 1);
        assert(res.rows[0][0] == "102");
        assert(res.rows[0][1] == "Bob");
    }

    // 7. SELECT range scan (WHERE id BETWEEN 102 AND 104)
    std::cout << "\n--- 7. Testing Range SELECT BETWEEN 102 AND 104 ---\n";
    {
        QueryResult res = engine.execute("SELECT * FROM employees WHERE id BETWEEN 102 AND 104;");
        std::cout << res.formatAsciiGrid();
        assert(res.success);
        assert(res.rows.size() == 3);
    }

    // 8. PRINT TREE
    std::cout << "\n--- 8. Testing PRINT TREE employees ---\n";
    {
        QueryResult res = engine.execute("PRINT TREE employees;");
        std::cout << res.formatAsciiGrid();
        assert(res.success);
    }

    // 9. DELETE WHERE id = 102
    std::cout << "\n--- 9. Testing DELETE WHERE id = 102 ---\n";
    {
        QueryResult res = engine.execute("DELETE FROM employees WHERE id = 102;");
        std::cout << res.formatAsciiGrid();
        assert(res.success);

        // Verify Bob is gone
        QueryResult check = engine.execute("SELECT * FROM employees WHERE id = 102;");
        std::cout << check.formatAsciiGrid();
        assert(check.rows.empty());

        // Verify remaining rows count is 3
        QueryResult all = engine.execute("SELECT * FROM employees;");
        assert(all.rows.size() == 3);
    }

    // 10. Persistence verification across new engine instance
    std::cout << "\n--- 10. Testing Persistence (Re-opening database) ---\n";
    {
        SqlEngine newEngine("data");
        QueryResult res = newEngine.execute("SELECT * FROM employees;");
        std::cout << res.formatAsciiGrid();
        assert(res.success);
        assert(res.rows.size() == 3);
    }

    // 11. Existing table compatibility (inspecting table 'bob' if present)
    std::cout << "\n--- 11. Testing Compatibility with existing table 'bob' ---\n";
    {
        if (engine.execute("SHOW TABLES;").formatAsciiGrid().find("bob") != std::string::npos) {
            QueryResult desc = engine.execute("DESCRIBE bob;");
            std::cout << desc.formatAsciiGrid();

            QueryResult sel = engine.execute("SELECT * FROM bob WHERE key = 1000;");
            std::cout << sel.formatAsciiGrid();

            QueryResult tree = engine.execute("PRINT TREE bob;");
            std::cout << tree.formatAsciiGrid();
        }
    }

    // 12. DROP TABLE
    std::cout << "\n--- 12. Testing DROP TABLE employees ---\n";
    {
        QueryResult res = engine.execute("DROP TABLE employees;");
        std::cout << res.formatAsciiGrid();
        assert(res.success);

        QueryResult check = engine.execute("SELECT * FROM employees;");
        assert(!check.success); // Should fail because table is dropped
    }

    std::cout << "\n========================================\n";
    std::cout << "     ALL SQL ENGINE TESTS PASSED!       \n";
    std::cout << "========================================\n";
}

int main() {
    runSqlTests();
    return 0;
}
