#include<vector>
using namespace std;
#ifndef b_tree_h
#define b_tree_h

#define order 4

class tree{
    public:
    struct node{
        bool isLeaf;
        vector<int> keys;
        vector<node*> children;
        node* next;
    };
    node* newNode();
    tree();
    void insert(int x);
    private:
    node* root;
};

#endif