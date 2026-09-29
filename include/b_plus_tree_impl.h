#ifndef B_PLUS_TREE_IMPL_H
#define B_PLUS_TREE_IMPL_H

#include "b_plus_tree.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <queue>
#include <vector>
#include <iostream>
#include <cstring>

inline std::string resolveTreeFolder(const std::string& name) {
    if (name.empty()) {
        return "data/default";
    }
    if (name.rfind("data/", 0) == 0 || name.rfind("./data/", 0) == 0) {
        return name;
    }
    return "data/" + name;
}

template<typename K, typename V>
BPlusTree<K, V>::BPlusTree(const std::string& treeName, int numFrames) {
    initTree(treeName, numFrames);
}

template<typename K, typename V>
BPlusTree<K, V>::~BPlusTree() {
    flush();
}

template<typename K, typename V>
void BPlusTree<K, V>::initTree(const std::string& treeName, int numFrames) {
    folderPath_ = resolveTreeFolder(treeName);
    std::filesystem::create_directories(folderPath_);

    std::string dbFile = folderPath_ + "/tree.db";
    std::string metaFile = folderPath_ + "/meta.dat";

    if (std::filesystem::exists(metaFile)) {
        std::ifstream ifs(metaFile, std::ios::binary);
        if (!ifs.is_open()) {
            throw std::runtime_error("BPlusTree: unable to open existing metadata file: " + metaFile);
        }
        TreeMetadata meta{};
        ifs.read(reinterpret_cast<char*>(&meta), sizeof(meta));
        if (ifs.gcount() != sizeof(meta) || meta.magic != DB_META_MAGIC) {
            throw std::runtime_error("BPlusTree: corrupted metadata file in: " + folderPath_);
        }
        if (meta.keyTypeId != static_cast<uint32_t>(KeyTypeInfo<K>::typeId)) {
            throw std::runtime_error("BPlusTree: key type mismatch in folder '" + folderPath_ +
                                     "'. Expected type: " + KeyTypeInfo<K>::name +
                                     ", found: " + meta.typeName);
        }
        if (meta.keyDiskSize != static_cast<uint32_t>(KeyTraits<K>::diskSize)) {
            throw std::runtime_error("BPlusTree: key size mismatch in folder '" + folderPath_ +
                                     "'. Expected size: " + std::to_string(KeyTraits<K>::diskSize) +
                                     ", found: " + std::to_string(meta.keyDiskSize));
        }
        if (meta.valDiskSize != static_cast<uint32_t>(KeyTraits<V>::diskSize)) {
            throw std::runtime_error("BPlusTree: value size mismatch in folder '" + folderPath_ +
                                     "'. Expected size: " + std::to_string(KeyTraits<V>::diskSize) +
                                     ", found: " + std::to_string(meta.valDiskSize));
        }
    } else {
        TreeMetadata meta{};
        meta.magic = DB_META_MAGIC;
        meta.keyTypeId = static_cast<uint32_t>(KeyTypeInfo<K>::typeId);
        meta.keyDiskSize = static_cast<uint32_t>(KeyTraits<K>::diskSize);
        meta.maxStringLen = (KeyTypeInfo<K>::typeId == KeyTypeId::FIXED_STRING) ? KeyTraits<K>::diskSize : 0;
        std::memset(meta.typeName, 0, sizeof(meta.typeName));
        std::strncpy(meta.typeName, KeyTypeInfo<K>::name, sizeof(meta.typeName) - 1);

        meta.valTypeId = static_cast<uint32_t>(KeyTypeInfo<V>::typeId);
        meta.valDiskSize = static_cast<uint32_t>(KeyTraits<V>::diskSize);
        std::memset(meta.valTypeName, 0, sizeof(meta.valTypeName));
        std::strncpy(meta.valTypeName, KeyTypeInfo<V>::name, sizeof(meta.valTypeName) - 1);

        std::ofstream ofs(metaFile, std::ios::binary);
        if (!ofs.is_open()) {
            throw std::runtime_error("BPlusTree: cannot create metadata file: " + metaFile);
        }
        ofs.write(reinterpret_cast<const char*>(&meta), sizeof(meta));
        ofs.flush();
    }

    pm_ = std::make_unique<PageManager>(dbFile, numFrames);
}

