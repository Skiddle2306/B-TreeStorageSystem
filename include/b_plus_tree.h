#ifndef B_PLUS_TREE_H
#define B_PLUS_TREE_H

#include "common.h"
#include "page.h"
#include "page_manager.h"
#include <string>
#include <vector>
#include <memory>
#include <iostream>

template<typename K>
class BPlusTree {
public:
    explicit BPlusTree(const std::string& treeName = "default", int numFrames = 64);
    ~BPlusTree();

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    BPlusTree(BPlusTree&&) noexcept = default;
    BPlusTree& operator=(BPlusTree&&) noexcept = default;

    void insert(const K& x);
    bool search(const K& val);
    void search(const K& lower, const K& upper);
    void deleteNode(const K& x);
    void printTree();

    page_id_t rootPageId() const;
    std::string folderPath() const;
    void flush();

private:
    std::string folderPath_;
    std::unique_ptr<PageManager> pm_;

    void initTree(const std::string& treeName, int numFrames);
    page_id_t findLeaf(const K& key, std::vector<page_id_t>& ancestors);
};

// ─────────────────────────────────────────────────────────────────────────────
// Explicit template declarations
// ─────────────────────────────────────────────────────────────────────────────
extern template class BPlusTree<int>;
extern template class BPlusTree<float>;
extern template class BPlusTree<double>;
extern template class BPlusTree<char>;
extern template class BPlusTree<std::string>;
extern template class BPlusTree<FixedString<256>>;
extern template class BPlusTree<FixedString<128>>;
extern template class BPlusTree<FixedString<64>>;
extern template class BPlusTree<FixedString<32>>;
extern template class BPlusTree<FixedString<16>>;

#endif // B_PLUS_TREE_H
