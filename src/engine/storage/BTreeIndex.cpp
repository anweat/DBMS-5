#include "BTreeIndex.h"
#include <algorithm>
#include <stdexcept>

// ============================================================
// Construction / Move
// ============================================================

BTreeIndex::BTreeIndex() : root_(std::make_unique<Node>(true)) {}
BTreeIndex::~BTreeIndex() = default;

BTreeIndex::BTreeIndex(BTreeIndex&&) noexcept = default;
BTreeIndex& BTreeIndex::operator=(BTreeIndex&&) noexcept = default;

// ============================================================
// Public API
// ============================================================

bool BTreeIndex::empty() const { return root_->keys.empty(); }

void BTreeIndex::clear() { root_ = std::make_unique<Node>(true); }

std::vector<int64_t> BTreeIndex::search(const std::string& key) const {
    const auto* v = const_cast<BTreeIndex*>(this)->findVal(root_.get(), key);
    return v ? *v : std::vector<int64_t>{};
}

void BTreeIndex::inorder(
    std::function<void(const std::string&, const std::vector<int64_t>&)> visitor) const
{
    doInorder(root_.get(), visitor);
}

// ============================================================
// Insert
// ============================================================

void BTreeIndex::insert(const std::string& key, int64_t offset, bool unique) {
    Node* r = root_.get();
    if ((int)r->keys.size() == 2 * T - 1) {
        // Root is full → create new root and split old root
        auto newRoot = std::make_unique<Node>(false);
        newRoot->children.push_back(std::move(root_));
        root_ = std::move(newRoot);
        splitChild(root_.get(), 0);
        insertNonFull(root_.get(), key, offset, unique);
    } else {
        insertNonFull(r, key, offset, unique);
    }
}

void BTreeIndex::splitChild(Node* parent, int idx) {
    Node* y = parent->children[idx].get();
    auto  z = std::make_unique<Node>(y->leaf);

    // z gets the right T-1 keys/vals of y
    z->keys.assign(y->keys.begin() + T, y->keys.end());
    z->vals.assign(y->vals.begin() + T, y->vals.end());
    if (!y->leaf) {
        // z gets the right T children of y
        for (int j = 0; j < T; ++j)
            z->children.push_back(std::move(y->children[T + j]));
        y->children.resize(T);
    }

    // Median key moves up into parent
    std::string         medKey = y->keys[T - 1];
    std::vector<int64_t> medVal = std::move(y->vals[T - 1]);

    y->keys.resize(T - 1);
    y->vals.resize(T - 1);

    parent->keys.insert(parent->keys.begin() + idx, std::move(medKey));
    parent->vals.insert(parent->vals.begin() + idx, std::move(medVal));
    parent->children.insert(parent->children.begin() + idx + 1, std::move(z));
}

void BTreeIndex::insertNonFull(Node* x, const std::string& key,
                                int64_t offset, bool unique) {
    int i = static_cast<int>(
        std::lower_bound(x->keys.begin(), x->keys.end(), key) - x->keys.begin());

    if (i < (int)x->keys.size() && x->keys[i] == key) {
        // Key already present → add offset (unless unique constraint)
        if (unique)
            throw std::runtime_error("Duplicate key '" + key + "' in unique index");
        x->vals[i].push_back(offset);
        return;
    }

    if (x->leaf) {
        x->keys.insert(x->keys.begin() + i, key);
        x->vals.insert(x->vals.begin() + i, {offset});
    } else {
        if ((int)x->children[i]->keys.size() == 2 * T - 1) {
            splitChild(x, i);
            if (key == x->keys[i]) {
                if (unique)
                    throw std::runtime_error("Duplicate key '" + key + "' in unique index");
                x->vals[i].push_back(offset);
                return;
            }
            if (key > x->keys[i]) ++i;
        }
        insertNonFull(x->children[i].get(), key, offset, unique);
    }
}

// ============================================================
// Remove
// ============================================================

void BTreeIndex::remove(const std::string& key, int64_t offset) {
    if (!root_) return;

    auto* vals = findVal(root_.get(), key);
    if (!vals) return;

    auto& offsets = *vals;
    offsets.erase(std::remove(offsets.begin(), offsets.end(), offset), offsets.end());

    if (!offsets.empty()) return; // key still has other offsets

    // All offsets gone → delete key from tree
    deleteKey(root_.get(), key);

    // Shrink tree root if it became empty and is not a leaf
    if (root_->keys.empty() && !root_->leaf)
        root_ = std::move(root_->children[0]);
}

// ── find node owning key ─────────────────────────────────────────────────────

std::vector<int64_t>* BTreeIndex::findVal(Node* x, const std::string& key) {
    int i = static_cast<int>(
        std::lower_bound(x->keys.begin(), x->keys.end(), key) - x->keys.begin());
    if (i < (int)x->keys.size() && x->keys[i] == key)
        return &x->vals[i];
    if (x->leaf) return nullptr;
    return findVal(x->children[i].get(), key);
}

// ── predecessor / successor ──────────────────────────────────────────────────

std::pair<std::string, std::vector<int64_t>>
BTreeIndex::predecessor(const Node* x, int idx) {
    const Node* cur = x->children[idx].get();
    while (!cur->leaf) cur = cur->children.back().get();
    return {cur->keys.back(), cur->vals.back()};
}

