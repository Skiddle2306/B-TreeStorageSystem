#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <cstring>
#include <string>
#include <iostream>
#include <compare>

// ─────────────────────────────────────────────────────────────────────────────
// Core constants
// ─────────────────────────────────────────────────────────────────────────────
constexpr int     PAGE_SIZE           = 4096;
constexpr int32_t INVALID_PAGE        = -1;

#define DEFAULT_STRING_LENGTH 256   // max on-disk bytes for a string / char key

using page_id_t = int32_t;

enum class PageType : uint8_t {
    LEAF     = 0,
    INTERNAL = 1,
    FREE     = 2,
    HEADER   = 3,
};

// ─────────────────────────────────────────────────────────────────────────────
// FixedString<N> / FixedChar<N>
//
// Fixed-size char array wrapper designed for on-disk persistence.
// Fits into LeafPage/InternalPage with a fixed size N (up to 256 bytes).
// Supports standard comparisons, string conversions, and stream operators.
// ─────────────────────────────────────────────────────────────────────────────
template<size_t N = DEFAULT_STRING_LENGTH>
struct FixedString {
    static_assert(N > 0 && N <= DEFAULT_STRING_LENGTH, "FixedString size must be between 1 and 256 bytes");
    char data[N];

    FixedString() {
        std::memset(data, 0, N);
    }

    FixedString(const char* str) {
        std::memset(data, 0, N);
        if (str) {
            std::strncpy(data, str, N - 1);
        }
    }

    FixedString(const std::string& str) {
        std::memset(data, 0, N);
        std::strncpy(data, str.c_str(), N - 1);
    }

    std::string str() const {
        return std::string(data, strnlen(data, N));
    }

    const char* c_str() const {
        return data;
    }

    operator std::string() const {
        return str();
    }

    bool operator==(const FixedString<N>& other) const {
        return std::strncmp(data, other.data, N) == 0;
    }
    bool operator!=(const FixedString<N>& other) const {
        return std::strncmp(data, other.data, N) != 0;
    }
    bool operator<(const FixedString<N>& other) const {
        return std::strncmp(data, other.data, N) < 0;
    }
    bool operator<=(const FixedString<N>& other) const {
        return std::strncmp(data, other.data, N) <= 0;
    }
    bool operator>(const FixedString<N>& other) const {
        return std::strncmp(data, other.data, N) > 0;
    }
    bool operator>=(const FixedString<N>& other) const {
        return std::strncmp(data, other.data, N) >= 0;
    }

    friend std::ostream& operator<<(std::ostream& os, const FixedString<N>& fs) {
        os << fs.data;
        return os;
    }

    friend std::istream& operator>>(std::istream& is, FixedString<N>& fs) {
        std::string s;
        if (is >> s) {
            fs = FixedString<N>(s);
        }
        return is;
    }
};

// Aliases for convenience
using StringKey = FixedString<DEFAULT_STRING_LENGTH>;
template<size_t N>
using FixedChar = FixedString<N>;

// ─────────────────────────────────────────────────────────────────────────────
// RowPayload<N>
//
// Generic fixed-size row payload storage for relational tables in SQL engine.
// Fits in LeafPage<K, RowPayload<N>> with zero heap allocation.
// ─────────────────────────────────────────────────────────────────────────────
template<size_t N = 256>
struct RowPayload {
    char data[N];

    RowPayload() {
        std::memset(data, 0, N);
    }

    bool operator==(const RowPayload<N>& other) const {
        return std::memcmp(data, other.data, N) == 0;
    }
    bool operator!=(const RowPayload<N>& other) const {
        return !(*this == other);
    }
    bool operator<(const RowPayload<N>& other) const {
        return std::memcmp(data, other.data, N) < 0;
    }
    bool operator<=(const RowPayload<N>& other) const {
        return std::memcmp(data, other.data, N) <= 0;
    }
    bool operator>(const RowPayload<N>& other) const {
        return std::memcmp(data, other.data, N) > 0;
    }
    bool operator>=(const RowPayload<N>& other) const {
        return std::memcmp(data, other.data, N) >= 0;
    }

    friend std::ostream& operator<<(std::ostream& os, const RowPayload<N>&) {
        os << "[RowPayload " << N << "B]";
        return os;
    }
};

using DefaultRowPayload = RowPayload<256>;

