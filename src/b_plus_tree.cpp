#include "b_plus_tree.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <queue>
#include <vector>
#include <iostream>
#include <cstring>

static std::string resolveTreeFolder(const std::string& name) {
    if (name.empty()) {
        return "data/default";
    }
    if (name.rfind("data/", 0) == 0 || name.rfind("./data/", 0) == 0) {
        return name;
    }
    return "data/" + name;
}

template<typename K>
BPlusTree<K>::BPlusTree(const std::string& treeName, int numFrames) {
    initTree(treeName, numFrames);
}

template<typename K>
BPlusTree<K>::~BPlusTree() {
    flush();
}

template<typename K>
void BPlusTree<K>::initTree(const std::string& treeName, int numFrames) {
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
    } else {
        TreeMetadata meta{};
        meta.magic = DB_META_MAGIC;
        meta.keyTypeId = static_cast<uint32_t>(KeyTypeInfo<K>::typeId);
        meta.keyDiskSize = static_cast<uint32_t>(KeyTraits<K>::diskSize);
        meta.maxStringLen = (KeyTypeInfo<K>::typeId == KeyTypeId::FIXED_STRING) ? KeyTraits<K>::diskSize : 0;
        std::memset(meta.typeName, 0, sizeof(meta.typeName));
        std::strncpy(meta.typeName, KeyTypeInfo<K>::name, sizeof(meta.typeName) - 1);

        std::ofstream ofs(metaFile, std::ios::binary);
        if (!ofs.is_open()) {
            throw std::runtime_error("BPlusTree: cannot create metadata file: " + metaFile);
        }
        ofs.write(reinterpret_cast<const char*>(&meta), sizeof(meta));
        ofs.flush();
    }

    pm_ = std::make_unique<PageManager>(dbFile, numFrames);
}