template<typename K, typename V>
page_id_t BPlusTree<K, V>::findLeaf(const K& key, std::vector<page_id_t>& ancestors) {
    page_id_t currId = pm_->rootPageId();
    if (currId == INVALID_PAGE) return INVALID_PAGE;

    while (true) {
        char* raw = pm_->pin(currId, LatchMode::SHARED);
        PageHeader* hdr = reinterpret_cast<PageHeader*>(raw);
        if (hdr->pageType == PageType::LEAF) {
            pm_->unpin(currId, false, LatchMode::SHARED);
            return currId;
        }
        InternalPage<K> internal(raw);
        int n = internal.numKeys();
        int childIdx = 0;
        for (int i = 0; i < n; i++) {
            if (internal.getKey(i) <= key) {
                childIdx = i + 1;
            } else {
                break;
            }
        }
        page_id_t nextId = internal.getChild(childIdx);
        pm_->unpin(currId, false, LatchMode::SHARED);
        ancestors.push_back(currId);
        currId = nextId;
    }
}

template<typename K, typename V>
void BPlusTree<K, V>::insert(const K& key, const V& value) {
    page_id_t rootId = pm_->rootPageId();

    // Case 1: Empty tree
    if (rootId == INVALID_PAGE) {
        page_id_t newRootId;
        char* raw = pm_->newPage(newRootId, LatchMode::EXCLUSIVE);
        LeafPage<K, V> leaf(raw);
        leaf.init(newRootId, INVALID_PAGE);
        leaf.insertKey(0, key, value);
        pm_->unpin(newRootId, true, LatchMode::EXCLUSIVE);
        pm_->setRootPageId(newRootId);
        pm_->flushAll();
        return;
    }

    // Case 2: Tree has at least one page
    std::vector<page_id_t> ancestors;
    page_id_t leafId = findLeaf(key, ancestors);

    char* leafRaw = pm_->pin(leafId, LatchMode::EXCLUSIVE);
    LeafPage<K, V> leaf(leafRaw);

    int n = leaf.numKeys();
    int insertIdx = 0;
    while (insertIdx < n && leaf.getKey(insertIdx) < key) {
        insertIdx++;
    }
    if (insertIdx < n && leaf.getKey(insertIdx) == key) {
        std::cerr << "Duplicate key rejected: " << key << std::endl;
        pm_->unpin(leafId, false, LatchMode::EXCLUSIVE);
        return;
    }

    if (!leaf.isFull()) {
        leaf.insertKey(insertIdx, key, value);
        pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
        pm_->flushAll();
        return;
    }

    // Leaf is full: split leaf into two
    std::vector<K> allKeys;
    std::vector<V> allValues;
    allKeys.reserve(n + 1);
    allValues.reserve(n + 1);
    for (int i = 0; i < insertIdx; i++) {
        allKeys.push_back(leaf.getKey(i));
        allValues.push_back(leaf.getValue(i));
    }
    allKeys.push_back(key);
    allValues.push_back(value);
    for (int i = insertIdx; i < n; i++) {
        allKeys.push_back(leaf.getKey(i));
        allValues.push_back(leaf.getValue(i));
    }

    page_id_t rightId;
    char* rightRaw = pm_->newPage(rightId, LatchMode::EXCLUSIVE);
    LeafPage<K, V> rightLeaf(rightRaw);
    rightLeaf.init(rightId, leaf.parent());

    int total = static_cast<int>(allKeys.size());
    int mid = total / 2;

    leaf.setNumKeys(mid);
    for (int i = 0; i < mid; i++) {
        leaf.setKey(i, allKeys[i]);
        leaf.setValue(i, allValues[i]);
    }

    rightLeaf.setNumKeys(total - mid);
    for (int i = mid; i < total; i++) {
        rightLeaf.setKey(i - mid, allKeys[i]);
        rightLeaf.setValue(i - mid, allValues[i]);
    }

    // Update linked list pointers
    rightLeaf.setNext(leaf.next());
    rightLeaf.setPrev(leafId);

    page_id_t oldNext = leaf.next();
    if (oldNext != INVALID_PAGE) {
        char* oldNextRaw = pm_->pin(oldNext, LatchMode::EXCLUSIVE);
        LeafPage<K, V> nextLeaf(oldNextRaw);
        nextLeaf.setPrev(rightId);
        pm_->unpin(oldNext, true, LatchMode::EXCLUSIVE);
    }
    leaf.setNext(rightId);

    K separator = rightLeaf.getKey(0);
    page_id_t currLeftId = leafId;
    page_id_t currRightId = rightId;

    pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
    pm_->unpin(rightId, true, LatchMode::EXCLUSIVE);

    // Propagate split up ancestor chain (internal pages only route K!)
    while (true) {
        if (ancestors.empty()) {
            page_id_t newRootId;
            char* nrRaw = pm_->newPage(newRootId, LatchMode::EXCLUSIVE);
            InternalPage<K> newRoot(nrRaw);
            newRoot.init(newRootId, INVALID_PAGE);
            newRoot.insertKeyAndRightChild(0, separator, currRightId);
            newRoot.setChild(0, currLeftId);
            pm_->unpin(newRootId, true, LatchMode::EXCLUSIVE);

            char* lRaw = pm_->pin(currLeftId, LatchMode::EXCLUSIVE);
            reinterpret_cast<PageHeader*>(lRaw)->parentPageId = newRootId;
            pm_->unpin(currLeftId, true, LatchMode::EXCLUSIVE);

            char* rRaw = pm_->pin(currRightId, LatchMode::EXCLUSIVE);
            reinterpret_cast<PageHeader*>(rRaw)->parentPageId = newRootId;
            pm_->unpin(currRightId, true, LatchMode::EXCLUSIVE);

            pm_->setRootPageId(newRootId);
            break;
        }

        page_id_t parentId = ancestors.back();
        ancestors.pop_back();

        char* pRaw = pm_->pin(parentId, LatchMode::EXCLUSIVE);
        InternalPage<K> parent(pRaw);

        int pKeys = parent.numKeys();
        int ki = 0;
        while (ki < pKeys && parent.getKey(ki) < separator) {
            ki++;
        }

        if (!parent.isFull()) {
            parent.insertKeyAndRightChild(ki, separator, currRightId);
            char* crRaw = pm_->pin(currRightId, LatchMode::EXCLUSIVE);
            reinterpret_cast<PageHeader*>(crRaw)->parentPageId = parentId;
            pm_->unpin(currRightId, true, LatchMode::EXCLUSIVE);

            pm_->unpin(parentId, true, LatchMode::EXCLUSIVE);
            break;
        }

        // Parent internal page is full, split it
        std::vector<K> allParentKeys;
        allParentKeys.reserve(pKeys + 1);
        for (int i = 0; i < ki; i++) allParentKeys.push_back(parent.getKey(i));
        allParentKeys.push_back(separator);
        for (int i = ki; i < pKeys; i++) allParentKeys.push_back(parent.getKey(i));

        std::vector<page_id_t> allChildren;
        allChildren.reserve(pKeys + 2);
        for (int i = 0; i <= ki; i++) allChildren.push_back(parent.getChild(i));
        allChildren.push_back(currRightId);
        for (int i = ki + 1; i <= pKeys; i++) allChildren.push_back(parent.getChild(i));

        page_id_t rightIntId;
        char* rRaw = pm_->newPage(rightIntId, LatchMode::EXCLUSIVE);
        InternalPage<K> rightInt(rRaw);
        rightInt.init(rightIntId, parent.parent());

        int totalP = static_cast<int>(allParentKeys.size());
        int pMid = totalP / 2;

        parent.setNumKeys(pMid);
        for (int i = 0; i < pMid; i++) {
            parent.setKey(i, allParentKeys[i]);
        }
        for (int i = 0; i <= pMid; i++) {
            parent.setChild(i, allChildren[i]);
        }

        K promotedSep = allParentKeys[pMid];

        int rightKeysCount = totalP - pMid - 1;
        rightInt.setNumKeys(rightKeysCount);
        for (int i = 0; i < rightKeysCount; i++) {
            rightInt.setKey(i, allParentKeys[pMid + 1 + i]);
        }
        for (int i = 0; i <= rightKeysCount; i++) {
            rightInt.setChild(i, allChildren[pMid + 1 + i]);
        }

        for (int i = 0; i <= rightKeysCount; i++) {
            page_id_t cId = rightInt.getChild(i);
            char* cRaw = pm_->pin(cId, LatchMode::EXCLUSIVE);
            reinterpret_cast<PageHeader*>(cRaw)->parentPageId = rightIntId;
            pm_->unpin(cId, true, LatchMode::EXCLUSIVE);
        }

        if (ki <= pMid) {
            char* cRaw = pm_->pin(currRightId, LatchMode::EXCLUSIVE);
            reinterpret_cast<PageHeader*>(cRaw)->parentPageId = parentId;
            pm_->unpin(currRightId, true, LatchMode::EXCLUSIVE);
        }

        pm_->unpin(parentId, true, LatchMode::EXCLUSIVE);
        pm_->unpin(rightIntId, true, LatchMode::EXCLUSIVE);

        currLeftId = parentId;
        currRightId = rightIntId;
        separator = promotedSep;
    }

    pm_->flushAll();
}

