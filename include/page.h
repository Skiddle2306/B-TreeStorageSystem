#ifndef PAGE_H
#define PAGE_H

#include "common.h"
#include <cstdint>
#include <cstring>
#include <stdexcept>

#pragma pack(push, 1)   //Remove padding for better memory efficiency
struct PageHeader {
    page_id_t pageId;       // 4 bytes — this page's own ID
    page_id_t parentPageId; // 4 bytes — parent's page ID (INVALID_PAGE for root)
    PageType  pageType;     // 1 byte  — LEAF / INTERNAL / FREE / HEADER
    uint16_t  numKeys;      // 2 bytes — how many keys are currently stored
    uint8_t   _pad;         // 1 byte  — align body to 4-byte boundary
    //12 bytes
};
#pragma pack(pop)

static_assert(sizeof(PageHeader) == 12, "PageHeader must be exactly 12 bytes");


template<typename K>
class LeafPage {
public:
    static constexpr int HEADER_OFFSET   = 0;
    static constexpr int PREV_OFFSET     = sizeof(PageHeader);          // 12
    static constexpr int NEXT_OFFSET     = PREV_OFFSET + 4;             // 16
    static constexpr int KEYS_OFFSET     = NEXT_OFFSET + 4;             // 20
    static constexpr int MAX_KEYS        = (PAGE_SIZE - KEYS_OFFSET) / sizeof(K);

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

    page_id_t  pageId()   const {
        return header().pageId;
    }
    page_id_t  parent()   const { return header().parentPageId; }
    uint16_t   numKeys()  const { return header().numKeys;  }
    int        capacity() const { return MAX_KEYS;          }
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
    // Keys are stored as a packed array starting at KEYS_OFFSET.
    // Use getKey/setKey — never pointer-arithmetic outside this class.
    K getKey(int idx) const {
        if (idx < 0 || idx >= numKeys())
            throw std::out_of_range("LeafPage::getKey out of range");
        K val;
        memcpy(&val, raw_ + KEYS_OFFSET + idx * sizeof(K), sizeof(K));
        return val;
    }
    void setKey(int idx, const K& val) {
        if (idx < 0 || idx >= capacity())
            throw std::out_of_range("LeafPage::setKey out of range");
        memcpy(raw_ + KEYS_OFFSET + idx * sizeof(K), &val, sizeof(K));
    }

    // Insert key at position idx, shifting right — caller must check !isFull()
    void insertKey(int idx, const K& val) {
        int n = numKeys();
        // Shift keys right to make room
        memmove(raw_ + KEYS_OFFSET + (idx + 1) * sizeof(K),
                raw_ + KEYS_OFFSET + idx       * sizeof(K),
                (n - idx) * sizeof(K));
        setKey(idx, val);
        setNumKeys(n + 1);
    }

    // Remove key at position idx, shifting left
    void removeKey(int idx) {
        int n = numKeys();
        memmove(raw_ + KEYS_OFFSET + idx       * sizeof(K),
                raw_ + KEYS_OFFSET + (idx + 1) * sizeof(K),
                (n - idx - 1) * sizeof(K));
        setNumKeys(n - 1);
    }

    // Raw page pointer — buffer pool needs this to pin/unpin
    char* raw() { return raw_; }

private:
    char* raw_;  // points into a buffer pool frame — never owns this memory
};


// ─────────────────────────────────────────────────────────────────────────────
// InternalPage<K>
//
// Disk layout (PAGE_SIZE bytes total):
//   [PageHeader   : 12 bytes]
//   [keys[]       : numKeys * sizeof(K)]
//   [children[]   : (numKeys + 1) * sizeof(page_id_t)]
//   [unused       : rest of page]
//
// Keys and children are packed from KEYS_OFFSET upward.
// Children start right after the key array — their offset depends on numKeys,
// but we always allocate MAX_KEYS slots for keys and MAX_KEYS+1 for children
// so the children region starts at a fixed offset too.
// ─────────────────────────────────────────────────────────────────────────────
template<typename K>
class InternalPage {
public:
    static constexpr int HEADER_OFFSET    = 0;
    static constexpr int KEYS_OFFSET      = sizeof(PageHeader);   // 12
    // Reserve space for MAX_KEYS keys, then children follow
    // Solve: MAX_KEYS * sizeof(K) + (MAX_KEYS + 1) * 4 <= PAGE_SIZE - 12
    static constexpr int MAX_KEYS         = (PAGE_SIZE - KEYS_OFFSET - 4)
                                            / (sizeof(K) + sizeof(page_id_t));
    static constexpr int CHILDREN_OFFSET  = KEYS_OFFSET + MAX_KEYS * sizeof(K);

    explicit InternalPage(char* rawPage) : raw_(rawPage) {}

    void init(page_id_t id, page_id_t parent = INVALID_PAGE) {
        memset(raw_, 0, PAGE_SIZE);
        header().pageId       = id;
        header().parentPageId = parent;
        header().pageType     = PageType::INTERNAL;
        header().numKeys      = 0;
        header()._pad         = 0;
    }

    // ── Header accessors ─────────────────────────────────────────────────────
    PageHeader& header()       { return *reinterpret_cast<PageHeader*>(raw_); }
    const PageHeader& header() const { return *reinterpret_cast<const PageHeader*>(raw_); }

    page_id_t pageId()   const { return header().pageId;        }
    page_id_t parent()   const { return header().parentPageId;  }
    uint16_t  numKeys()  const { return header().numKeys;       }
    int       capacity() const { return MAX_KEYS;               }
    bool      isFull()   const { return numKeys() >= MAX_KEYS;  }

    void setParent(page_id_t p)  { header().parentPageId = p; }
    void setNumKeys(uint16_t n)  { header().numKeys = n;      }

    // ── Key access ───────────────────────────────────────────────────────────
    K getKey(int idx) const {
        if (idx < 0 || idx >= numKeys())
            throw std::out_of_range("InternalPage::getKey out of range");
        K val;
        memcpy(&val, raw_ + KEYS_OFFSET + idx * sizeof(K), sizeof(K));
        return val;
    }
    void setKey(int idx, const K& val) {
        if (idx < 0 || idx >= capacity())
            throw std::out_of_range("InternalPage::setKey out of range");
        memcpy(raw_ + KEYS_OFFSET + idx * sizeof(K), &val, sizeof(K));
    }

    // Insert key at idx, shifting right
    void insertKey(int idx, const K& val) {
        int n = numKeys();
        memmove(raw_ + KEYS_OFFSET + (idx + 1) * sizeof(K),
                raw_ + KEYS_OFFSET + idx       * sizeof(K),
                (n - idx) * sizeof(K));
        setKey(idx, val);
        // numKeys updated by caller after inserting child too
    }

    void removeKey(int idx) {
        int n = numKeys();
        memmove(raw_ + KEYS_OFFSET + idx       * sizeof(K),
                raw_ + KEYS_OFFSET + (idx + 1) * sizeof(K),
                (n - idx - 1) * sizeof(K));
    }

    // ── Child access — n keys means n+1 children ─────────────────────────────
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

    // Insert child at idx, shifting right
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

    // Convenience: insert key+right-child together (the common split case)
    // Inserts key at keyIdx and rightChild at keyIdx+1
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


// ─────────────────────────────────────────────────────────────────────────────
// HeaderPage — page 0 of the file, always.
// Stores file-level metadata. Not a tree node.
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