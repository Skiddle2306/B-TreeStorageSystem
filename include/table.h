#ifndef TABLE_H
#define TABLE_H

#include "common.h"
#include "schema.h"
#include "b_plus_tree.h"
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <type_traits>

// ─────────────────────────────────────────────────────────────────────────────
// Type conversion helpers
// ─────────────────────────────────────────────────────────────────────────────
template<typename K>
inline K parseKey(const std::string& str) {
    if constexpr (std::is_same_v<K, int>) {
        return std::stoi(str);
    } else if constexpr (std::is_same_v<K, int64_t>) {
        return std::stoll(str);
    } else if constexpr (std::is_same_v<K, float>) {
        return std::stof(str);
    } else if constexpr (std::is_same_v<K, double>) {
        return std::stod(str);
    } else if constexpr (std::is_same_v<K, char>) {
        return str.empty() ? ' ' : str[0];
    } else {
        return K(str);
    }
}

template<typename K>
inline std::string formatKey(const K& key) {
    if constexpr (std::is_same_v<K, int>) {
        return std::to_string(key);
    } else if constexpr (std::is_same_v<K, int64_t>) {
        return std::to_string(key);
    } else if constexpr (std::is_same_v<K, float>) {
        std::ostringstream oss;
        oss << std::setprecision(4) << key;
        return oss.str();
    } else if constexpr (std::is_same_v<K, double>) {
        std::ostringstream oss;
        oss << std::setprecision(6) << key;
        return oss.str();
    } else if constexpr (std::is_same_v<K, char>) {
        return std::string(1, key);
    } else {
        return key.str();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Abstract Table Interface
// ─────────────────────────────────────────────────────────────────────────────
class ITable {
public:
    virtual ~ITable() = default;
    virtual const TableSchema& getSchema() const = 0;
    virtual bool insertRow(const std::vector<std::string>& colValues, std::string& errorMsg) = 0;
    virtual bool selectPoint(const std::string& keyStr, std::vector<std::string>& outRow) = 0;
    virtual std::vector<std::vector<std::string>> selectRange(const std::string& lowerStr, const std::string& upperStr) = 0;
    virtual std::vector<std::vector<std::string>> selectAll() = 0;
    virtual bool deletePoint(const std::string& keyStr) = 0;
    virtual void printTree() = 0;
    virtual void flush() = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Concrete SqlTable<K, V> wrapping BPlusTree<K, V>
// ─────────────────────────────────────────────────────────────────────────────
template<typename K, typename V = DefaultRowPayload>
class SqlTable : public ITable {
public:
    SqlTable(const TableSchema& schema, const std::string& treeName)
        : schema_(schema), tree_(std::make_unique<BPlusTree<K, V>>(treeName)) {}

    const TableSchema& getSchema() const override {
        return schema_;
    }

    bool insertRow(const std::vector<std::string>& colValues, std::string& errorMsg) override {
        if (colValues.size() > schema_.columns.size()) {
            errorMsg = "ERROR 1136 (21S01): Column count doesn't match value count";
            return false;
        }

        int pkIdx = schema_.primaryKeyIndex;
        if (pkIdx < 0 || pkIdx >= static_cast<int>(colValues.size())) {
            errorMsg = "ERROR 1364 (HY000): Field '" + 
                       (schema_.getPKColumn() ? schema_.getPKColumn()->name : "PRIMARY") + 
                       "' doesn't have a default value";
            return false;
        }

        K key;
        try {
            key = parseKey<K>(colValues[pkIdx]);
        } catch (const std::exception& e) {
            errorMsg = "ERROR 1366 (HY000): Incorrect integer/key value: '" + colValues[pkIdx] + "'";
            return false;
        }

        V existing{};
        if (tree_->search(key, existing)) {
            errorMsg = "ERROR 1062 (23000): Duplicate entry '" + colValues[pkIdx] + "' for key 'PRIMARY'";
            return false;
        }

        V payload{};
        if constexpr (std::is_same_v<V, DefaultRowPayload> || std::is_same_v<V, RowPayload<256>> || std::is_same_v<V, RowPayload<512>>) {
            for (size_t i = 0; i < schema_.columns.size(); i++) {
                if (static_cast<int>(i) == pkIdx) continue;
                const auto& col = schema_.columns[i];
                std::string valStr = (i < colValues.size()) ? colValues[i] : "";

                try {
                    switch (col.type) {
                        case DataType::INT: {
                            int32_t v = valStr.empty() ? 0 : std::stoi(valStr);
                            std::memcpy(payload.data + col.offset, &v, 4);
                            break;
                        }
                        case DataType::BIGINT: {
                            int64_t v = valStr.empty() ? 0 : std::stoll(valStr);
                            std::memcpy(payload.data + col.offset, &v, 8);
                            break;
                        }
                        case DataType::FLOAT: {
                            float v = valStr.empty() ? 0.0f : std::stof(valStr);
                            std::memcpy(payload.data + col.offset, &v, 4);
                            break;
                        }
                        case DataType::DOUBLE: {
                            double v = valStr.empty() ? 0.0 : std::stod(valStr);
                            std::memcpy(payload.data + col.offset, &v, 8);
                            break;
                        }
                        case DataType::VARCHAR: {
                            std::memset(payload.data + col.offset, 0, col.size);
                            if (!valStr.empty()) {
                                std::strncpy(payload.data + col.offset, valStr.c_str(), col.size - 1);
                            }
                            break;
                        }
                        case DataType::CHAR: {
                            std::memset(payload.data + col.offset, 0, col.size);
                            if (!valStr.empty()) {
                                std::strncpy(payload.data + col.offset, valStr.c_str(), col.size);
                            }
                            break;
                        }
                    }
                } catch (const std::exception& e) {
                    errorMsg = "ERROR 1366 (HY000): Incorrect value for column '" + col.name + "': '" + valStr + "'";
                    return false;
                }
            }
        }

        tree_->insert(key, payload);
        tree_->flush();
        return true;
    }

    bool selectPoint(const std::string& keyStr, std::vector<std::string>& outRow) override {
        K key;
        try {
            key = parseKey<K>(keyStr);
        } catch (...) {
            return false;
        }

        V payload{};
        if (!tree_->search(key, payload)) {
            return false;
        }

        outRow = decodeRow(key, payload);
        return true;
    }

    std::vector<std::vector<std::string>> selectRange(const std::string& lowerStr, const std::string& upperStr) override {
        std::vector<std::vector<std::string>> rows;
        K lower, upper;
        try {
            lower = parseKey<K>(lowerStr);
            upper = parseKey<K>(upperStr);
        } catch (...) {
            return rows;
        }

        auto pairs = tree_->scan(lower, upper);
        rows.reserve(pairs.size());
        for (const auto& [k, v] : pairs) {
            rows.push_back(decodeRow(k, v));
        }
        return rows;
    }

    std::vector<std::vector<std::string>> selectAll() override {
        std::vector<std::vector<std::string>> rows;
        auto pairs = tree_->scanAll();
        rows.reserve(pairs.size());
        for (const auto& [k, v] : pairs) {
            rows.push_back(decodeRow(k, v));
        }
        return rows;
    }

    bool deletePoint(const std::string& keyStr) override {
        K key;
        try {
            key = parseKey<K>(keyStr);
        } catch (...) {
            return false;
        }

        bool deleted = tree_->deleteNode(key, false);
        if (deleted) {
            tree_->flush();
        }
        return deleted;
    }

    void printTree() override {
        std::cout << "=== B+ Tree: " << schema_.tableName << " ===" << std::endl;
        tree_->printTree();
    }

    void flush() override {
        tree_->flush();
    }

private:
    TableSchema schema_;
    std::unique_ptr<BPlusTree<K, V>> tree_;

    std::vector<std::string> decodeRow(const K& key, const V& payload) const {
        std::vector<std::string> row(schema_.columns.size());
        int pkIdx = schema_.primaryKeyIndex;
        if (pkIdx >= 0 && pkIdx < static_cast<int>(row.size())) {
            row[pkIdx] = formatKey<K>(key);
        }

        if constexpr (std::is_same_v<V, DefaultRowPayload> || std::is_same_v<V, RowPayload<256>> || std::is_same_v<V, RowPayload<512>>) {
            for (size_t i = 0; i < schema_.columns.size(); i++) {
                if (static_cast<int>(i) == pkIdx) continue;
                const auto& col = schema_.columns[i];

                switch (col.type) {
                    case DataType::INT: {
                        int32_t val = 0;
                        std::memcpy(&val, payload.data + col.offset, 4);
                        row[i] = std::to_string(val);
                        break;
                    }
                    case DataType::BIGINT: {
                        int64_t val = 0;
                        std::memcpy(&val, payload.data + col.offset, 8);
                        row[i] = std::to_string(val);
                        break;
                    }
                    case DataType::FLOAT: {
                        float val = 0.0f;
                        std::memcpy(&val, payload.data + col.offset, 4);
                        std::ostringstream oss;
                        oss << std::setprecision(4) << val;
                        row[i] = oss.str();
                        break;
                    }
                    case DataType::DOUBLE: {
                        double val = 0.0;
                        std::memcpy(&val, payload.data + col.offset, 8);
                        std::ostringstream oss;
                        oss << std::setprecision(6) << val;
                        row[i] = oss.str();
                        break;
                    }
                    case DataType::VARCHAR: {
                        row[i] = std::string(payload.data + col.offset, strnlen(payload.data + col.offset, col.size));
                        break;
                    }
                    case DataType::CHAR: {
                        row[i] = std::string(payload.data + col.offset, strnlen(payload.data + col.offset, col.size));
                        break;
                    }
                }
            }
        }
        return row;
    }
};

#endif // TABLE_H
