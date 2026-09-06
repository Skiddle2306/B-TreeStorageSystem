#include "b_plus_tree.h"
template<typename K>
typename BPlusTree<K>::Node* BPlusTree<K>::makeNode() {
    return new Node();
}

template<typename K>
void BPlusTree<K>::destroyTree(Node* node) {
    if (!node) return;
    if (!node->isLeaf)
        for (Node* c : node->children) destroyTree(c);
    delete node;
}


template<typename K>
BPlusTree<K>::BPlusTree() {
    root           = new Node();
    root->isLeaf   = true;
    root->next     = nullptr;
    root->previous = nullptr;
}

template<typename K>
BPlusTree<K>::~BPlusTree() {
    destroyTree(root);
}

// ── splitLeaf ────────────────────────────────────────────────────────────────

template<typename K>
typename BPlusTree<K>::SplitResult BPlusTree<K>::splitLeaf(Node* leaf) {
    int   n     = leaf->keys.size() / 2;
    Node* left  = makeNode();
    Node* right = makeNode();
    left->isLeaf  = true;
    right->isLeaf = true;

    left->previous  = leaf->previous;
    left->next      = right;
    right->previous = left;
    right->next     = leaf->next;
    if (leaf->next) leaf->next->previous = right;

    for (int i = 0; i < n; i++)                       left->keys.push_back(leaf->keys[i]);
    for (int i = n; i < (int)leaf->keys.size(); i++)  right->keys.push_back(leaf->keys[i]);

    return { right->keys[0], left, right };
}


template<typename K>
typename BPlusTree<K>::SplitResult BPlusTree<K>::splitInternal(Node* node) {
    int   n     = node->keys.size() / 2;
    Node* left  = makeNode();
    Node* right = makeNode();

    for (int i = 0; i < n; i++) {
        left->keys.push_back(node->keys[i]);
        left->children.push_back(node->children[i]);
    }
    left->children.push_back(node->children[n]);

    K separator = node->keys[n];

    for (int i = n + 1; i < (int)node->keys.size(); i++) {
        right->keys.push_back(node->keys[i]);
        right->children.push_back(node->children[i]);
    }
    right->children.push_back(node->children[node->keys.size()]);

    return { separator, left, right };
}


template<typename K>
void BPlusTree<K>::insert(const K& x) {
    stack<Node*> visited;
    Node* current = root;

    if (root->keys.empty()) {
        root->keys.push_back(x);
        return;
    }

    while (!current->isLeaf) {
        visited.push(current);
        int i;
        for (i = 0; i < (int)current->keys.size() && current->keys[i] <= x; i++) {}
        current = current->children[i];
    }

    int i;
    for (i = 0; i < (int)current->keys.size() && current->keys[i] < x; i++) {}
    if (i < (int)current->keys.size() && current->keys[i] == x) {
        cerr << "Duplicate key rejected: " << x << endl;
        return;
    }
    current->keys.insert(current->keys.begin() + i, x);

    if ((int)current->keys.size() < ORDER) return;

    SplitResult sr = splitLeaf(current);
    if (current->previous) current->previous->next = sr.left;
    sr.left->previous = current->previous;

    K             separator = sr.separator;
    vector<Node*> children  = { sr.left, sr.right };

    if (visited.empty()) {
        current->keys     = { separator };
        current->children = children;
        current->isLeaf   = false;
        current->previous = nullptr;
        current->next     = nullptr;
        return;
    }

    while (true) {
        if (!visited.empty()) {
            Node* parent = visited.top();
            visited.pop();

            int idx;
            for (idx = 0; parent->children[idx] != current; idx++) {}
            parent->children[idx] = children[0];
            parent->children.insert(parent->children.begin() + idx + 1, children[1]);
            delete current;

            int ki;
            for (ki = 0; ki < (int)parent->keys.size() && parent->keys[ki] < separator; ki++) {}
            parent->keys.insert(parent->keys.begin() + ki, separator);

            if ((int)parent->keys.size() < ORDER) break;

            SplitResult psr = splitInternal(parent);
            children  = { psr.left, psr.right };
            separator = psr.separator;
            current   = parent;
        } else {
            Node* newRoot     = makeNode();
            newRoot->keys     = { separator };
            newRoot->children = children;
            root              = newRoot;
            delete current;
            break;
        }
    }
}

