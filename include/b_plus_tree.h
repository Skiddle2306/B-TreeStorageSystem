#ifndef B_PLUS_TREE_H
#define B_PLUS_TREE_H
#include <cmath>
#include <iostream>
#include <stack>
#include <queue>
#include <cstdlib>
#include <vector>
#include <string>
using namespace std;

#define ORDER 4

using page_id_t = int32_t;
constexpr page_id_t INVALID_PAGE = -1;

template<typename K>
class BPlusTree {
public:
    struct Node {
        bool          isLeaf;
        vector<K>     keys;
        vector<Node*> children;
        Node*         next;
        Node*         previous;

        // Future disk fields (swap in when paging lands):
        // page_id_t page_id      = INVALID_PAGE;
        // page_id_t prev_page    = INVALID_PAGE;
        // page_id_t next_page    = INVALID_PAGE;
        // page_id_t parent_page  = INVALID_PAGE;

        Node() : isLeaf(false), next(nullptr), previous(nullptr) {
            keys.reserve(ORDER - 1);
            children.reserve(ORDER);
        }
    };

    BPlusTree();
    ~BPlusTree();

    void insert(const K& x);
    bool search(const K& val);
    void search(const K& lower, const K& upper);
    void deleteNode(const K& x);
    void printTree();

private:
    Node* root;

    struct SplitResult {
        K     separator;
        Node* left;
        Node* right;
    };

    Node*       makeNode();
    SplitResult splitLeaf(Node* leaf);
    SplitResult splitInternal(Node* node);
    void        destroyTree(Node* node);
};

extern template class BPlusTree<int>;
extern template class BPlusTree<std::string>;
extern template class BPlusTree<float>;
extern template class BPlusTree<double>;
extern template class BPlusTree<char>;

#endif // B_PLUS_TREE_H