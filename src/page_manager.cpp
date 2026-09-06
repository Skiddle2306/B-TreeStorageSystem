#include "page_manager.h"
#include <stdexcept>
#include <iostream>
#include <cstring>
using namespace std;

// ─────────────────────────────────────────────────────────────────────────────
// DiskManager
// ─────────────────────────────────────────────────────────────────────────────

DiskManager::DiskManager(const string& filename)
    : header_(headerBuf_)
{
    memset(headerBuf_, 0, PAGE_SIZE);

    file_.open(filename, ios::in | ios::out | ios::binary);

    if (!file_.is_open()) {
        file_.open(filename, ios::in | ios::out | ios::binary | ios::trunc);
        if (!file_.is_open())
            throw runtime_error("DiskManager: cannot open or create file: " + filename);

        header_.init();
        file_.seekp(0);
        file_.write(headerBuf_, PAGE_SIZE);
        file_.flush();
    } else {
        file_.seekg(0);
        file_.read(headerBuf_, PAGE_SIZE);
        if (!header_.valid())
            throw runtime_error("DiskManager: corrupt or invalid database file");
    }
}

DiskManager::~DiskManager() {
    flushHeader();
    file_.close();
}

void DiskManager::readPage(page_id_t id, char* dst) {
    if (id < 0 || (uint32_t)id >= header_.data().numPages)
        throw out_of_range("DiskManager::readPage: invalid page id " + to_string(id));
    lock_guard<mutex> lock(fileMutex_);
    file_.seekg(pageOffset(id));
    file_.read(dst, PAGE_SIZE);
    if (file_.fail())
        throw runtime_error("DiskManager::readPage: read failed for page " + to_string(id));
}

void DiskManager::writePage(page_id_t id, const char* src) {
    if (id < 0 || (uint32_t)id >= header_.data().numPages)
        throw out_of_range("DiskManager::writePage: invalid page id " + to_string(id));
    lock_guard<mutex> lock(fileMutex_);
    file_.seekp(pageOffset(id));
    file_.write(src, PAGE_SIZE);
    file_.flush();
    if (file_.fail())
        throw runtime_error("DiskManager::writePage: write failed for page " + to_string(id));
}

page_id_t DiskManager::allocatePage() {
    lock_guard<mutex> lock(fileMutex_);
    auto& hd = header_.data();
    page_id_t newId;

    if (hd.freeListHead != INVALID_PAGE) {
        newId = hd.freeListHead;
        char buf[PAGE_SIZE];
        file_.seekg(pageOffset(newId));
        file_.read(buf, PAGE_SIZE);
        page_id_t nextFree;
        memcpy(&nextFree, buf, sizeof(page_id_t));
        hd.freeListHead = nextFree;
    } else {
        newId = static_cast<page_id_t>(hd.numPages);
        hd.numPages++;
        char blank[PAGE_SIZE] = {};
        file_.seekp(pageOffset(newId));
        file_.write(blank, PAGE_SIZE);
        file_.flush();
    }
    return newId;
}

void DiskManager::deallocatePage(page_id_t id) {
    lock_guard<mutex> lock(fileMutex_);
    auto& hd = header_.data();

    char buf[PAGE_SIZE] = {};
    memcpy(buf, &hd.freeListHead, sizeof(page_id_t));
    PageType freeType = PageType::FREE;
    memcpy(buf + sizeof(page_id_t), &freeType, 1);

    file_.seekp(pageOffset(id));
    file_.write(buf, PAGE_SIZE);
    file_.flush();

    hd.freeListHead = id;
}

void DiskManager::flushHeader() {
    lock_guard<mutex> lock(fileMutex_);
    file_.seekp(0);
    file_.write(headerBuf_, PAGE_SIZE);
    file_.flush();
}

page_id_t DiskManager::rootPageId() const {
    return header_.data().rootPageId;
}

void DiskManager::setRootPageId(page_id_t id) {
    header_.data().rootPageId = id;
}

uint32_t DiskManager::numPages() const {
    return header_.data().numPages;
}

// ─────────────────────────────────────────────────────────────────────────────
// PageManager
// ─────────────────────────────────────────────────────────────────────────────

PageManager::PageManager(const string& filename, int numFrames)
    : disk_(filename), numFrames_(numFrames)
{
    frames_ = new Frame[numFrames_];
    for (int i = 0; i < numFrames_; i++) {
        lruList_.push_front(i);
        lruPos_[i] = lruList_.begin();
    }
}

PageManager::~PageManager() {
    flushAll();
    delete[] frames_;
}

void PageManager::touchLRU(int frameIdx) {
    if (lruPos_.count(frameIdx))
        lruList_.erase(lruPos_[frameIdx]);
    lruList_.push_front(frameIdx);
    lruPos_[frameIdx] = lruList_.begin();
}