// ─────────────────────────────────────────────────────────────────────────────
// KeyTraits<K>
//
// Decouples "how big is K on disk" from "what K looks like in C++".
// diskSize  — bytes this key occupies in a page buffer
// write     — serialise a K into a raw byte region
// read      — deserialise a K from a raw byte region
// ─────────────────────────────────────────────────────────────────────────────
template<typename K>
struct KeyTraits {
    // Default: trivially copyable types (int, float, double, char, structs, FixedString<N>, …)
    static constexpr int diskSize = sizeof(K);

    static void write(char* dst, const K& val) {
        std::memcpy(dst, &val, sizeof(K));
    }
    static K read(const char* src) {
        K val;
        std::memcpy(&val, src, sizeof(K));
        return val;
    }
};

// Specialisation for std::string — fixed DEFAULT_STRING_LENGTH bytes on disk
template<>
struct KeyTraits<std::string> {
    static constexpr int diskSize = DEFAULT_STRING_LENGTH;

    static void write(char* dst, const std::string& val) {
        std::memset(dst, 0, DEFAULT_STRING_LENGTH);
        std::strncpy(dst, val.c_str(), DEFAULT_STRING_LENGTH - 1);
    }
    static std::string read(const char* src) {
        return std::string(src, strnlen(src, DEFAULT_STRING_LENGTH));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// KeyTypeId and Metadata structure for meta.dat
// ─────────────────────────────────────────────────────────────────────────────
enum class KeyTypeId : uint32_t {
    UNKNOWN      = 0,
    INT          = 1,
    FLOAT        = 2,
    DOUBLE       = 3,
    CHAR         = 4,
    FIXED_STRING = 5,
    CUSTOM       = 6,
};

template<typename K>
struct KeyTypeInfo {
    static constexpr KeyTypeId typeId = KeyTypeId::CUSTOM;
    static constexpr const char* name = "custom";
};

template<>
struct KeyTypeInfo<int> {
    static constexpr KeyTypeId typeId = KeyTypeId::INT;
    static constexpr const char* name = "int";
};

template<>
struct KeyTypeInfo<float> {
    static constexpr KeyTypeId typeId = KeyTypeId::FLOAT;
    static constexpr const char* name = "float";
};

template<>
struct KeyTypeInfo<double> {
    static constexpr KeyTypeId typeId = KeyTypeId::DOUBLE;
    static constexpr const char* name = "double";
};

template<>
struct KeyTypeInfo<char> {
    static constexpr KeyTypeId typeId = KeyTypeId::CHAR;
    static constexpr const char* name = "char";
};

template<>
struct KeyTypeInfo<std::string> {
    static constexpr KeyTypeId typeId = KeyTypeId::FIXED_STRING;
    static constexpr const char* name = "string";
};

template<size_t N>
struct KeyTypeInfo<FixedString<N>> {
    static constexpr KeyTypeId typeId = KeyTypeId::FIXED_STRING;
    static constexpr const char* name = "fixed_string";
};

template<>
struct KeyTypeInfo<int64_t> {
    static constexpr KeyTypeId typeId = KeyTypeId::CUSTOM;
    static constexpr const char* name = "bigint";
};

template<size_t N>
struct KeyTypeInfo<RowPayload<N>> {
    static constexpr KeyTypeId typeId = KeyTypeId::CUSTOM;
    static constexpr const char* name = "row_payload";
};

constexpr uint32_t DB_META_MAGIC = 0xB7EEFACE;

#pragma pack(push, 1)
struct TreeMetadata {
    uint32_t magic;          // DB_META_MAGIC (0xB7EEFACE)
    uint32_t keyTypeId;      // KeyTypeId for K
    uint32_t keyDiskSize;    // KeyTraits<K>::diskSize
    uint32_t maxStringLen;   // length for key if string
    char     typeName[32];   // "int", "fixed_string", etc.
    uint32_t valTypeId;      // KeyTypeId for V
    uint32_t valDiskSize;    // KeyTraits<V>::diskSize
    char     valTypeName[32];// "char", "custom", etc.
    uint8_t  _reserved[424]; // pad to 512 bytes
};
#pragma pack(pop)

static_assert(sizeof(TreeMetadata) == 512, "TreeMetadata must be exactly 512 bytes");

#endif // COMMON_H
