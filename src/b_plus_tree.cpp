#include "b_plus_tree.h"
#include <cstdlib>
#include <iostream>
#include<vector>
#include<stack>
using namespace std;
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

void splitLeaf(tree::node* &leafNode){
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
    leafNode->isLeaf=false;
    leafNode->children.push_back(left);
    leafNode->children.push_back(right);
    
};
void tree::insert(int x){
    //Empty Tree case
    if(root->keys.empty()){
        root->keys.push_back(x);
        return;
    }
    //Handling root as leaf node
    if(root->isLeaf==true){
        int i;
        for(i=0;i < root->keys.size() && root->keys[i]<x  ;i++){}
        if (i < root->keys.size() && root->keys[i] == x) {
            cerr << "Cannot add the same element" << endl;
            exit(EXIT_FAILURE);
        }
        
        root->keys.insert(root->keys.begin()+i,x);
        //Split if overflow occurs (no parent node will be here)
        if(root->keys.size()==order){
            splitLeaf(root);
            //left most element of right split(to insert in the above node)
            int a=root->children[1]->keys[0];
            root->keys.clear();
            root->keys.push_back(a);
        }
    }
}
