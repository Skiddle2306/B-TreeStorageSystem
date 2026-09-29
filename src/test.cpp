#include "b_plus_tree.h"
#include <iostream>
#include <cassert>
#include <filesystem>

void testIntTreePersistence() {
    std::cout << "\n=========================================\n";
    std::cout << "TEST 1: Integer B+ Tree with Persistence\n";
    std::cout << "=========================================\n";

    const std::string treeName = "test_int_tree";
    // Clean up previous test run if exists
    std::filesystem::remove_all("data/" + treeName);

    std::cout << "[Step 1] Creating a new B+ tree in 'data/" << treeName << "'...\n";
    {
        BPlusTree<int> tree(treeName);

        std::vector<int> values = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 5};
        for (int v : values) {
            tree.insert(v);
        }

        std::cout << "\nTree after initial insertions:\n";
        tree.printTree();

        std::cout << "\nPerforming point searches:\n";
        assert(tree.search(50) == true);
        assert(tree.search(5) == true);
        assert(tree.search(160) == true);
        assert(tree.search(999) == false);

        std::cout << "\nPerforming range search [25, 75]:\n";
        tree.search(25, 75);

        std::cout << "\nDeleting 30, 50, 140 and inserting 25, 170:\n";
        tree.deleteNode(30);
        tree.insert(25);
        tree.deleteNode(50);
        tree.insert(170);
        tree.deleteNode(140);

        std::cout << "\nTree after deletions and new insertions:\n";
        tree.printTree();

        assert(tree.search(30) == false);
        assert(tree.search(50) == false);
        assert(tree.search(140) == false);
        assert(tree.search(25) == true);
        assert(tree.search(170) == true);

        std::cout << "\nClosing tree (destructor flushes to disk)...\n";
    }

    std::cout << "\n[Step 2] Reopening existing B+ tree from 'data/" << treeName << "'...\n";
    {
        BPlusTree<int> reloadedTree(treeName);

        std::cout << "Tree after reload from disk:\n";
        reloadedTree.printTree();

        std::cout << "Verifying persistence of all keys:\n";
        assert(reloadedTree.search(10) == true);
        assert(reloadedTree.search(20) == true);
        assert(reloadedTree.search(25) == true);
        assert(reloadedTree.search(40) == true);
        assert(reloadedTree.search(60) == true);
        assert(reloadedTree.search(170) == true);
        assert(reloadedTree.search(30) == false);
        assert(reloadedTree.search(50) == false);
        assert(reloadedTree.search(140) == false);

        std::cout << "Inserting 200 into reloaded tree:\n";
        reloadedTree.insert(200);
        assert(reloadedTree.search(200) == true);
        reloadedTree.printTree();
    }

    std::cout << ">>> TEST 1 PASSED! <<<\n";
}

void testStringTree() {
    std::cout << "\n=========================================\n";
    std::cout << "TEST 2: FixedString B+ Tree with Persistence\n";
    std::cout << "=========================================\n";

    const std::string treeName = "test_str_tree";
    std::filesystem::remove_all("data/" + treeName);

    using StrKey = FixedString<64>; // 64-byte fixed char array

    std::cout << "[Step 1] Creating a new string B+ tree in 'data/" << treeName << "'...\n";
    {
        BPlusTree<StrKey> tree(treeName);

        std::vector<std::string> words = {
            "apple", "banana", "cherry", "date", "elderberry",
            "fig", "grape", "honeydew", "kiwi", "lemon", "mango"
        };

        for (const auto& w : words) {
            tree.insert(StrKey(w));
        }

        std::cout << "\nString tree after insertions:\n";
        tree.printTree();

        assert(tree.search(StrKey("cherry")) == true);
        assert(tree.search(StrKey("mango")) == true);
        assert(tree.search(StrKey("watermelon")) == false);

        std::cout << "\nRange search [\"c\", \"m\"]:\n";
        tree.search(StrKey("c"), StrKey("m"));

        std::cout << "\nDeleting \"cherry\" and \"fig\"...\n";
        tree.deleteNode(StrKey("cherry"));
        tree.deleteNode(StrKey("fig"));

        tree.printTree();
        assert(tree.search(StrKey("cherry")) == false);
        assert(tree.search(StrKey("fig")) == false);
        assert(tree.search(StrKey("apple")) == true);
    }

    std::cout << "\n[Step 2] Reloading string tree from disk...\n";
    {
        BPlusTree<StrKey> reloadedTree(treeName);
        std::cout << "Reloaded string tree:\n";
        reloadedTree.printTree();

        assert(reloadedTree.search(StrKey("apple")) == true);
        assert(reloadedTree.search(StrKey("banana")) == true);
        assert(reloadedTree.search(StrKey("cherry")) == false);
        assert(reloadedTree.search(StrKey("fig")) == false);
        assert(reloadedTree.search(StrKey("mango")) == true);
    }

    std::cout << ">>> TEST 2 PASSED! <<<\n";
}

