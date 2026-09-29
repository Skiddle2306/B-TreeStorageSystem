#ifndef PAGE_H
#define PAGE_H

#include "common.h"
#include <cstdint>
#include <cstring>
#include <stdexcept>

#pragma pack(push, 1)   // Remove padding for better memory efficiency
struct PageHeader {
    page_id_t pageId;       // 4 bytes — this page's own ID
    page_id_t parentPageId; // 4 bytes — parent's page ID (INVALID_PAGE for root)
    PageType  pageType;     // 1 byte  — LEAF / INTERNAL / FREE / HEADER
    uint16_t  numKeys;      // 2 bytes — how many keys are currently stored
    uint8_t   _pad;         // 1 byte  — align body to 4-byte boundary
    // 12 bytes
};
#pragma pack(pop)

static_assert(sizeof(PageHeader) == 12, "PageHeader must be exactly 12 bytes");


// ─────────────────────────────────────────────────────────────────────────────
// LeafPage<K, V>
//
// Disk layout (PAGE_SIZE bytes total):
//   [PageHeader   : 12 bytes]
//   [prev         : 4 bytes]
//   [next         : 4 bytes]
//   [keys[]       : MAX_KEYS * KEY_SIZE]
//   [values[]     : MAX_KEYS * VAL_SIZE]
//   [unused       : rest of page]
//
// Storing keys and values in parallel contiguous arrays maximizes CPU cache line
// efficiency during key searches while guaranteeing zero serialization overhead.
// ─────────────────────────────────────────────────────────────────────────────
template<typename K, typename V = char>
class LeafPage {
public:
    static constexpr int HEADER_OFFSET   = 0;
    static constexpr int PREV_OFFSET     = sizeof(PageHeader);          // 12
    static constexpr int NEXT_OFFSET     = PREV_OFFSET + 4;             // 16
    static constexpr int KEYS_OFFSET     = NEXT_OFFSET + 4;             // 20
    static constexpr int KEY_SIZE        = KeyTraits<K>::diskSize;
    static constexpr int VAL_SIZE        = KeyTraits<V>::diskSize;
    static constexpr int ENTRY_SIZE      = KEY_SIZE + VAL_SIZE;
    static constexpr int MAX_KEYS        = (PAGE_SIZE - KEYS_OFFSET) / ENTRY_SIZE;
    static constexpr int VALUES_OFFSET   = KEYS_OFFSET + MAX_KEYS * KEY_SIZE;

    // Construct a view over an existing raw buffer (read from disk)
    explicit LeafPage(char* rawPage) : raw_(rawPage) {}

    // Initialise a brand-new blank leaf page
    void init(page_id_t id, page_id_t parent = INVALID_PAGE) {
        memset(raw_, 0, PAGE_SIZE);
        header().pageId       = id;
        header().parentPageId = parent;
        header().pageType     = PageType::LEAF;
        header().numKeys      = 0;
        header()._pad         = 0;
        setPrev(INVALID_PAGE);
        setNext(INVALID_PAGE);
    }

    // ── Header accessors ─────────────────────────────────────────────────────
    PageHeader& header()       {
        return *reinterpret_cast<PageHeader*>(raw_ + HEADER_OFFSET); 
    }
    const PageHeader& header() const {
        return *reinterpret_cast<const PageHeader*>(raw_ + HEADER_OFFSET); 
    }

    page_id_t  pageId()   const { return header().pageId;       }
    page_id_t  parent()   const { return header().parentPageId; }
    uint16_t   numKeys()  const { return header().numKeys;      }
    int        capacity() const { return MAX_KEYS;              }
    bool       isFull()   const { return numKeys() >= MAX_KEYS; }

    void setParent(page_id_t p)  { header().parentPageId = p; }
    void setNumKeys(uint16_t n)  { header().numKeys = n;      }

    // ── Linked list ──────────────────────────────────────────────────────────
    page_id_t prev() const {
        page_id_t v; memcpy(&v, raw_ + PREV_OFFSET, 4); return v;
    }
    page_id_t next() const {
        page_id_t v; memcpy(&v, raw_ + NEXT_OFFSET, 4); return v;
    }
    void setPrev(page_id_t id) { memcpy(raw_ + PREV_OFFSET, &id, 4); }
    void setNext(page_id_t id) { memcpy(raw_ + NEXT_OFFSET, &id, 4); }

