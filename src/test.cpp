#include"b_plus_tree.h"
#include <iostream>

int main(){
    BPlusTree<int>* t=new BPlusTree<int>();
    t->insert(10);
    t->insert(20);
    t->insert(30);
    t->insert(40);
    t->insert(50);
    t->insert(60);
    t->insert(70);
    t->insert(80);
    t->insert(90);
    t->insert(100);
    t->insert(110);
    t->insert(120);
    t->insert(130);
    t->insert(140);
    t->insert(150);
    t->insert(160);
    t->insert(5);
    t->printTree();    
    t->deleteNode(30);
    t->insert(25);
    t->printTree();
    t->deleteNode(50);
    t->printTree();
    t->insert(170);
    t->printTree();
    t->deleteNode(140);
    t->printTree();

    //Add previous pointer before deletion
    cout << "YAY";
}