#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "common.h"
#include "page.h"
#include <string>
#include <fstream>
#include <unordered_map>
#include <list>
#include <shared_mutex>
#include <mutex>
#include <stdexcept>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Frame — one slot in the buffer pool.
// Holds a raw PAGE_SIZE buffer that maps 1:1 to a page on disk.
// The tree works directly on frame.data — no copy, no serialisation.
// ─────────────────────────────────────────────────────────────────────────────
struct Frame {
    char        data[PAGE_SIZE]; // raw page bytes — IS the page
    page_id_t   pageId;          // which page is loaded here
    int         pinCount;        // how many callers are actively using this frame
    bool        isDirty;         // true = modified, must write back before eviction

    // Per-frame latch for latch crabbing (see PageManager::pin/unpin)
    // shared = multiple readers, exclusive = one writer
    std::shared_mutex latch;

    Frame() : pageId(INVALID_PAGE), pinCount(0), isDirty(false) {
        memset(data, 0, PAGE_SIZE);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// DiskManager — owns the database file.
// Responsible ONLY for raw page reads/writes and page allocation.
// Never called directly by the tree — always goes through PageManager.
// ─────────────────────────────────────────────────────────────────────────────
class DiskManager {
public:
    // Open existing file or create a new one
    explicit DiskManager(const std::string& filename);
    ~DiskManager();

    // Read page `id` from disk into `dst` (must be PAGE_SIZE bytes)
    void readPage(page_id_t id, char* dst);

    // Write `src` (PAGE_SIZE bytes) to page `id` on disk
    void writePage(page_id_t id, const char* src);

    // Allocate a new page: either reclaim from free list or extend the file
    page_id_t allocatePage();

    // Return page to free list (next allocation can reuse it)
    void deallocatePage(page_id_t id);

    // Flush the header page (call after root changes, allocations etc.)
    void flushHeader();

    page_id_t rootPageId()   const;
    void      setRootPageId(page_id_t id);
    uint32_t  numPages()     const;

private:
    std::fstream  file_;
    char          headerBuf_[PAGE_SIZE]; // always-loaded header page
    HeaderPage    header_;               // view over headerBuf_

    std::mutex    fileMutex_;            // serialise all file I/O

    // Offset in file for a given page id
    static std::streamoff pageOffset(page_id_t id) {
        return static_cast<std::streamoff>(id) * PAGE_SIZE;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PageManager (Buffer Pool)
//
// Fixed pool of Frame slots. The tree always goes through PageManager:
//   char* frame = pm.pin(pageId, LatchMode::EXCLUSIVE);
//   // ... work on frame directly as LeafPage<K> or InternalPage<K> view ...
//   pm.unpin(pageId, true /*dirty*/);
//
// Eviction policy: LRU via an ordered list of unpinned frame indices.
// Pinned frames are never evicted.
// ─────────────────────────────────────────────────────────────────────────────
enum class LatchMode { SHARED, EXCLUSIVE };

class PageManager {
public:
    // numFrames: how many pages to keep in memory at once
    explicit PageManager(const std::string& filename, int numFrames = 64);
    ~PageManager();

    // Pin a page into a frame and acquire a latch.
    // Returns pointer to the frame's raw data (PAGE_SIZE bytes).
    // Caller MUST call unpin() when done — every pin must have an unpin.
    char* pin(page_id_t id, LatchMode mode = LatchMode::EXCLUSIVE);

    // Release the latch and decrement pin count.
    // Pass dirty=true if you modified the page — marks it for write-back.
    void unpin(page_id_t id, bool dirty, LatchMode mode = LatchMode::EXCLUSIVE);

    // Allocate a brand-new page, pin it, zero it, and return its frame data.
    // Caller must unpin() when done.
    char* newPage(page_id_t& outId, LatchMode mode = LatchMode::EXCLUSIVE);

    // Unpin and mark a page as deleted — it returns to the free list on flush.
    void deletePage(page_id_t id);

    // Write all dirty frames to disk
    void flushAll();

    // Write one specific page to disk immediately (useful for WAL)
    void flushPage(page_id_t id);

    page_id_t rootPageId()        const { return disk_.rootPageId(); }
    void      setRootPageId(page_id_t id);
    int       numFrames()         const { return numFrames_; }

private:
    DiskManager disk_;
    int         numFrames_;

    // Frame pool — fixed size, allocated once
    Frame*      frames_;

    // Page table: page_id → frame index
    std::unordered_map<page_id_t, int> pageTable_;

    // LRU list of UNPINNED frame indices (front = most recent, back = evict first)
    std::list<int> lruList_;
    std::unordered_map<int, std::list<int>::iterator> lruPos_; // frameIdx → iterator

    // Protects pageTable_, lruList_, lruPos_, and frame metadata
    // (NOT frame.data — that's protected by frame.latch)
    std::mutex poolMutex_;

    // Find a free frame: return frame index of an unpinned frame, evicting if needed
    // Must be called with poolMutex_ held
    int evict();

    // Load page `id` from disk into frame at `frameIdx`
    // Must be called with poolMutex_ held
    void loadPage(page_id_t id, int frameIdx);

    // Move frame to front of LRU list (most recently used)
    // Must be called with poolMutex_ held
    void touchLRU(int frameIdx);

    // Remove frame from LRU list
    // Must be called with poolMutex_ held
    void removeLRU(int frameIdx);
};

#endif // PAGE_MANAGER_H
