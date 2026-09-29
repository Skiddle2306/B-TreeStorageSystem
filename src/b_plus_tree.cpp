#include "b_plus_tree.h"

// ─────────────────────────────────────────────────────────────────────────────
// Explicit template instantiations for common types
// ─────────────────────────────────────────────────────────────────────────────
template class BPlusTree<int, char>;
template class BPlusTree<float, char>;
template class BPlusTree<double, char>;
template class BPlusTree<char, char>;
template class BPlusTree<std::string, char>;
template class BPlusTree<FixedString<256>, char>;
template class BPlusTree<FixedString<128>, char>;
template class BPlusTree<FixedString<64>, char>;
template class BPlusTree<FixedString<32>, char>;
template class BPlusTree<FixedString<16>, char>;