// ── search (point) ───────────────────────────────────────────────────────────

template<typename K>
bool BPlusTree<K>::search(const K& val) {
    if (!root || root->keys.empty()) {
        cout << "Not found: " << val << endl;
        return false;
    }
    Node* t = root;
    while (!t->isLeaf) {
        int i;
        for (i = 0; i < (int)t->keys.size() && val >= t->keys[i]; i++) {}
        t = t->children[i];
    }
    for (const K& k : t->keys) {
        if (k == val) { cout << "Found: " << val << endl; return true; }
        if (k > val)  break;
    }
    cout << "Not found: " << val << endl;
    return false;
}

// ── search (range) ───────────────────────────────────────────────────────────

template<typename K>
void BPlusTree<K>::search(const K& lower, const K& upper) {
    if (!root || root->keys.empty()) {
        cout << "Tree is empty." << endl;
        return;
    }
    Node* t = root;
    while (!t->isLeaf) {
        int i;
        for (i = 0; i < (int)t->keys.size() && lower >= t->keys[i]; i++) {}
        t = t->children[i];
    }
    cout << "Range [" << lower << ", " << upper << "]: ";
    bool found = false;
    while (t) {
        int i;
        for (i = 0; i < (int)t->keys.size() && t->keys[i] < lower; i++) {}
        for (; i < (int)t->keys.size() && t->keys[i] <= upper; i++) {
            cout << t->keys[i] << " ";
            found = true;
        }
        if (i < (int)t->keys.size()) break;
        t = t->next;
    }
    if (!found) cout << "(none)";
    cout << endl;
}

// ── deleteNode ───────────────────────────────────────────────────────────────