    // ── Key access ───────────────────────────────────────────────────────────
    K getKey(int idx) const {
        if (idx < 0 || idx >= numKeys())
            throw std::out_of_range("LeafPage::getKey out of range");
        return KeyTraits<K>::read(raw_ + KEYS_OFFSET + idx * KEY_SIZE);
    }
    void setKey(int idx, const K& val) {
        if (idx < 0 || idx >= capacity())
            throw std::out_of_range("LeafPage::setKey out of range");
        KeyTraits<K>::write(raw_ + KEYS_OFFSET + idx * KEY_SIZE, val);
    }

    // ── Value access ─────────────────────────────────────────────────────────
    V getValue(int idx) const {
        if (idx < 0 || idx >= numKeys())
            throw std::out_of_range("LeafPage::getValue out of range");
        return KeyTraits<V>::read(raw_ + VALUES_OFFSET + idx * VAL_SIZE);
    }
    void setValue(int idx, const V& val) {
        if (idx < 0 || idx >= capacity())
            throw std::out_of_range("LeafPage::setValue out of range");
        KeyTraits<V>::write(raw_ + VALUES_OFFSET + idx * VAL_SIZE, val);
    }

    // Insert key and value at position idx, shifting right — caller must check !isFull()
    void insertKey(int idx, const K& key, const V& val = V{}) {
        int n = numKeys();
        memmove(raw_ + KEYS_OFFSET + (idx + 1) * KEY_SIZE,
                raw_ + KEYS_OFFSET + idx       * KEY_SIZE,
                (n - idx) * KEY_SIZE);
        memmove(raw_ + VALUES_OFFSET + (idx + 1) * VAL_SIZE,
                raw_ + VALUES_OFFSET + idx       * VAL_SIZE,
                (n - idx) * VAL_SIZE);
        setKey(idx, key);
        setValue(idx, val);
        setNumKeys(n + 1);
    }

    // Remove key and value at position idx, shifting left
    void removeKey(int idx) {
        int n = numKeys();
        memmove(raw_ + KEYS_OFFSET + idx       * KEY_SIZE,
                raw_ + KEYS_OFFSET + (idx + 1) * KEY_SIZE,
                (n - idx - 1) * KEY_SIZE);
        memmove(raw_ + VALUES_OFFSET + idx       * VAL_SIZE,
                raw_ + VALUES_OFFSET + (idx + 1) * VAL_SIZE,
                (n - idx - 1) * VAL_SIZE);
        setNumKeys(n - 1);
    }

    char* raw() { return raw_; }

private:
    char* raw_;
};


// ─────────────────────────────────────────────────────────────────────────────
// InternalPage<K> (also aliased as IntervalPage<K>)
//
// Internal routing pages store ONLY keys and child page pointers.
// They never store values, keeping internal nodes compact and maximizing fan-out.
// ─────────────────────────────────────────────────────────────────────────────
template<typename K>
class InternalPage {
public:
    static constexpr int HEADER_OFFSET    = 0;
    static constexpr int KEYS_OFFSET      = sizeof(PageHeader);   // 12
    static constexpr int KEY_SIZE         = KeyTraits<K>::diskSize;
    // Reserve space for MAX_KEYS keys, then children follow
    static constexpr int MAX_KEYS         = (PAGE_SIZE - KEYS_OFFSET - 4)
                                            / (KEY_SIZE + sizeof(page_id_t));
    static constexpr int CHILDREN_OFFSET  = KEYS_OFFSET + MAX_KEYS * KEY_SIZE;

    explicit InternalPage(char* rawPage) : raw_(rawPage) {}

    void init(page_id_t id, page_id_t parent = INVALID_PAGE) {
        memset(raw_, 0, PAGE_SIZE);
        header().pageId       = id;
        header().parentPageId = parent;
        header().pageType     = PageType::INTERNAL;
        header().numKeys      = 0;
        header()._pad         = 0;
    }

    PageHeader& header()       { return *reinterpret_cast<PageHeader*>(raw_); }
    const PageHeader& header() const { return *reinterpret_cast<const PageHeader*>(raw_); }

    page_id_t pageId()   const { return header().pageId;        }
    page_id_t parent()   const { return header().parentPageId;  }
    uint16_t  numKeys()  const { return header().numKeys;       }
    int       capacity() const { return MAX_KEYS;               }
    bool      isFull()   const { return numKeys() >= MAX_KEYS;  }

    void setParent(page_id_t p)  { header().parentPageId = p; }
    void setNumKeys(uint16_t n)  { header().numKeys = n;      }