template<typename K, typename V>
bool BPlusTree<K, V>::search(const K& key, V& outValue) {
    page_id_t rootId = pm_->rootPageId();
    if (rootId == INVALID_PAGE) return false;

    std::vector<page_id_t> ancestors;
    page_id_t leafId = findLeaf(key, ancestors);
    if (leafId == INVALID_PAGE) return false;

    char* raw = pm_->pin(leafId, LatchMode::SHARED);
    LeafPage<K, V> leaf(raw);
    int n = leaf.numKeys();
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (leaf.getKey(i) == key) {
            outValue = leaf.getValue(i);
            found = true;
            break;
        }
        if (leaf.getKey(i) > key) break;
    }
    pm_->unpin(leafId, false, LatchMode::SHARED);
    return found;
}

template<typename K, typename V>
bool BPlusTree<K, V>::search(const K& val) {
    V outVal{};
    bool found = search(val, outVal);
    if (found) {
        std::cout << "Found: " << val << std::endl;
        return true;
    } else {
        std::cout << "Not found: " << val << std::endl;
        return false;
    }
}

template<typename K, typename V>
void BPlusTree<K, V>::search(const K& lower, const K& upper) {
    page_id_t rootId = pm_->rootPageId();
    if (rootId == INVALID_PAGE) {
        std::cout << "Tree is empty." << std::endl;
        return;
    }
    std::vector<page_id_t> ancestors;
    page_id_t leafId = findLeaf(lower, ancestors);
    if (leafId == INVALID_PAGE) {
        std::cout << "Tree is empty." << std::endl;
        return;
    }

    std::cout << "Range [" << lower << ", " << upper << "]: ";
    bool found = false;
    bool done = false;

    while (leafId != INVALID_PAGE && !done) {
        char* raw = pm_->pin(leafId, LatchMode::SHARED);
        LeafPage<K, V> leaf(raw);
        int n = leaf.numKeys();
        int i = 0;
        while (i < n && leaf.getKey(i) < lower) {
            i++;
        }
        for (; i < n; i++) {
            K k = leaf.getKey(i);
            if (k <= upper) {
                std::cout << k << " ";
                found = true;
            } else {
                done = true;
                break;
            }
        }
        page_id_t nextLeafId = leaf.next();
        pm_->unpin(leafId, false, LatchMode::SHARED);
        leafId = nextLeafId;
    }

    if (!found) {
        std::cout << "(none)";
    }
    std::cout << std::endl;
}

