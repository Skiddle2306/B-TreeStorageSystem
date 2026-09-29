#ifndef TABLE_MANAGER_H
#define TABLE_MANAGER_H

#include "common.h"
#include "schema.h"
#include "table.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <filesystem>
#include <algorithm>

class TableManager {
public:
    explicit TableManager(const std::string& baseDir = "data") : baseDir_(baseDir) {
        std::filesystem::create_directories(baseDir_);
    }

    ~TableManager() {
        flushAll();
    }

    bool tableExists(const std::string& name) const {
        std::string folder = baseDir_ + "/" + name;
        return std::filesystem::is_directory(folder) && 
               std::filesystem::exists(folder + "/tree.db");
    }

    std::vector<std::string> listTables() const {
        std::vector<std::string> tables;
        if (!std::filesystem::exists(baseDir_)) return tables;

        for (const auto& entry : std::filesystem::directory_iterator(baseDir_)) {
            if (entry.is_directory()) {
                std::string dbPath = entry.path().string() + "/tree.db";
                if (std::filesystem::exists(dbPath)) {
                    tables.push_back(entry.path().filename().string());
                }
            }
        }
        std::sort(tables.begin(), tables.end());
        return tables;
    }

    std::shared_ptr<ITable> getTable(const std::string& name) {
        auto it = tableCache_.find(name);
        if (it != tableCache_.end()) {
            return it->second;
        }

        if (!tableExists(name)) {
            return nullptr;
        }

        std::string folder = baseDir_ + "/" + name;
        std::string schemaPath = folder + "/schema.dat";
        std::string metaPath = folder + "/meta.dat";

        TableSchema schema;
        bool hasSchema = false;

        if (std::filesystem::exists(schemaPath)) {
            hasSchema = TableSchema::loadFromFile(schemaPath, schema);
        }

        TreeMetadata meta{};
        bool hasMeta = false;
        if (std::filesystem::exists(metaPath)) {
            std::ifstream ifs(metaPath, std::ios::binary);
            if (ifs.read(reinterpret_cast<char*>(&meta), sizeof(meta)) && 
                ifs.gcount() == sizeof(meta) && meta.magic == DB_META_MAGIC) {
                hasMeta = true;
            }
        }

        if (!hasSchema) {
            if (hasMeta) {
                schema = TableSchema::fromMetadata(name, meta);
            } else {
                return nullptr;
            }
        }

        std::shared_ptr<ITable> table = instantiateTable(name, schema, hasMeta ? &meta : nullptr);
        if (table) {
            tableCache_[name] = table;
        }
        return table;
    }

    bool createTable(TableSchema schema, std::string& errorMsg) {
        if (tableExists(schema.tableName)) {
            errorMsg = "ERROR 1050 (42S01): Table '" + schema.tableName + "' already exists";
            return false;
        }

        schema.finalizeOffsets();

        if (schema.totalPayloadSize > 256) {
            errorMsg = "ERROR 1118 (42000): Row size too large (max 256 bytes payload for current page configuration)";
            return false;
        }

        std::string folder = baseDir_ + "/" + schema.tableName;
        std::filesystem::create_directories(folder);

        std::string schemaPath = folder + "/schema.dat";
        if (!schema.saveToFile(schemaPath)) {
            errorMsg = "ERROR: Failed to write schema file: " + schemaPath;
            return false;
        }

        auto table = instantiateTable(schema.tableName, schema, nullptr);
        if (!table) {
            errorMsg = "ERROR: Failed to instantiate storage for table: " + schema.tableName;
            return false;
        }

        tableCache_[schema.tableName] = table;
        return true;
    }

    bool dropTable(const std::string& name, std::string& errorMsg) {
        if (!tableExists(name)) {
            errorMsg = "ERROR 1051 (42S02): Unknown table '" + name + "'";
            return false;
        }

        tableCache_.erase(name);

        std::string folder = baseDir_ + "/" + name;
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
        if (ec) {
            errorMsg = "ERROR: Failed to remove directory: " + ec.message();
            return false;
        }
        return true;
    }

    void flushAll() {
        for (auto& [name, table] : tableCache_) {
            if (table) {
                table->flush();
            }
        }
    }

private:
    std::string baseDir_;
    std::unordered_map<std::string, std::shared_ptr<ITable>> tableCache_;

    std::shared_ptr<ITable> instantiateTable(const std::string& name, const TableSchema& schema, const TreeMetadata* meta) {
        const ColumnDef* pk = schema.getPKColumn();
        if (!pk) return nullptr;

        bool isKeyOnly = (meta && meta->valDiskSize == 1) || (schema.columns.size() == 1);

        switch (pk->type) {
            case DataType::INT:
                if (isKeyOnly) {
                    return std::make_shared<SqlTable<int, char>>(schema, name);
                } else {
                    return std::make_shared<SqlTable<int, DefaultRowPayload>>(schema, name);
                }
            case DataType::BIGINT:
                if (isKeyOnly) {
                    return std::make_shared<SqlTable<int64_t, char>>(schema, name);
                } else {
                    return std::make_shared<SqlTable<int64_t, DefaultRowPayload>>(schema, name);
                }
            case DataType::FLOAT:
                if (isKeyOnly) {
                    return std::make_shared<SqlTable<float, char>>(schema, name);
                } else {
                    return std::make_shared<SqlTable<float, DefaultRowPayload>>(schema, name);
                }
            case DataType::DOUBLE:
                if (isKeyOnly) {
                    return std::make_shared<SqlTable<double, char>>(schema, name);
                } else {
                    return std::make_shared<SqlTable<double, DefaultRowPayload>>(schema, name);
                }
            case DataType::VARCHAR:
            case DataType::CHAR:
                if (pk->length <= 32) {
                    if (isKeyOnly) {
                        return std::make_shared<SqlTable<FixedString<32>, char>>(schema, name);
                    } else {
                        return std::make_shared<SqlTable<FixedString<32>, DefaultRowPayload>>(schema, name);
                    }
                } else if (pk->length <= 64) {
                    if (isKeyOnly) {
                        return std::make_shared<SqlTable<FixedString<64>, char>>(schema, name);
                    } else {
                        return std::make_shared<SqlTable<FixedString<64>, DefaultRowPayload>>(schema, name);
                    }
                } else {
                    if (isKeyOnly) {
                        return std::make_shared<SqlTable<FixedString<256>, char>>(schema, name);
                    } else {
                        return std::make_shared<SqlTable<FixedString<256>, DefaultRowPayload>>(schema, name);
                    }
                }
            default:
                return nullptr;
        }
    }
};

#endif // TABLE_MANAGER_H