void PageManager::removeLRU(int frameIdx) {
    if (lruPos_.count(frameIdx)) {
        lruList_.erase(lruPos_[frameIdx]);
        lruPos_.erase(frameIdx);
    }
}

int PageManager::evict() {
    for (auto it = lruList_.rbegin(); it != lruList_.rend(); ++it) {
        int fi    = *it;
        Frame& f  = frames_[fi];
        if (f.pinCount > 0) continue;

        if (f.isDirty && f.pageId != INVALID_PAGE) {
            disk_.writePage(f.pageId, f.data);
            f.isDirty = false;
        }

        if (f.pageId != INVALID_PAGE)
            pageTable_.erase(f.pageId);

        lruList_.erase(std::next(it).base());
        lruPos_.erase(fi);
        return fi;
    }
    throw runtime_error("PageManager::evict: all frames are pinned — increase pool size");
}

void PageManager::loadPage(page_id_t id, int frameIdx) {
    disk_.readPage(id, frames_[frameIdx].data);
    frames_[frameIdx].pageId   = id;
    frames_[frameIdx].isDirty  = false;
    frames_[frameIdx].pinCount = 0;
    pageTable_[id]             = frameIdx;
}

char* PageManager::pin(page_id_t id, LatchMode mode) {
    int frameIdx = -1;
    {
        lock_guard<mutex> lock(poolMutex_);
        if (pageTable_.count(id)) {
            frameIdx = pageTable_[id];
            frames_[frameIdx].pinCount++;
            removeLRU(frameIdx);
        } else {
            frameIdx = evict();
            loadPage(id, frameIdx);
            frames_[frameIdx].pinCount = 1;
        }
    }

    Frame& f = frames_[frameIdx];
    if (mode == LatchMode::SHARED)
        f.latch.lock_shared();
    else
        f.latch.lock();

    return f.data;
}

void PageManager::unpin(page_id_t id, bool dirty, LatchMode mode) {
    lock_guard<mutex> lock(poolMutex_);
    if (!pageTable_.count(id))
        throw runtime_error("PageManager::unpin: page not in pool: " + to_string(id));

    int frameIdx = pageTable_[id];
    Frame& f     = frames_[frameIdx];

    if (f.pinCount <= 0)
        throw runtime_error("PageManager::unpin: pin count already 0 for page: " + to_string(id));

    if (dirty) f.isDirty = true;
    f.pinCount--;

    if (mode == LatchMode::SHARED)
        f.latch.unlock_shared();
    else
        f.latch.unlock();

    if (f.pinCount == 0) {
        lruList_.push_front(frameIdx);
        lruPos_[frameIdx] = lruList_.begin();
    }
}

char* PageManager::newPage(page_id_t& outId, LatchMode mode) {
    page_id_t id = disk_.allocatePage();
    outId        = id;

    int frameIdx = -1;
    {
        lock_guard<mutex> lock(poolMutex_);
        frameIdx = evict();
        memset(frames_[frameIdx].data, 0, PAGE_SIZE);
        frames_[frameIdx].pageId   = id;
        frames_[frameIdx].isDirty  = true;
        frames_[frameIdx].pinCount = 1;
        pageTable_[id]             = frameIdx;
    }

    Frame& f = frames_[frameIdx];
    if (mode == LatchMode::SHARED)
        f.latch.lock_shared();
    else
        f.latch.lock();

    return f.data;
}

void PageManager::deletePage(page_id_t id) {
    {
        lock_guard<mutex> lock(poolMutex_);
        if (pageTable_.count(id)) {
            int fi          = pageTable_[id];
            Frame& f        = frames_[fi];
            f.isDirty       = false;
            f.pageId        = INVALID_PAGE;
            f.pinCount      = 0;
            pageTable_.erase(id);
            lruList_.push_front(fi);
            lruPos_[fi] = lruList_.begin();
        }
    }
    disk_.deallocatePage(id);
}

void PageManager::flushAll() {
    lock_guard<mutex> lock(poolMutex_);
    for (int i = 0; i < numFrames_; i++) {
        Frame& f = frames_[i];
        if (f.isDirty && f.pageId != INVALID_PAGE) {
            disk_.writePage(f.pageId, f.data);
            f.isDirty = false;
        }
    }
    disk_.flushHeader();
}

void PageManager::flushPage(page_id_t id) {
    lock_guard<mutex> lock(poolMutex_);
    if (!pageTable_.count(id)) return;
    Frame& f = frames_[pageTable_[id]];
    if (f.isDirty) {
        disk_.writePage(f.pageId, f.data);
        f.isDirty = false;
    }
}

void PageManager::setRootPageId(page_id_t id) {
    disk_.setRootPageId(id);
    disk_.flushHeader();
}