template<typename K, typename V>
std::vector<std::pair<K, V>> BPlusTree<K, V>::scan(const K& lower, const K& upper) {
    std::vector<std::pair<K, V>> results;
    page_id_t rootId = pm_->rootPageId();
    if (rootId == INVALID_PAGE) return results;

    std::vector<page_id_t> ancestors;
    page_id_t leafId = findLeaf(lower, ancestors);
    if (leafId == INVALID_PAGE) return results;

    bool done = false;
    while (leafId != INVALID_PAGE && !done) {
        char* raw = pm_->pin(leafId, LatchMode::SHARED);
        LeafPage<K, V> leaf(raw);
        int n = leaf.numKeys();
        int i = 0;
        while (i < n && leaf.getKey(i) < lower) {
            i++;
        }
        for (; i < n; i++) {
            K k = leaf.getKey(i);
            if (k <= upper) {
                results.push_back({k, leaf.getValue(i)});
            } else {
                done = true;
                break;
            }
        }
        page_id_t nextLeafId = leaf.next();
        pm_->unpin(leafId, false, LatchMode::SHARED);
        leafId = nextLeafId;
    }
    return results;
}

template<typename K, typename V>
std::vector<std::pair<K, V>> BPlusTree<K, V>::scanAll() {
    std::vector<std::pair<K, V>> results;
    page_id_t currId = pm_->rootPageId();
    if (currId == INVALID_PAGE) return results;

    // Follow leftmost child to find the very first leaf
    while (true) {
        char* raw = pm_->pin(currId, LatchMode::SHARED);
        PageHeader* hdr = reinterpret_cast<PageHeader*>(raw);
        if (hdr->pageType == PageType::LEAF) {
            pm_->unpin(currId, false, LatchMode::SHARED);
            break;
        }
        InternalPage<K> internal(raw);
        page_id_t nextId = internal.getChild(0);
        pm_->unpin(currId, false, LatchMode::SHARED);
        currId = nextId;
    }

    // Now currId is the first leaf page, traverse the linked list
    while (currId != INVALID_PAGE) {
        char* raw = pm_->pin(currId, LatchMode::SHARED);
        LeafPage<K, V> leaf(raw);
        int n = leaf.numKeys();
        for (int i = 0; i < n; i++) {
            results.push_back({leaf.getKey(i), leaf.getValue(i)});
        }
        page_id_t nextLeafId = leaf.next();
        pm_->unpin(currId, false, LatchMode::SHARED);
        currId = nextLeafId;
    }
    return results;
}