std::pair<std::string, std::vector<int64_t>>
BTreeIndex::successor(const Node* x, int idx) {
    const Node* cur = x->children[idx + 1].get();
    while (!cur->leaf) cur = cur->children.front().get();
    return {cur->keys.front(), cur->vals.front()};
}

// ── rebalancing helpers ──────────────────────────────────────────────────────

void BTreeIndex::borrowFromPrev(Node* x, int idx) {
    Node* child   = x->children[idx].get();
    Node* sibling = x->children[idx - 1].get();

    child->keys.insert(child->keys.begin(), x->keys[idx - 1]);
    child->vals.insert(child->vals.begin(), x->vals[idx - 1]);

    x->keys[idx - 1] = sibling->keys.back();
    x->vals[idx - 1] = std::move(sibling->vals.back());
    sibling->keys.pop_back();
    sibling->vals.pop_back();

    if (!sibling->leaf) {
        child->children.insert(child->children.begin(),
                                std::move(sibling->children.back()));
        sibling->children.pop_back();
    }
}

void BTreeIndex::borrowFromNext(Node* x, int idx) {
    Node* child   = x->children[idx].get();
    Node* sibling = x->children[idx + 1].get();

    child->keys.push_back(x->keys[idx]);
    child->vals.push_back(x->vals[idx]);

    x->keys[idx] = sibling->keys.front();
    x->vals[idx] = std::move(sibling->vals.front());
    sibling->keys.erase(sibling->keys.begin());
    sibling->vals.erase(sibling->vals.begin());

    if (!sibling->leaf) {
        child->children.push_back(std::move(sibling->children.front()));
        sibling->children.erase(sibling->children.begin());
    }
}

void BTreeIndex::merge(Node* x, int idx) {
    Node* left  = x->children[idx].get();
    Node* right = x->children[idx + 1].get();

    // Pull separator key+val from parent into left
    left->keys.push_back(x->keys[idx]);
    left->vals.push_back(std::move(x->vals[idx]));

    // Append right's keys/vals/children into left
    for (auto& k : right->keys) left->keys.push_back(std::move(k));
    for (auto& v : right->vals) left->vals.push_back(std::move(v));
    if (!right->leaf)
        for (auto& c : right->children) left->children.push_back(std::move(c));

    // Remove separator from parent
    x->keys.erase(x->keys.begin() + idx);
    x->vals.erase(x->vals.begin() + idx);
    x->children.erase(x->children.begin() + idx + 1);
}

void BTreeIndex::fill(Node* x, int idx) {
    if (idx > 0 && (int)x->children[idx - 1]->keys.size() >= T)
        borrowFromPrev(x, idx);
    else if (idx < (int)x->children.size() - 1
             && (int)x->children[idx + 1]->keys.size() >= T)
        borrowFromNext(x, idx);
    else {
        if (idx < (int)x->children.size() - 1)
            merge(x, idx);
        else
            merge(x, idx - 1);
    }
}

// ── core delete ──────────────────────────────────────────────────────────────

void BTreeIndex::deleteKey(Node* x, const std::string& key) {
    int i = static_cast<int>(
        std::lower_bound(x->keys.begin(), x->keys.end(), key) - x->keys.begin());

    if (i < (int)x->keys.size() && x->keys[i] == key) {
        // Case 1 / 2: key is in this node
        if (x->leaf) {
            x->keys.erase(x->keys.begin() + i);
            x->vals.erase(x->vals.begin() + i);
        } else {
            if ((int)x->children[i]->keys.size() >= T) {
                // Case 2a: left child has >= T keys → replace with predecessor
                auto [pk, pv] = predecessor(x, i);
                x->keys[i] = pk;
                x->vals[i] = std::move(pv);
                deleteKey(x->children[i].get(), pk);
            } else if ((int)x->children[i + 1]->keys.size() >= T) {
                // Case 2b: right child has >= T keys → replace with successor
                auto [sk, sv] = successor(x, i);
                x->keys[i] = sk;
                x->vals[i] = std::move(sv);
                deleteKey(x->children[i + 1].get(), sk);
            } else {
                // Case 2c: both children have T-1 keys → merge
                merge(x, i);
                deleteKey(x->children[i].get(), key);
            }
        }
    } else {
        // Case 3: key is in a subtree
        if (x->leaf) return; // not found

        bool isLast = (i == (int)x->keys.size());
        if ((int)x->children[i]->keys.size() < T) {
            fill(x, i);
            // If we merged and the child was the last one, idx shifted left
            if (isLast && i > (int)x->keys.size()) --i;
        }
        deleteKey(x->children[i].get(), key);
    }
}

// ============================================================
// Traversal
// ============================================================

void BTreeIndex::doInorder(
    const Node* node,
    std::function<void(const std::string&, const std::vector<int64_t>&)>& visitor) const
{
    if (!node) return;
    for (int i = 0; i < (int)node->keys.size(); ++i) {
        if (!node->leaf)
            doInorder(node->children[i].get(), visitor);
        visitor(node->keys[i], node->vals[i]);
    }
    if (!node->leaf)
        doInorder(node->children.back().get(), visitor);
}