    K getKey(int idx) const {
        if (idx < 0 || idx >= numKeys())
            throw std::out_of_range("InternalPage::getKey out of range");
        return KeyTraits<K>::read(raw_ + KEYS_OFFSET + idx * KEY_SIZE);
    }
    void setKey(int idx, const K& val) {
        if (idx < 0 || idx >= capacity())
            throw std::out_of_range("InternalPage::setKey out of range");
        KeyTraits<K>::write(raw_ + KEYS_OFFSET + idx * KEY_SIZE, val);
    }

    void insertKey(int idx, const K& val) {
        int n = numKeys();
        memmove(raw_ + KEYS_OFFSET + (idx + 1) * KEY_SIZE,
                raw_ + KEYS_OFFSET + idx       * KEY_SIZE,
                (n - idx) * KEY_SIZE);
        setKey(idx, val);
    }

    void removeKey(int idx) {
        int n = numKeys();
        memmove(raw_ + KEYS_OFFSET + idx       * KEY_SIZE,
                raw_ + KEYS_OFFSET + (idx + 1) * KEY_SIZE,
                (n - idx - 1) * KEY_SIZE);
    }

    page_id_t getChild(int idx) const {
        if (idx < 0 || idx > numKeys())
            throw std::out_of_range("InternalPage::getChild out of range");
        page_id_t id;
        memcpy(&id, raw_ + CHILDREN_OFFSET + idx * sizeof(page_id_t), sizeof(page_id_t));
        return id;
    }
    void setChild(int idx, page_id_t id) {
        if (idx < 0 || idx > capacity())
            throw std::out_of_range("InternalPage::setChild out of range");
        memcpy(raw_ + CHILDREN_OFFSET + idx * sizeof(page_id_t), &id, sizeof(page_id_t));
    }

    void insertChild(int idx, page_id_t id) {
        int n = numKeys(); // n+1 children exist before this insert
        memmove(raw_ + CHILDREN_OFFSET + (idx + 1) * sizeof(page_id_t),
                raw_ + CHILDREN_OFFSET + idx       * sizeof(page_id_t),
                (n + 1 - idx) * sizeof(page_id_t));
        setChild(idx, id);
    }

    void removeChild(int idx) {
        int n = numKeys();
        memmove(raw_ + CHILDREN_OFFSET + idx       * sizeof(page_id_t),
                raw_ + CHILDREN_OFFSET + (idx + 1) * sizeof(page_id_t),
                (n - idx) * sizeof(page_id_t));
    }

    void insertKeyAndRightChild(int keyIdx, const K& key, page_id_t rightChild) {
        insertKey(keyIdx, key);
        insertChild(keyIdx + 1, rightChild);
        setNumKeys(numKeys() + 1);
    }

    void removeKeyAndRightChild(int keyIdx) {
        removeKey(keyIdx);
        removeChild(keyIdx + 1);
        setNumKeys(numKeys() - 1);
    }

    void removeKeyAndLeftChild(int keyIdx) {
        removeKey(keyIdx);
        removeChild(keyIdx);
        setNumKeys(numKeys() - 1);
    }

    char* raw() { return raw_; }

private:
    char* raw_;
};

// User-specified alias for interval_page
template<typename K>
using IntervalPage = InternalPage<K>;


// ─────────────────────────────────────────────────────────────────────────────
// HeaderPage — page 0 of the file, always.
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)
struct HeaderPageData {
    uint32_t  magic;          // 0xB7EEB7EE — sanity check on open
    page_id_t rootPageId;     // current root of the B+ tree
    page_id_t freeListHead;   // first free page (INVALID_PAGE if none)
    uint32_t  numPages;       // total pages allocated (including header page)
    uint8_t   _reserved[PAGE_SIZE - 16]; // pad to full page size
};
#pragma pack(pop)

static_assert(sizeof(HeaderPageData) == PAGE_SIZE, "HeaderPageData must be exactly PAGE_SIZE bytes");

constexpr uint32_t DB_MAGIC = 0xB7EEB7EE;

class HeaderPage {
public:
    explicit HeaderPage(char* rawPage) : raw_(rawPage) {}

    void init() {
        memset(raw_, 0, PAGE_SIZE);
        data().magic       = DB_MAGIC;
        data().rootPageId  = INVALID_PAGE;
        data().freeListHead= INVALID_PAGE;
        data().numPages    = 1; // header page itself
    }

    bool valid() const { return data().magic == DB_MAGIC; }

    HeaderPageData&       data()       { return *reinterpret_cast<HeaderPageData*>(raw_); }
    const HeaderPageData& data() const { return *reinterpret_cast<const HeaderPageData*>(raw_); }

    char* raw() { return raw_; }

private:
    char* raw_;
};

#endif // PAGE_H