template<typename K, typename V>
bool BPlusTree<K, V>::deleteNode(const K& x, bool verbose) {
    page_id_t rootId = pm_->rootPageId();
    if (rootId == INVALID_PAGE) return false;

    std::vector<page_id_t> ancestors;
    page_id_t leafId = findLeaf(x, ancestors);
    if (leafId == INVALID_PAGE) return false;

    char* leafRaw = pm_->pin(leafId, LatchMode::EXCLUSIVE);
    LeafPage<K, V> leaf(leafRaw);
    int n = leaf.numKeys();
    int idx = 0;
    while (idx < n && leaf.getKey(idx) < x) {
        idx++;
    }
    if (idx == n || leaf.getKey(idx) != x) {
        if (verbose) std::cout << "Element not present." << std::endl;
        pm_->unpin(leafId, false, LatchMode::EXCLUSIVE);
        return false;
    }

    leaf.removeKey(idx);

    if (leafId == rootId) {
        if (leaf.numKeys() == 0) {
            pm_->unpin(leafId, false, LatchMode::EXCLUSIVE);
            pm_->deletePage(leafId);
            pm_->setRootPageId(INVALID_PAGE);
        } else {
            pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
        }
        pm_->flushAll();
        if (verbose) std::cout << "Successfully deleted element." << std::endl;
        return true;
    }

    int minLeafKeys = (leaf.capacity() + 1) / 2;
    if (leaf.numKeys() >= minLeafKeys) {
        if (!ancestors.empty() && leaf.numKeys() > 0 && idx == 0) {
            page_id_t pId = ancestors.back();
            char* pRaw = pm_->pin(pId, LatchMode::EXCLUSIVE);
            InternalPage<K> parent(pRaw);
            int ci = 0;
            while (ci <= parent.numKeys() && parent.getChild(ci) != leafId) {
                ci++;
            }
            if (ci > 0 && ci - 1 < parent.numKeys()) {
                parent.setKey(ci - 1, leaf.getKey(0));
            }
            pm_->unpin(pId, true, LatchMode::EXCLUSIVE);
        }
        pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
        pm_->flushAll();
        if (verbose) std::cout << "Successfully deleted element." << std::endl;
        return true;
    }

    // Leaf underflow
    page_id_t parentId = ancestors.back();
    ancestors.pop_back();

    char* pRaw = pm_->pin(parentId, LatchMode::EXCLUSIVE);
    InternalPage<K> parent(pRaw);

    int ci = 0;
    while (ci <= parent.numKeys() && parent.getChild(ci) != leafId) {
        ci++;
    }

    // Borrow from left sibling
    if (ci > 0) {
        page_id_t leftSibId = parent.getChild(ci - 1);
        char* leftRaw = pm_->pin(leftSibId, LatchMode::EXCLUSIVE);
        LeafPage<K, V> leftSib(leftRaw);
        if (leftSib.numKeys() > minLeafKeys) {
            K borrowKey = leftSib.getKey(leftSib.numKeys() - 1);
            V borrowVal = leftSib.getValue(leftSib.numKeys() - 1);
            leftSib.removeKey(leftSib.numKeys() - 1);
            leaf.insertKey(0, borrowKey, borrowVal);
            parent.setKey(ci - 1, leaf.getKey(0));

            pm_->unpin(leftSibId, true, LatchMode::EXCLUSIVE);
            pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
            pm_->unpin(parentId, true, LatchMode::EXCLUSIVE);
            pm_->flushAll();
            if (verbose) std::cout << "Successfully deleted element." << std::endl;
            return true;
        }
        pm_->unpin(leftSibId, false, LatchMode::EXCLUSIVE);
    }

    // Borrow from right sibling
    if (ci < parent.numKeys()) {
        page_id_t rightSibId = parent.getChild(ci + 1);
        char* rightRaw = pm_->pin(rightSibId, LatchMode::EXCLUSIVE);
        LeafPage<K, V> rightSib(rightRaw);
        if (rightSib.numKeys() > minLeafKeys) {
            K borrowKey = rightSib.getKey(0);
            V borrowVal = rightSib.getValue(0);
            rightSib.removeKey(0);
            leaf.insertKey(leaf.numKeys(), borrowKey, borrowVal);
            parent.setKey(ci, rightSib.getKey(0));

            pm_->unpin(rightSibId, true, LatchMode::EXCLUSIVE);
            pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
            pm_->unpin(parentId, true, LatchMode::EXCLUSIVE);
            pm_->flushAll();
            if (verbose) std::cout << "Successfully deleted element." << std::endl;
            return true;
        }
        pm_->unpin(rightSibId, false, LatchMode::EXCLUSIVE);
    }

    // Merge leaves
    if (ci > 0) {
        page_id_t leftSibId = parent.getChild(ci - 1);
        char* leftRaw = pm_->pin(leftSibId, LatchMode::EXCLUSIVE);
        LeafPage<K, V> leftSib(leftRaw);

        for (int i = 0; i < leaf.numKeys(); i++) {
            leftSib.insertKey(leftSib.numKeys(), leaf.getKey(i), leaf.getValue(i));
        }
        leftSib.setNext(leaf.next());
        if (leaf.next() != INVALID_PAGE) {
            char* nextRaw = pm_->pin(leaf.next(), LatchMode::EXCLUSIVE);
            LeafPage<K, V> nextLeaf(nextRaw);
            nextLeaf.setPrev(leftSibId);
            pm_->unpin(leaf.next(), true, LatchMode::EXCLUSIVE);
        }
        parent.removeKeyAndRightChild(ci - 1);

        pm_->unpin(leftSibId, true, LatchMode::EXCLUSIVE);
        pm_->unpin(leafId, false, LatchMode::EXCLUSIVE);
        pm_->deletePage(leafId);
    } else {
        page_id_t rightSibId = parent.getChild(ci + 1);
        char* rightRaw = pm_->pin(rightSibId, LatchMode::EXCLUSIVE);
        LeafPage<K, V> rightSib(rightRaw);

        for (int i = 0; i < rightSib.numKeys(); i++) {
            leaf.insertKey(leaf.numKeys(), rightSib.getKey(i), rightSib.getValue(i));
        }
        leaf.setNext(rightSib.next());
        if (rightSib.next() != INVALID_PAGE) {
            char* nextRaw = pm_->pin(rightSib.next(), LatchMode::EXCLUSIVE);
            LeafPage<K, V> nextLeaf(nextRaw);
            nextLeaf.setPrev(leafId);
            pm_->unpin(rightSib.next(), true, LatchMode::EXCLUSIVE);
        }
        parent.removeKeyAndRightChild(ci);

        pm_->unpin(rightSibId, false, LatchMode::EXCLUSIVE);
        pm_->deletePage(rightSibId);
        pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
    }

    // Rebalance internal nodes upward (InternalPage only uses K)
    page_id_t currInternalId = parentId;
    while (true) {
        if (currInternalId == pm_->rootPageId()) {
            if (parent.numKeys() == 0) {
                page_id_t newRoot = parent.getChild(0);
                pm_->unpin(currInternalId, false, LatchMode::EXCLUSIVE);
                pm_->deletePage(currInternalId);

                char* nrRaw = pm_->pin(newRoot, LatchMode::EXCLUSIVE);
                reinterpret_cast<PageHeader*>(nrRaw)->parentPageId = INVALID_PAGE;
                pm_->unpin(newRoot, true, LatchMode::EXCLUSIVE);

                pm_->setRootPageId(newRoot);
            } else {
                pm_->unpin(currInternalId, true, LatchMode::EXCLUSIVE);
            }
            break;
        }

        int minInternalKeys = (parent.capacity() + 1) / 2;
        if (parent.numKeys() >= minInternalKeys || ancestors.empty()) {
            pm_->unpin(currInternalId, true, LatchMode::EXCLUSIVE);
            break;
        }

        page_id_t gpId = ancestors.back();
        ancestors.pop_back();

        char* gpRaw = pm_->pin(gpId, LatchMode::EXCLUSIVE);
        InternalPage<K> gParent(gpRaw);

        int gci = 0;
        while (gci <= gParent.numKeys() && gParent.getChild(gci) != currInternalId) {
            gci++;
        }

        bool resolved = false;
        if (gci > 0) {
            page_id_t leftSibId = gParent.getChild(gci - 1);
            char* lRaw = pm_->pin(leftSibId, LatchMode::EXCLUSIVE);
            InternalPage<K> leftSib(lRaw);
            if (leftSib.numKeys() > minInternalKeys) {
                K parentKey = gParent.getKey(gci - 1);
                K sibKey = leftSib.getKey(leftSib.numKeys() - 1);
                page_id_t sibChild = leftSib.getChild(leftSib.numKeys());

                leftSib.removeKey(leftSib.numKeys() - 1);
                leftSib.removeChild(leftSib.numKeys());
                leftSib.setNumKeys(leftSib.numKeys() - 1);

                parent.insertKey(0, parentKey);
                parent.insertChild(0, sibChild);
                parent.setNumKeys(parent.numKeys() + 1);

                char* cRaw = pm_->pin(sibChild, LatchMode::EXCLUSIVE);
                reinterpret_cast<PageHeader*>(cRaw)->parentPageId = currInternalId;
                pm_->unpin(sibChild, true, LatchMode::EXCLUSIVE);

                gParent.setKey(gci - 1, sibKey);

                pm_->unpin(leftSibId, true, LatchMode::EXCLUSIVE);
                pm_->unpin(currInternalId, true, LatchMode::EXCLUSIVE);
                pm_->unpin(gpId, true, LatchMode::EXCLUSIVE);
                resolved = true;
                break;
            }
            pm_->unpin(leftSibId, false, LatchMode::EXCLUSIVE);
        }

        if (!resolved && gci < gParent.numKeys()) {
            page_id_t rightSibId = gParent.getChild(gci + 1);
            char* rRaw = pm_->pin(rightSibId, LatchMode::EXCLUSIVE);
            InternalPage<K> rightSib(rRaw);
            if (rightSib.numKeys() > minInternalKeys) {
                K parentKey = gParent.getKey(gci);
                K sibKey = rightSib.getKey(0);
                page_id_t sibChild = rightSib.getChild(0);

                rightSib.removeKey(0);
                rightSib.removeChild(0);
                rightSib.setNumKeys(rightSib.numKeys() - 1);

                parent.insertKey(parent.numKeys(), parentKey);
                parent.insertChild(parent.numKeys() + 1, sibChild);
                parent.setNumKeys(parent.numKeys() + 1);

                char* cRaw = pm_->pin(sibChild, LatchMode::EXCLUSIVE);
                reinterpret_cast<PageHeader*>(cRaw)->parentPageId = currInternalId;
                pm_->unpin(sibChild, true, LatchMode::EXCLUSIVE);

                gParent.setKey(gci, sibKey);

                pm_->unpin(rightSibId, true, LatchMode::EXCLUSIVE);
                pm_->unpin(currInternalId, true, LatchMode::EXCLUSIVE);
                pm_->unpin(gpId, true, LatchMode::EXCLUSIVE);
                resolved = true;
                break;
            }
            pm_->unpin(rightSibId, false, LatchMode::EXCLUSIVE);
        }

        if (!resolved) {
            if (gci > 0) {
                page_id_t leftSibId = gParent.getChild(gci - 1);
                char* lRaw = pm_->pin(leftSibId, LatchMode::EXCLUSIVE);
                InternalPage<K> leftSib(lRaw);

                K parentKey = gParent.getKey(gci - 1);
                leftSib.insertKey(leftSib.numKeys(), parentKey);
                leftSib.setNumKeys(leftSib.numKeys() + 1);

                for (int i = 0; i < parent.numKeys(); i++) {
                    leftSib.insertKey(leftSib.numKeys(), parent.getKey(i));
                    leftSib.setNumKeys(leftSib.numKeys() + 1);
                }
                for (int i = 0; i <= parent.numKeys(); i++) {
                    page_id_t cId = parent.getChild(i);
                    leftSib.setChild(leftSib.numKeys() - parent.numKeys() + i, cId);
                    char* cRaw = pm_->pin(cId, LatchMode::EXCLUSIVE);
                    reinterpret_cast<PageHeader*>(cRaw)->parentPageId = leftSibId;
                    pm_->unpin(cId, true, LatchMode::EXCLUSIVE);
                }

                gParent.removeKeyAndRightChild(gci - 1);

                pm_->unpin(leftSibId, true, LatchMode::EXCLUSIVE);
                pm_->unpin(currInternalId, false, LatchMode::EXCLUSIVE);
                pm_->deletePage(currInternalId);

                currInternalId = gpId;
                parent = gParent;
            } else {
                page_id_t rightSibId = gParent.getChild(gci + 1);
                char* rRaw = pm_->pin(rightSibId, LatchMode::EXCLUSIVE);
                InternalPage<K> rightSib(rRaw);

                K parentKey = gParent.getKey(gci);
                parent.insertKey(parent.numKeys(), parentKey);
                parent.setNumKeys(parent.numKeys() + 1);

                for (int i = 0; i < rightSib.numKeys(); i++) {
                    parent.insertKey(parent.numKeys(), rightSib.getKey(i));
                    parent.setNumKeys(parent.numKeys() + 1);
                }
                for (int i = 0; i <= rightSib.numKeys(); i++) {
                    page_id_t cId = rightSib.getChild(i);
                    parent.setChild(parent.numKeys() - rightSib.numKeys() + i, cId);
                    char* cRaw = pm_->pin(cId, LatchMode::EXCLUSIVE);
                    reinterpret_cast<PageHeader*>(cRaw)->parentPageId = currInternalId;
                    pm_->unpin(cId, true, LatchMode::EXCLUSIVE);
                }

                gParent.removeKeyAndRightChild(gci);

                pm_->unpin(rightSibId, false, LatchMode::EXCLUSIVE);
                pm_->deletePage(rightSibId);

                pm_->unpin(currInternalId, true, LatchMode::EXCLUSIVE);
                currInternalId = gpId;
                parent = gParent;
            }
        }
    }

    pm_->flushAll();
    if (verbose) std::cout << "Successfully deleted element." << std::endl;
    return true;
}

