#include "b_plus_tree.h"
#include <cstdlib>
#include <iostream>
#include<vector>
#include<stack>
#include<queue>
using namespace std;

typedef struct values{
    int val;
    tree::node* left;
    tree::node* right;
}intervalSplitReturn;
tree::node* newNode(){
    tree::node* a=new tree::node;
    a->keys.reserve(order-1);
    a->children.reserve(order);
    a->isLeaf=false;
    a->next=nullptr;
    return a;
}

tree::tree(){
    root=new node;
    root->isLeaf=true;
    root->next=nullptr;
    
}
void tree::printTree() {
    cout << "Printing Tree : " << endl;
    queue<tree::node*> nextNodes;
    nextNodes.push(root);
    int level = 0;
    while (!nextNodes.empty()) {
        int nodesInLevel = nextNodes.size();
        cout << "Level " << level << ": ";
        for (int i = 0; i < nodesInLevel; i++) {
            tree::node* traverse = nextNodes.front();
            nextNodes.pop();
            cout << "[";
            for (int key : traverse->keys) {
                cout << key << " ";
            }
            cout << "] ";
            for (tree::node* child : traverse->children) {
                nextNodes.push(child);
            }
        }
        cout << endl;
        level++;
    }
}
intervalSplitReturn splitInterval(tree::node* intervalNode){
    int n=intervalNode->keys.size()/2;
    tree::node* left=newNode();
    tree::node* right=newNode();
    int i=0;
    while (i<n) {
        left->keys.push_back(intervalNode->keys[i]);
        left->children.push_back(intervalNode->children[i]);
        i++;
    }
    left->children.push_back(intervalNode->children[i]);
    int val=intervalNode->keys[i++];

    while(i<intervalNode->keys.size()){
        right->keys.push_back(intervalNode->keys[i]);
        right->children.push_back(intervalNode->children[i]);
        i++;
    }
    right->children.push_back(intervalNode->children[i]);
    return  {val,left,right};
}

tree::node* findPredecessorLeaf(stack<tree::node*> ancestors, tree::node* child){
    while(!ancestors.empty()){
        tree::node* parent = ancestors.top();
        ancestors.pop();
        int idx;
        for(idx=0; parent->children[idx]!=child; idx++){}
        if(idx>0){
            tree::node* n = parent->children[idx-1];
            while(!n->isLeaf) n = n->children.back();
            return n;
        }
        child = parent; // was leftmost child, keep climbing
    }
    return nullptr; // child is the leftmost leaf in the whole tree
}

vector<tree::node*> splitLeaf(tree::node* &leafNode){
    int n =leafNode->keys.size()/2;
    tree::node* left=newNode();
    tree::node* right=newNode();
    left->isLeaf=true;
    right->isLeaf=true;
    left->next=right;
    right->next=leafNode->next;
    int i=0;
    while(i<n){
        left->keys.push_back(leafNode->keys[i]);
        i++;
    }
    while(i<leafNode->keys.size()){
        right->keys.push_back(leafNode->keys[i]);
        i++;
    }
    vector<tree::node*> ret = {left,right};
    return ret;
}
void tree::insert(int x){
    
    stack<node*> visited;
    node* current=root;
    //Empty Tree case
    if(root->keys.empty()){
        root->keys.push_back(x);
        return;
    }
    //Finding the leaf node to insert
    while (current->isLeaf!=true) {
        visited.push(current);
        int i;
        for(i=0;i < current->keys.size() && current->keys[i] <= x;i++){}
        current=current->children[i];
    }
    //Handling root as leaf node
    if(current->isLeaf==true){
        int i;
        for(i=0;i < current->keys.size() && current->keys[i]<x  ;i++){}
        if (i < current->keys.size() && current->keys[i] == x) {
            cerr << "Cannot add the same element" << endl;
            exit(EXIT_FAILURE);
        }    
        current->keys.insert(current->keys.begin()+i,x);
        //Split if overflow occurs (no parent node will be here)
        if(current->keys.size()==order){   
            vector<tree::node*> children = splitLeaf(current);
            tree::node* predecessor = findPredecessorLeaf(visited, current);
            if(predecessor != nullptr){
                predecessor->next = children[0];
            }
            //left most element of right split(to insert in the above node)
            int a=children[1]->keys[0];
            if(visited.empty()){
                current->keys.clear();
                current->keys.push_back(a);
                current->children=children;
                current->isLeaf=false;
            }else{
                while(true){
                    if(!visited.empty()){
                        tree::node* parent=visited.top();
                        visited.pop();
                        int i;
                        for(i=0;parent->children[i]!=current;i++){}
                        parent->children[i]=children[0];
                        parent->children.insert(parent->children.begin()+i+1,children[1]);
                        delete current;
                        for(i=0;i < parent->keys.size() && parent->keys[i]<a  ;i++){}
                        parent->keys.insert(parent->keys.begin()+i,a);
                        if(parent->keys.size()==order){
                            intervalSplitReturn ret= splitInterval(parent);
                            children = {ret.left,ret.right};
                            a=ret.val;
                            current=parent;
                        }else{
                            break;
                        }
                    }
                    else{
                     
                        node* b=newNode();
                        b->children=children;
                        b->keys.push_back(a);
                        root=b;
                        delete current;
                        break;
                    }
                }
            }
        }
    }
}
