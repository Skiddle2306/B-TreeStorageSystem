#include"b_plus_tree.h"
#include <iostream>

int main(){
    tree* t=new tree();
    t->search(10);
    t->insert(10);
    t->search(10);   
    t->insert(20);
    t->insert(30);
    t->insert(40);
    t->search(10);
    t->insert(50);
    t->insert(60);
    t->insert(70);
    t->insert(80);
    t->insert(90);
    t->printTree();
    t->insert(100);
    t->insert(110);
    t->insert(120);
    t->insert(130);
    t->insert(140);
    t->insert(150);
    t->insert(160);
    t->search(70);
    t->search(130);
    t->search(30);
    t->search(190);
    t->printTree();

    t->search(30,50);
    t->search(55,120);

    cout << "YAY";
}