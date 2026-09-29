#include "b_plus_tree.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

// ---------- Config for the "bob" tree ----------
static const std::string BOB_NAME = "bob";
static const int BOB_N =
    20000; // raise this if printTree shows you still only have 2 levels

// Deterministic rule for which keys get deleted, shared by both phases
// so the verify phase knows exactly what to expect.
static bool bobIsDeleted(int k) {
  if (k % 5 == 0)
    return true; // every 5th key (scattered deletes)
  if (k >= 1000 && k < 1600)
    return true; // a contiguous block (forces underflow / merge / redistribute)
  return false;
}

// Simple check helper (doesn't vanish under NDEBUG like assert does, and
// reports all failures)
static int g_failures = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      if (g_failures <= 20)                                                    \
        std::cout << "  [FAIL] " << msg << "\n";                               \
    }                                                                          \
  } while (0)

// ---------- PHASE 1: build the tree, then stop ----------
void bobInsertPhase() {
  std::cout << "\n=========================================\n";
  std::cout << "BOB PHASE 1: Insert lots of data + delete some\n";
  std::cout << "=========================================\n";

  // Start clean every time phase 1 runs
  std::filesystem::remove_all("data/" + BOB_NAME);

  // Insert in shuffled (but reproducible) order so splits happen all over the
  // tree
  std::vector<int> keys(BOB_N);
  std::iota(keys.begin(), keys.end(), 0);
  std::mt19937 rng(42);
  std::shuffle(keys.begin(), keys.end(), rng);

  {
    BPlusTree<int> tree(BOB_NAME);

    std::cout << "Inserting " << BOB_N << " keys...\n";
    for (int k : keys)
      tree.insert(k);

    std::cout << "Deleting keys (k % 5 == 0, and block [1000,1600))...\n";
    int deleted = 0;
    for (int k = 0; k < BOB_N; k++) {
      if (bobIsDeleted(k)) {
        tree.deleteNode(k);
        deleted++;
      }
    }
    std::cout << "Deleted " << deleted << " keys, " << (BOB_N - deleted)
              << " should remain.\n";

    // Quick in-memory sanity check before we persist
    g_failures = 0;
    for (int k = 0; k < BOB_N; k++) {
      CHECK(tree.search(k) == !bobIsDeleted(k),
            "pre-close: key " << k << " expected " << !bobIsDeleted(k));
    }
    std::cout << (g_failures == 0 ? "In-memory check OK.\n"
                                  : "In-memory check FAILED (see above).\n");

    // Uncomment if you want to eyeball the structure (very long output at this
    // N): tree.printTree();

    std::cout << "Closing tree (destructor flushes to disk)...\n";
  }

  std::cout
      << ">>> PHASE 1 DONE. Now run the verify phase in a NEW process. <<<\n";
}

// ---------- PHASE 2: run in a fresh process after phase 1 ----------
void bobVerifyPhase() {
  std::cout << "\n=========================================\n";
  std::cout << "BOB PHASE 2: Reload from disk and verify\n";
  std::cout << "=========================================\n";

  // NOTE: no remove_all here, we want the data phase 1 left behind.
  if (!std::filesystem::exists("data/" + BOB_NAME)) {
    std::cout << "data/" << BOB_NAME
              << " not found. Run the insert phase first.\n";
    return;
  }

  g_failures = 0;
  {
    BPlusTree<int> tree(BOB_NAME);

    std::cout << "Checking every key in [0, " << BOB_N << ")...\n";
    int present = 0, absent = 0;
    for (int k = 0; k < BOB_N; k++) {
      bool expected = !bobIsDeleted(k);
      bool actual = tree.search(k);
      CHECK(actual == expected,
            "key " << k << " expected " << (expected ? "present" : "absent")
                   << " but got " << (actual ? "present" : "absent"));
      actual ? present++ : absent++;
    }
    std::cout << "  present: " << present << ", absent: " << absent << "\n";

    std::cout << "Checking keys that were never inserted...\n";
    CHECK(tree.search(-1) == false, "key -1 should not exist");
    CHECK(tree.search(BOB_N) == false, "key BOB_N should not exist");
    CHECK(tree.search(BOB_N + 12345) == false,
          "key far out of range should not exist");

    std::cout
        << "Range searches (output printed by the tree; eyeball these):\n";
    std::cout << " [990, 1010]:\n";
    tree.search(990, 1010); // straddles the deleted block start
    std::cout << " [1595, 1610]:\n";
    tree.search(1595, 1610); // straddles the deleted block end

    std::cout << "Re-inserting the deleted block [1000,1600) to confirm the "
                 "tree is still usable...\n";
    for (int k = 1000; k < 1600; k++)
      tree.insert(k);
    for (int k = 1000; k < 1600; k++) {
      CHECK(tree.search(k) == true,
            "re-inserted key " << k << " should be present");
    }

    std::cout << "Deleting a few more survivors and re-checking...\n";
    int extra[] = {1, 2, 3, 19999, 10001};
    for (int k : extra)
      tree.deleteNode(k);
    for (int k : extra)
      CHECK(tree.search(k) == false,
            "extra-deleted key " << k << " should be gone");
    CHECK(tree.search(4) == true, "key 4 should still exist");
  }

  if (g_failures == 0)
    std::cout << ">>> PHASE 2 PASSED! <<<\n";
  else
    std::cout << ">>> PHASE 2 FAILED with " << g_failures << " failures <<<\n";
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " insert | verify\n";
    return 1;
  }
  std::string mode = argv[1];
  if (mode == "insert")
    bobInsertPhase();
  else if (mode == "verify")
    bobVerifyPhase();
  else {
    std::cout << "Unknown mode: " << mode << "\n";
    return 1;
  }
  return 0;
}