template<typename K, typename V>
void BPlusTree<K, V>::printTree() {
    page_id_t rootId = pm_->rootPageId();
    if (rootId == INVALID_PAGE) {
        std::cout << "=== B+ Tree is empty ===" << std::endl;
        return;
    }

    std::cout << "=== B+ Tree ===" << std::endl;
    std::queue<page_id_t> q;
    q.push(rootId);
    int level = 0;

    while (!q.empty()) {
        int sz = static_cast<int>(q.size());
        std::cout << "L" << level << ": ";
        for (int j = 0; j < sz; j++) {
            page_id_t pid = q.front();
            q.pop();

            char* raw = pm_->pin(pid, LatchMode::SHARED);
            PageHeader* hdr = reinterpret_cast<PageHeader*>(raw);

            if (hdr->pageType == PageType::LEAF) {
                LeafPage<K, V> leaf(raw);
                std::cout << "[";
                for (int k = 0; k < leaf.numKeys(); k++) {
                    std::cout << leaf.getKey(k);
                    if (k < leaf.numKeys() - 1) std::cout << "|";
                }
                std::cout << "] ";
            } else {
                InternalPage<K> internal(raw);
                std::cout << "[";
                for (int k = 0; k < internal.numKeys(); k++) {
                    std::cout << internal.getKey(k);
                    if (k < internal.numKeys() - 1) std::cout << "|";
                }
                std::cout << "] ";
                for (int c = 0; c <= internal.numKeys(); c++) {
                    q.push(internal.getChild(c));
                }
            }
            pm_->unpin(pid, false, LatchMode::SHARED);
        }
        std::cout << std::endl;
        level++;
    }
}

template<typename K, typename V>
page_id_t BPlusTree<K, V>::rootPageId() const {
    return pm_ ? pm_->rootPageId() : INVALID_PAGE;
}

template<typename K, typename V>
std::string BPlusTree<K, V>::folderPath() const {
    return folderPath_;
}

template<typename K, typename V>
void BPlusTree<K, V>::flush() {
    if (pm_) {
        pm_->flushAll();
    }
}

#endif // B_PLUS_TREE_IMPL_H