template<typename K>
page_id_t BPlusTree<K>::findLeaf(const K& key, std::vector<page_id_t>& ancestors) {
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

template<typename K>
void BPlusTree<K>::insert(const K& x) {
    page_id_t rootId = pm_->rootPageId();

    // Case 1: Empty tree
    if (rootId == INVALID_PAGE) {
        page_id_t newRootId;
        char* raw = pm_->newPage(newRootId, LatchMode::EXCLUSIVE);
        LeafPage<K> leaf(raw);
        leaf.init(newRootId, INVALID_PAGE);
        leaf.insertKey(0, x);
        pm_->unpin(newRootId, true, LatchMode::EXCLUSIVE);
        pm_->setRootPageId(newRootId);
        pm_->flushAll();
        return;
    }

    // Case 2: Tree has at least one page
    std::vector<page_id_t> ancestors;
    page_id_t leafId = findLeaf(x, ancestors);

    char* leafRaw = pm_->pin(leafId, LatchMode::EXCLUSIVE);
    LeafPage<K> leaf(leafRaw);

    int n = leaf.numKeys();
    int insertIdx = 0;
    while (insertIdx < n && leaf.getKey(insertIdx) < x) {
        insertIdx++;
    }
    if (insertIdx < n && leaf.getKey(insertIdx) == x) {
        std::cerr << "Duplicate key rejected: " << x << std::endl;
        pm_->unpin(leafId, false, LatchMode::EXCLUSIVE);
        return;
    }

    if (!leaf.isFull()) {
        leaf.insertKey(insertIdx, x);
        pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
        pm_->flushAll();
        return;
    }

    // Leaf is full: split leaf into two
    std::vector<K> allKeys;
    allKeys.reserve(n + 1);
    for (int i = 0; i < insertIdx; i++) allKeys.push_back(leaf.getKey(i));
    allKeys.push_back(x);
    for (int i = insertIdx; i < n; i++) allKeys.push_back(leaf.getKey(i));

    page_id_t rightId;
    char* rightRaw = pm_->newPage(rightId, LatchMode::EXCLUSIVE);
    LeafPage<K> rightLeaf(rightRaw);
    rightLeaf.init(rightId, leaf.parent());

    int total = static_cast<int>(allKeys.size());
    int mid = total / 2;

    leaf.setNumKeys(mid);
    for (int i = 0; i < mid; i++) {
        leaf.setKey(i, allKeys[i]);
    }

    rightLeaf.setNumKeys(total - mid);
    for (int i = mid; i < total; i++) {
        rightLeaf.setKey(i - mid, allKeys[i]);
    }

    // Update linked list pointers
    rightLeaf.setNext(leaf.next());
    rightLeaf.setPrev(leafId);

    page_id_t oldNext = leaf.next();
    if (oldNext != INVALID_PAGE) {
        char* oldNextRaw = pm_->pin(oldNext, LatchMode::EXCLUSIVE);
        LeafPage<K> nextLeaf(oldNextRaw);
        nextLeaf.setPrev(rightId);
        pm_->unpin(oldNext, true, LatchMode::EXCLUSIVE);
    }
    leaf.setNext(rightId);

    K separator = rightLeaf.getKey(0);
    page_id_t currLeftId = leafId;
    page_id_t currRightId = rightId;

    pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
    pm_->unpin(rightId, true, LatchMode::EXCLUSIVE);

    // Propagate split up ancestor chain
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

template<typename K>
bool BPlusTree<K>::search(const K& val) {
    page_id_t rootId = pm_->rootPageId();
    if (rootId == INVALID_PAGE) {
        std::cout << "Not found: " << val << std::endl;
        return false;
    }
    std::vector<page_id_t> ancestors;
    page_id_t leafId = findLeaf(val, ancestors);
    if (leafId == INVALID_PAGE) {
        std::cout << "Not found: " << val << std::endl;
        return false;
    }

    char* raw = pm_->pin(leafId, LatchMode::SHARED);
    LeafPage<K> leaf(raw);
    int n = leaf.numKeys();
    bool found = false;
    for (int i = 0; i < n; i++) {
        K k = leaf.getKey(i);
        if (k == val) {
            found = true;
            break;
        }
        if (k > val) break;
    }
    pm_->unpin(leafId, false, LatchMode::SHARED);

    if (found) {
        std::cout << "Found: " << val << std::endl;
        return true;
    } else {
        std::cout << "Not found: " << val << std::endl;
        return false;
    }
}

template<typename K>
void BPlusTree<K>::search(const K& lower, const K& upper) {
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
        LeafPage<K> leaf(raw);
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

template<typename K>
void BPlusTree<K>::deleteNode(const K& x) {
    page_id_t rootId = pm_->rootPageId();
    if (rootId == INVALID_PAGE) return;

    std::vector<page_id_t> ancestors;
    page_id_t leafId = findLeaf(x, ancestors);
    if (leafId == INVALID_PAGE) return;

    char* leafRaw = pm_->pin(leafId, LatchMode::EXCLUSIVE);
    LeafPage<K> leaf(leafRaw);
    int n = leaf.numKeys();
    int idx = 0;
    while (idx < n && leaf.getKey(idx) < x) {
        idx++;
    }
    if (idx == n || leaf.getKey(idx) != x) {
        std::cout << "Element not present." << std::endl;
        pm_->unpin(leafId, false, LatchMode::EXCLUSIVE);
        return;
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
        std::cout << "Successfully deleted element." << std::endl;
        return;
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
        std::cout << "Successfully deleted element." << std::endl;
        return;
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
        LeafPage<K> leftSib(leftRaw);
        if (leftSib.numKeys() > minLeafKeys) {
            K borrowKey = leftSib.getKey(leftSib.numKeys() - 1);
            leftSib.removeKey(leftSib.numKeys() - 1);
            leaf.insertKey(0, borrowKey);
            parent.setKey(ci - 1, leaf.getKey(0));

            pm_->unpin(leftSibId, true, LatchMode::EXCLUSIVE);
            pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
            pm_->unpin(parentId, true, LatchMode::EXCLUSIVE);
            pm_->flushAll();
            std::cout << "Successfully deleted element." << std::endl;
            return;
        }
        pm_->unpin(leftSibId, false, LatchMode::EXCLUSIVE);
    }

    // Borrow from right sibling
    if (ci < parent.numKeys()) {
        page_id_t rightSibId = parent.getChild(ci + 1);
        char* rightRaw = pm_->pin(rightSibId, LatchMode::EXCLUSIVE);
        LeafPage<K> rightSib(rightRaw);
        if (rightSib.numKeys() > minLeafKeys) {
            K borrowKey = rightSib.getKey(0);
            rightSib.removeKey(0);
            leaf.insertKey(leaf.numKeys(), borrowKey);
            parent.setKey(ci, rightSib.getKey(0));

            pm_->unpin(rightSibId, true, LatchMode::EXCLUSIVE);
            pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
            pm_->unpin(parentId, true, LatchMode::EXCLUSIVE);
            pm_->flushAll();
            std::cout << "Successfully deleted element." << std::endl;
            return;
        }
        pm_->unpin(rightSibId, false, LatchMode::EXCLUSIVE);
    }

    // Merge leaves
    if (ci > 0) {
        page_id_t leftSibId = parent.getChild(ci - 1);
        char* leftRaw = pm_->pin(leftSibId, LatchMode::EXCLUSIVE);
        LeafPage<K> leftSib(leftRaw);

        for (int i = 0; i < leaf.numKeys(); i++) {
            leftSib.insertKey(leftSib.numKeys(), leaf.getKey(i));
        }
        leftSib.setNext(leaf.next());
        if (leaf.next() != INVALID_PAGE) {
            char* nextRaw = pm_->pin(leaf.next(), LatchMode::EXCLUSIVE);
            LeafPage<K> nextLeaf(nextRaw);
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
        LeafPage<K> rightSib(rightRaw);

        for (int i = 0; i < rightSib.numKeys(); i++) {
            leaf.insertKey(leaf.numKeys(), rightSib.getKey(i));
        }
        leaf.setNext(rightSib.next());
        if (rightSib.next() != INVALID_PAGE) {
            char* nextRaw = pm_->pin(rightSib.next(), LatchMode::EXCLUSIVE);
            LeafPage<K> nextLeaf(nextRaw);
            nextLeaf.setPrev(leafId);
            pm_->unpin(rightSib.next(), true, LatchMode::EXCLUSIVE);
        }
        parent.removeKeyAndRightChild(ci);

        pm_->unpin(rightSibId, false, LatchMode::EXCLUSIVE);
        pm_->deletePage(rightSibId);
        pm_->unpin(leafId, true, LatchMode::EXCLUSIVE);
    }

    // Rebalance internal nodes upward
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
    std::cout << "Successfully deleted element." << std::endl;
}

template<typename K>
void BPlusTree<K>::printTree() {
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
                LeafPage<K> leaf(raw);
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

template<typename K>
page_id_t BPlusTree<K>::rootPageId() const {
    return pm_ ? pm_->rootPageId() : INVALID_PAGE;
}

template<typename K>
std::string BPlusTree<K>::folderPath() const {
    return folderPath_;
}

template<typename K>
void BPlusTree<K>::flush() {
    if (pm_) {
        pm_->flushAll();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Explicit template instantiations
// ─────────────────────────────────────────────────────────────────────────────
template class BPlusTree<int>;
template class BPlusTree<float>;
template class BPlusTree<double>;
template class BPlusTree<char>;
template class BPlusTree<std::string>;
template class BPlusTree<FixedString<256>>;
template class BPlusTree<FixedString<128>>;
template class BPlusTree<FixedString<64>>;
template class BPlusTree<FixedString<32>>;
template class BPlusTree<FixedString<16>>;