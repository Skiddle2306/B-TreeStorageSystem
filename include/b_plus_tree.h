#ifndef B_PLUS_TREE_H
#define B_PLUS_TREE_H

#include "common.h"
#include "page.h"
#include "page_manager.h"
#include <string>
#include <vector>
#include <memory>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// BPlusTree<K, V>
//
// Persistent, on-disk Key-Value B+ Tree.
// - K: Primary key type (sorted, unique). Used in internal routing nodes & leaf nodes.
// - V: Value / payload type (defaults to char for key-only sets). Stored ONLY in leaf nodes.
// ─────────────────────────────────────────────────────────────────────────────
template<typename K, typename V = char>
class BPlusTree {
public:
    explicit BPlusTree(const std::string& treeName = "default", int numFrames = 64);
    ~BPlusTree();

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    BPlusTree(BPlusTree&&) noexcept = default;
    BPlusTree& operator=(BPlusTree&&) noexcept = default;

    // Insert key with optional value (defaults to V{} for key-only trees)
    void insert(const K& key, const V& value = V{});

    // Point search: retrieves value associated with key, returns true if found
    bool search(const K& key, V& outValue);

    // Overload: checks if key exists (and prints Found/Not found)
    bool search(const K& key);

    // Range search: prints all keys in [lower, upper]
    void search(const K& lower, const K& upper);

    // Range scan: returns all (key, value) pairs in [lower, upper]
    std::vector<std::pair<K, V>> scan(const K& lower, const K& upper);

    // Full table scan: returns all (key, value) pairs in the entire tree
    std::vector<std::pair<K, V>> scanAll();

    // Delete key (and its value) from tree, returns true if deleted, false if not found
    bool deleteNode(const K& key, bool verbose = true);

    // Visualise the tree level by level
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

// Include complete template implementation
#include "b_plus_tree_impl.h"

#endif // B_PLUS_TREE_H