template<typename K>
void BPlusTree<K>::deleteNode(const K& x) {
    if (root->keys.empty()) return;

    stack<Node*> ancestors;
    Node* traverse = root;

    while (!traverse->isLeaf) {
        int i;
        for (i = 0; i < (int)traverse->keys.size() && x >= traverse->keys[i]; i++) {}
        ancestors.push(traverse);
        traverse = traverse->children[i];
    }

    int i;
    for (i = 0; i < (int)traverse->keys.size() && traverse->keys[i] != x; i++) {}
    if (i == (int)traverse->keys.size()) {
        cout << "Element not present." << endl;
        return;
    }
    traverse->keys.erase(traverse->keys.begin() + i);

    int minKeys = (int)ceil((ORDER - 1) / 2.0);

    if ((int)traverse->keys.size() >= minKeys) {
        if (!ancestors.empty() && !traverse->keys.empty() && i == 0) {
            Node* parent = ancestors.top();
            int ci;
            for (ci = 0; parent->children[ci] != traverse; ci++) {}
            if (ci > 0) parent->keys[ci - 1] = traverse->keys[0];
        }
        cout << "Successfully deleted element." << endl;
        return;
    }

    if (ancestors.empty()) return;

    while (true) {
        Node* parent = ancestors.top();
        ancestors.pop();

        int ci;
        for (ci = 0; parent->children[ci] != traverse; ci++) {}

        // Borrow from left sibling
        if (ci > 0 && (int)parent->children[ci - 1]->keys.size() > minKeys) {
            Node* leftSib = parent->children[ci - 1];
            if (traverse->isLeaf) {
                traverse->keys.insert(traverse->keys.begin(), leftSib->keys.back());
                leftSib->keys.pop_back();
                parent->keys[ci - 1] = traverse->keys[0];
            } else {
                traverse->keys.insert(traverse->keys.begin(), parent->keys[ci - 1]);
                traverse->children.insert(traverse->children.begin(), leftSib->children.back());
                leftSib->children.pop_back();
                parent->keys[ci - 1] = leftSib->keys.back();
                leftSib->keys.pop_back();
            }
            cout << "Successfully deleted element." << endl;
            return;
        }

        // Borrow from right sibling
        if (ci < (int)parent->children.size() - 1 &&
            (int)parent->children[ci + 1]->keys.size() > minKeys) {
            Node* rightSib = parent->children[ci + 1];
            if (traverse->isLeaf) {
                traverse->keys.push_back(rightSib->keys.front());
                rightSib->keys.erase(rightSib->keys.begin());
                parent->keys[ci] = rightSib->keys[0];
            } else {
                traverse->keys.push_back(parent->keys[ci]);
                traverse->children.push_back(rightSib->children.front());
                rightSib->children.erase(rightSib->children.begin());
                parent->keys[ci] = rightSib->keys.front();
                rightSib->keys.erase(rightSib->keys.begin());
            }
            cout << "Successfully deleted element." << endl;
            return;
        }

        // Merge
        if (ci > 0) {
            Node* leftSib = parent->children[ci - 1];
            if (!traverse->isLeaf)
                leftSib->keys.push_back(parent->keys[ci - 1]);
            for (const K& k : traverse->keys)     leftSib->keys.push_back(k);
            for (Node* c    : traverse->children)  leftSib->children.push_back(c);
            if (traverse->isLeaf) {
                leftSib->next = traverse->next;
                if (traverse->next) traverse->next->previous = leftSib;
            }
            parent->keys.erase(parent->keys.begin() + ci - 1);
            parent->children.erase(parent->children.begin() + ci);
            delete traverse;
        } else {
            Node* rightSib = parent->children[ci + 1];
            if (!traverse->isLeaf)
                traverse->keys.push_back(parent->keys[ci]);
            for (const K& k : rightSib->keys)     traverse->keys.push_back(k);
            for (Node* c    : rightSib->children)  traverse->children.push_back(c);
            if (traverse->isLeaf) {
                traverse->next = rightSib->next;
                if (rightSib->next) rightSib->next->previous = traverse;
            }
            parent->keys.erase(parent->keys.begin() + ci);
            parent->children.erase(parent->children.begin() + ci + 1);
            delete rightSib;
        }

        // Root collapsed
        if (parent == root && parent->keys.empty()) {
            root = parent->children[0];
            delete parent;
            cout << "Successfully deleted element." << endl;
            return;
        }

        if ((int)parent->keys.size() >= minKeys) {
            cout << "Successfully deleted element." << endl;
            return;
        }

        if (ancestors.empty()) {
            cout << "Successfully deleted element." << endl;
            return;
        }

        traverse = parent;
    }
}


template<typename K>
void BPlusTree<K>::printTree() {
    cout << "=== B+ Tree ===" << endl;
    queue<Node*> q;
    q.push(root);
    int level = 0;
    while (!q.empty()) {
        int sz = q.size();
        cout << "L" << level << ": ";
        for (int j = 0; j < sz; j++) {
            Node* n = q.front(); q.pop();
            cout << "[";
            for (int k = 0; k < (int)n->keys.size(); k++) {
                cout << n->keys[k];
                if (k < (int)n->keys.size() - 1) cout << "|";
            }
            cout << "] ";
            for (Node* c : n->children) q.push(c);
        }
        cout << endl;
        level++;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Explicit instantiations — compiler generates code for these types here.
// Add a line for every new type you want to support.
// ─────────────────────────────────────────────────────────────────────────────
template class BPlusTree<int>;
template class BPlusTree<std::string>;
template class BPlusTree<float>;
template class BPlusTree<double>;
template class BPlusTree<char>;