void testTypeMismatchSafety() {
    std::cout << "\n=========================================\n";
    std::cout << "TEST 3: Metadata Type Validation\n";
    std::cout << "=========================================\n";

    // Attempting to open the int tree as a float or string tree should be rejected
    bool exceptionCaught = false;
    try {
        BPlusTree<float> wrongTree("test_int_tree");
    } catch (const std::exception& e) {
        std::cout << "Successfully caught expected type mismatch error:\n  " << e.what() << "\n";
        exceptionCaught = true;
    }
    assert(exceptionCaught);
    std::cout << ">>> TEST 3 PASSED! <<<\n";
}

void testMultiLevelSplits() {
    std::cout << "\n=========================================\n";
    std::cout << "TEST 4: Multi-Level Splits & Large Insertions\n";
    std::cout << "=========================================\n";

    const std::string treeName = "test_multilevel_tree";
    std::filesystem::remove_all("data/" + treeName);

    using StrKey = FixedString<64>; // max 63 keys per leaf page
    const int N = 200;

    std::cout << "[Step 1] Inserting " << N << " keys to trigger page splits...\n";
    {
        BPlusTree<StrKey> tree(treeName);
        for (int i = 0; i < N; i++) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "key_%04d", i);
            tree.insert(StrKey(buf));
        }

        std::cout << "Tree structure after " << N << " inserts:\n";
        tree.printTree();

        // Verify all keys can be searched
        for (int i = 0; i < N; i++) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "key_%04d", i);
            assert(tree.search(StrKey(buf)) == true);
        }
    }

    std::cout << "\n[Step 2] Reloading multi-level tree from disk...\n";
    {
        BPlusTree<StrKey> reloadedTree(treeName);
        std::cout << "Reloaded tree structure:\n";
        reloadedTree.printTree();

        // Verify all keys persisted
        for (int i = 0; i < N; i++) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "key_%04d", i);
            assert(reloadedTree.search(StrKey(buf)) == true);
        }
    }

    std::cout << ">>> TEST 4 PASSED! <<<\n";
}

struct StudentPayload {
    FixedString<32> name;
    float gpa;
    int age;

    bool operator==(const StudentPayload& o) const {
        return name == o.name && std::abs(gpa - o.gpa) < 0.001f && age == o.age;
    }
};

void testKeyValueCustomStruct() {
    std::cout << "\n=========================================\n";
    std::cout << "TEST 5: Key-Value B+ Tree with Custom Struct\n";
    std::cout << "=========================================\n";

    const std::string treeName = "test_kv_students";
    std::filesystem::remove_all("data/" + treeName);

    std::cout << "[Step 1] Creating a Key-Value B+ tree (Key: int ID, Value: StudentPayload)...\n";
    {
        BPlusTree<int, StudentPayload> tree(treeName);

        tree.insert(101, {"Alice Smith", 3.95f, 20});
        tree.insert(105, {"Bob Jones",   3.40f, 22});
        tree.insert(102, {"Charlie Brown", 3.80f, 21});
        tree.insert(108, {"Diana Prince", 4.00f, 19});

        std::cout << "Tree after inserting students:\n";
        tree.printTree();

        // Search and verify payload retrieval
        StudentPayload student{};
        bool found = tree.search(102, student);
        assert(found == true);
        assert(student.name == "Charlie Brown");
        assert(student.age == 21);
        std::cout << "Successfully retrieved student 102: " << student.name << ", GPA: " << student.gpa << ", Age: " << student.age << "\n";

        found = tree.search(999, student);
        assert(found == false);

        std::cout << "Deleting student 105...\n";
        tree.deleteNode(105);
        assert(tree.search(105, student) == false);
        assert(tree.search(101, student) == true);
    }

    std::cout << "\n[Step 2] Reloading Key-Value tree from disk...\n";
    {
        BPlusTree<int, StudentPayload> reloadedTree(treeName);

        StudentPayload s101{}, s102{}, s108{}, s105{};
        assert(reloadedTree.search(101, s101) == true);
        assert(s101.name == "Alice Smith");
        assert(s101.age == 20);

        assert(reloadedTree.search(108, s108) == true);
        assert(s108.name == "Diana Prince");
        assert(s108.age == 19);

        assert(reloadedTree.search(105, s105) == false);

        std::cout << "Reloaded student 108: " << s108.name << " (GPA: " << s108.gpa << ")\n";
    }

    std::cout << ">>> TEST 5 PASSED! <<<\n";
}

int main() {
    std::cout << "Running Persistent B+ Tree Storage System Tests...\n";
    testIntTreePersistence();
    testStringTree();
    testTypeMismatchSafety();
    testMultiLevelSplits();
    testKeyValueCustomStruct();
    std::cout << "\nALL TESTS PASSED SUCCESSFULLY! YAY\n";
    return 0;
}