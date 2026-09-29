#ifndef SCHEMA_H
#define SCHEMA_H

#include "common.h"
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstdint>
#include <cstring>

enum class DataType : uint8_t {
    INT      = 1,
    BIGINT   = 2,
    FLOAT    = 3,
    DOUBLE   = 4,
    VARCHAR  = 5,
    CHAR     = 6,
};

inline std::string dataTypeToString(DataType type, uint32_t length = 0) {
    switch (type) {
        case DataType::INT:     return "int";
        case DataType::BIGINT:  return "bigint";
        case DataType::FLOAT:   return "float";
        case DataType::DOUBLE:  return "double";
        case DataType::VARCHAR: return "varchar(" + std::to_string(length ? length : 256) + ")";
        case DataType::CHAR:    return (length > 1) ? ("char(" + std::to_string(length) + ")") : "char(1)";
        default:                return "unknown";
    }
}

inline DataType stringToDataType(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    if (s == "INT" || s == "INTEGER") return DataType::INT;
    if (s == "BIGINT") return DataType::BIGINT;
    if (s == "FLOAT") return DataType::FLOAT;
    if (s == "DOUBLE") return DataType::DOUBLE;
    if (s == "VARCHAR") return DataType::VARCHAR;
    if (s == "CHAR") return DataType::CHAR;
    throw std::runtime_error("Unknown data type: " + str);
}

struct ColumnDef {
    std::string name;
    DataType    type = DataType::INT;
    uint32_t    length = 0;       // max string length for VARCHAR/CHAR
    bool        isPrimaryKey = false;
    bool        notNull = false;
    uint32_t    offset = 0;       // byte offset in row payload buffer (for non-PK)
    uint32_t    size = 0;         // byte size on disk

    uint32_t calculateSize() const {
        switch (type) {
            case DataType::INT:     return 4;
            case DataType::BIGINT:  return 8;
            case DataType::FLOAT:   return 4;
            case DataType::DOUBLE:  return 8;
            case DataType::VARCHAR: return length > 0 ? length : 256;
            case DataType::CHAR:    return length > 0 ? length : 1;
            default:                return 4;
        }
    }

    std::string typeString() const {
        return dataTypeToString(type, length);
    }
};

struct TableSchema {
    std::string            tableName;
    std::vector<ColumnDef> columns;
    int                    primaryKeyIndex = -1;
    uint32_t               totalPayloadSize = 0;

    int getColumnIndex(const std::string& colName) const {
        std::string target = colName;
        std::transform(target.begin(), target.end(), target.begin(), ::tolower);
        for (size_t i = 0; i < columns.size(); i++) {
            std::string c = columns[i].name;
            std::transform(c.begin(), c.end(), c.begin(), ::tolower);
            if (c == target) return static_cast<int>(i);
        }
        return -1;
    }

    const ColumnDef* getPKColumn() const {
        if (primaryKeyIndex >= 0 && primaryKeyIndex < static_cast<int>(columns.size())) {
            return &columns[primaryKeyIndex];
        }
        return nullptr;
    }

    void finalizeOffsets() {
        uint32_t curOffset = 0;
        primaryKeyIndex = -1;
        for (size_t i = 0; i < columns.size(); i++) {
            columns[i].size = columns[i].calculateSize();
            if (columns[i].isPrimaryKey) {
                if (primaryKeyIndex != -1) {
                    throw std::runtime_error("Table can have only one PRIMARY KEY");
                }
                primaryKeyIndex = static_cast<int>(i);
                columns[i].offset = 0;
            }
        }

        if (primaryKeyIndex == -1 && !columns.empty()) {
            // Default first column as primary key if not explicitly set
            primaryKeyIndex = 0;
            columns[0].isPrimaryKey = true;
        }

        for (size_t i = 0; i < columns.size(); i++) {
            if (!columns[i].isPrimaryKey) {
                columns[i].offset = curOffset;
                curOffset += columns[i].size;
            }
        }
        totalPayloadSize = curOffset;
    }

