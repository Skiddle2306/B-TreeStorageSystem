#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <cstring>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Core constants
// ─────────────────────────────────────────────────────────────────────────────
constexpr int     PAGE_SIZE           = 4096;
constexpr int32_t INVALID_PAGE        = -1;

#define DEFAULT_STRING_LENGTH 256   // max on-disk bytes for a string key

using page_id_t = int32_t;

enum class PageType : uint8_t {
    LEAF     = 0,
    INTERNAL = 1,
    FREE     = 2,
    HEADER   = 3,
};

// ─────────────────────────────────────────────────────────────────────────────
// KeyTraits<K>
//
// Decouples "how big is K on disk" from "what K looks like in C++".
// The tree and page classes use KeyTraits instead of sizeof(K) directly,
// so std::string works even though it has no fixed sizeof.
//
// diskSize  — bytes this key occupies in a page buffer
// write     — serialise a K into a raw byte region
// read      — deserialise a K from a raw byte region
//
// Add a specialisation here for any new type that needs custom handling.
// ─────────────────────────────────────────────────────────────────────────────
template<typename K>
struct KeyTraits {
    // Default: trivially copyable types (int, float, double, char, …)
    static constexpr int diskSize = sizeof(K);

    static void write(char* dst, const K& val) {
        memcpy(dst, &val, sizeof(K));
    }
    static K read(const char* src) {
        K val;
        memcpy(&val, src, sizeof(K));
        return val;
    }
};

// Specialisation for std::string — fixed DEFAULT_STRING_LENGTH bytes on disk
template<>
struct KeyTraits<std::string> {
    static constexpr int diskSize = DEFAULT_STRING_LENGTH;

    static void write(char* dst, const std::string& val) {
        memset(dst, 0, DEFAULT_STRING_LENGTH);
        strncpy(dst, val.c_str(), DEFAULT_STRING_LENGTH - 1);
        // always null-terminated because we zero first
    }
    static std::string read(const char* src) {
        // safe: buf is null-terminated within DEFAULT_STRING_LENGTH bytes
        return std::string(src, strnlen(src, DEFAULT_STRING_LENGTH));
    }
};

#endif // COMMON_H