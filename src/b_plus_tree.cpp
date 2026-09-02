#include "b_plus_tree.h"
#include <cmath>
#include <cstddef>
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
    a->previous=nullptr;
    return a;
}

tree::tree(){
    root=new node;
    root->isLeaf=true;
    root->next=nullptr;
    root->previous=nullptr;
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



vector<tree::node*> splitLeaf(tree::node* &leafNode){
    int n =leafNode->keys.size()/2;
    tree::node* left=newNode();
    tree::node* right=newNode();
    left->isLeaf=true;
    left->previous=leafNode->previous;
    right->isLeaf=true;
    left->next=right;
    right->previous=left;
    right->next=leafNode->next;
    if (leafNode->next != nullptr) {
        leafNode->next->previous = right;  
    }
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
            if(current->previous!=nullptr){
                 current->previous->next = children[0];
            }
            children[0]->previous = current->previous;
            //left most element of right split(to insert in the above node)
            int a=children[1]->keys[0];
            if(visited.empty()){
                current->keys.clear();
                current->keys.push_back(a);
                current->children=children;
                current->isLeaf=false;
                current->previous=nullptr;
                current->next=nullptr;
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

bool tree::search(int val){
    tree::node* traverse=root;
    if(traverse==nullptr){
        cout << "Val : "<< val <<" not found. " << endl;
        return false;
    }
    if(traverse->keys.empty()){
        cout << "Val : "<< val <<" not found. " << endl;
        return false;
    }
    while(traverse->isLeaf!=true){
        int i;
        for(i=0;i<traverse->keys.size() && val >= traverse->keys[i];i++){}
        traverse=traverse->children[i];
    }
    for(int i : traverse->keys){
        if(i==val){
            cout << "Val : "<< val << " found." << endl;
            return true;
        }
    }
    cout << "Val : "<< val <<" not found. " << endl;
    return false;
}
void tree::search(int lower,int upper){
    tree::node* traverse=root;
    if(traverse==nullptr){
        cout << "Val : "<< lower <<" not found. " << endl;
        return;
    }
    if(traverse->keys.empty()){
        cout << "Val : "<< lower <<" not found. " << endl;
        return;
    }
    while(traverse->isLeaf!=true){
        int i;
        for(i=0;i<traverse->keys.size() && lower >= traverse->keys[i];i++){}
        traverse=traverse->children[i];
    }
    cout << "Values are : ";
    while(traverse!=nullptr){
        int i;
        for(i=0;traverse->keys[i]<lower;i++){}
        if(i==traverse->keys.size()){
            traverse=traverse->next;
        }else{
            for(;i<traverse->keys.size() && traverse->keys[i] <=upper;i++){
                cout << traverse->keys[i] << " ";
            }
            traverse=traverse->next;
            break;
        }
    }
    while(traverse!=nullptr){
        int i;
        for(i=0;i<traverse->keys.size() && traverse->keys[i] <=upper;i++){
            cout << traverse->keys[i] << " ";
        }if(i!=traverse->keys.size()){
            cout << endl;
            return;
        }
        traverse=traverse->next;
    }
    cout << endl;
}
void tree::deleteNode(int x){
    if(root->keys.empty()){
        return;
    }
    stack<tree::node*> ancestors;
    tree::node* traverse= root;
    while(!traverse->isLeaf){
        int i;
        for(i=0;i<traverse->keys.size() && x>=traverse->keys[i];i++){}
        ancestors.push(traverse);
        traverse=traverse->children[i];
    }int i;
    for(i=0;i<traverse->keys.size() && traverse->keys[i]!= x;i++){}
    if(i==traverse->keys.size()){
        cout << "Element not present." << endl;;
        return;
    }
    traverse->keys.erase(traverse->keys.begin()+i);
    int minKeys=ceil((order-1)/2.0 );
    //More than enough elements are present to delete without borrowing
    if(traverse->keys.size()>=minKeys){
        cout << "Successfully deleted Element" << endl;
        return;
    }else{
        if(!ancestors.empty()){
                tree::node* parent=ancestors.top();
                for(i=0;parent->children[i]!=traverse;i++){}
                //Not the leftmost Child and left sibling has extra elements
                if(i>0 && parent->children[i-1]->keys.size()>minKeys){
                    x=parent->children[i-1]->keys[parent->children[i-1]->keys.size()-1];
                    parent->children[i-1]->keys.pop_back();
                    traverse->keys.insert(traverse->keys.begin(),x);
                    //Verify This line later
                    parent->keys[i-1] = traverse->keys[0];
                    return;
                }
                //Not the rightmost child and right sibling has extra elements
                else if(i<parent->children.size()-1 && parent->children[i+1]->keys.size()>minKeys){
                    x=parent->children[i+1]->keys[0];
                    parent->children[i+1]->keys.erase(parent->children[i+1]->keys.begin());
                    traverse->keys.push_back(x);
                    //Verify This line later
                    parent->keys[i]=parent->children[i+1]->keys[0];
                    return;
                }
                //Has parent and merge nodes(make sure to change linked list pointers if needed)
                //merge with left child
                if(i!=0){
                    if(traverse->previous!=nullptr){
                        node* leftSibling=traverse->previous;
                        for(auto i : traverse->keys){
                            leftSibling->keys.push_back(i);
                        }
                        for(auto i : traverse->children){
                            leftSibling->children.push_back(i);
                        }
                        leftSibling->next=traverse->next;
                        if(traverse->next!=nullptr){
                            traverse->next->previous=leftSibling;
                        }
                        
                        for(i=0;i<parent->children.size()&& parent->children[i]!=traverse;i++){}
                        parent->children.erase(parent->children.begin()+i);
                        delete traverse;
                        traverse=leftSibling;
                        parent->keys[i-1]=leftSibling->keys[0];
                    }
                }else if(i!=parent->children.size()-1) {//Merge with right sibling
                        
                }else{
                    node* sibling;
                    if(i==0){
                        sibling=traverse->next;
                        for(auto i : traverse->keys){
                            sibling->keys.push_back(i);
                        }
                        for(auto i : traverse->children){
                            sibling->children.push_back(i);
                        }
                        root=sibling;
                        delete traverse;
                        return;
                    }else{
                        sibling=traverse->previous;
                        for(auto i : sibling->keys){
                            traverse->keys.push_back(i);
                        }
                        for(auto i : sibling->children){
                            traverse->children.push_back(i);
                        }
                        root=traverse;
                        delete sibling;
                        return;
                    }
                    
                }
                //no parent in which case its the root element where root gets deleted and new root is chosen aka the left child after taking element from the right child
        }else{
            //This is when the root node is the leaf node in which case no need to do anything
            return;
        }
    }
}