    bool saveToFile(const std::string& filepath) const {
        std::ofstream ofs(filepath);
        if (!ofs.is_open()) return false;
        ofs << "SCHEMA_V1 " << tableName << "\n";
        ofs << "COLUMNS " << columns.size() << "\n";
        for (const auto& col : columns) {
            ofs << col.name << " "
                << static_cast<int>(col.type) << " "
                << col.length << " "
                << (col.isPrimaryKey ? 1 : 0) << " "
                << (col.notNull ? 1 : 0) << " "
                << col.offset << " "
                << col.size << "\n";
        }
        ofs << "PK_INDEX " << primaryKeyIndex << "\n";
        ofs << "PAYLOAD_SIZE " << totalPayloadSize << "\n";
        return true;
    }

    static bool loadFromFile(const std::string& filepath, TableSchema& outSchema) {
        std::ifstream ifs(filepath);
        if (!ifs.is_open()) return false;
        std::string header, name;
        if (!(ifs >> header >> name) || header != "SCHEMA_V1") return false;
        outSchema.tableName = name;
        std::string colTag;
        size_t numCols = 0;
        if (!(ifs >> colTag >> numCols) || colTag != "COLUMNS") return false;
        outSchema.columns.clear();
        for (size_t i = 0; i < numCols; i++) {
            ColumnDef col;
            int typeInt, isPkInt, notNullInt;
            if (!(ifs >> col.name >> typeInt >> col.length >> isPkInt >> notNullInt >> col.offset >> col.size)) {
                return false;
            }
            col.type = static_cast<DataType>(typeInt);
            col.isPrimaryKey = (isPkInt != 0);
            col.notNull = (notNullInt != 0);
            outSchema.columns.push_back(col);
        }
        std::string pkTag, payloadTag;
        if (ifs >> pkTag >> outSchema.primaryKeyIndex && pkTag == "PK_INDEX") {
            ifs >> payloadTag >> outSchema.totalPayloadSize;
        } else {
            outSchema.finalizeOffsets();
        }
        return true;
    }

    static TableSchema fromMetadata(const std::string& tableName, const TreeMetadata& meta) {
        TableSchema schema;
        schema.tableName = tableName;

        ColumnDef pkCol;
        pkCol.name = "key";
        pkCol.isPrimaryKey = true;
        pkCol.notNull = true;
        pkCol.offset = 0;
        pkCol.size = meta.keyDiskSize;

        switch (static_cast<KeyTypeId>(meta.keyTypeId)) {
            case KeyTypeId::INT:
                pkCol.type = DataType::INT;
                pkCol.length = 0;
                break;
            case KeyTypeId::FLOAT:
                pkCol.type = DataType::FLOAT;
                pkCol.length = 0;
                break;
            case KeyTypeId::DOUBLE:
                pkCol.type = DataType::DOUBLE;
                pkCol.length = 0;
                break;
            case KeyTypeId::CHAR:
                pkCol.type = DataType::CHAR;
                pkCol.length = 1;
                break;
            case KeyTypeId::FIXED_STRING:
                pkCol.type = DataType::VARCHAR;
                pkCol.length = meta.keyDiskSize;
                break;
            default:
                if (std::string(meta.typeName) == "bigint") {
                    pkCol.type = DataType::BIGINT;
                    pkCol.length = 0;
                } else {
                    pkCol.type = DataType::VARCHAR;
                    pkCol.length = meta.keyDiskSize;
                }
                break;
        }
        schema.columns.push_back(pkCol);

        // If valDiskSize > 1 and it's not a row_payload, add a value column
        if (meta.valDiskSize > 1 && std::string(meta.valTypeName) != "row_payload") {
            ColumnDef valCol;
            valCol.name = "value";
            valCol.isPrimaryKey = false;
            valCol.notNull = false;
            valCol.type = DataType::VARCHAR;
            valCol.length = meta.valDiskSize;
            valCol.offset = 0;
            valCol.size = meta.valDiskSize;
            schema.columns.push_back(valCol);
        }

        schema.finalizeOffsets();
        return schema;
    }
};

#endif // SCHEMA_H
