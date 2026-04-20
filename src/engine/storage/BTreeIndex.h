#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

/**
 * In-memory B-tree index mapping string keys to lists of file offsets.
 *
 * Minimum degree T = 4:
 *   - Every non-root node holds [T-1, 2T-1] keys.
 *   - Root holds [1, 2T-1] keys (or 0 when tree is empty).
 *   - Internal node with k keys has k+1 children.
 *
 * Multiple offsets per key are supported (non-unique index); for a unique
 * index the caller should check before inserting.
 */
class BTreeIndex {
public:
    static const int T = 4;   // minimum degree

    BTreeIndex();
    ~BTreeIndex();

    BTreeIndex(const BTreeIndex&) = delete;
    BTreeIndex& operator=(const BTreeIndex&) = delete;

    BTreeIndex(BTreeIndex&&) noexcept;
    BTreeIndex& operator=(BTreeIndex&&) noexcept;

    /**
     * Insert offset under key.
     * @param unique  if true and key already exists, throws std::runtime_error.
     */
    void insert(const std::string& key, int64_t offset, bool unique = false);

    /**
     * Remove a specific offset from the key's list.
     * If the list becomes empty the key is deleted from the tree.
     */
    void remove(const std::string& key, int64_t offset);

    /** Return all offsets for key (empty vector if key absent). */
    std::vector<int64_t> search(const std::string& key) const;

    /**
     * Visit all (key, offsets) pairs in ascending key order.
     * Used for serialisation.
     */
    void inorder(std::function<void(const std::string&,
                                    const std::vector<int64_t>&)> visitor) const;

    /** Remove all keys and offsets. */
    void clear();

    /** True if the tree holds no keys. */
    bool empty() const;

private:
    struct Node {
        bool leaf = true;
        std::vector<std::string>          keys;
        std::vector<std::vector<int64_t>> vals;   // parallel to keys
        std::vector<std::unique_ptr<Node>> children;

        explicit Node(bool isLeaf) : leaf(isLeaf) {}
    };

    std::unique_ptr<Node> root_;

    // ── insertion helpers ────────────────────────────────────────────
    void splitChild(Node* parent, int idx);
    void insertNonFull(Node* node, const std::string& key,
                       int64_t offset, bool unique);

    // ── deletion helpers ─────────────────────────────────────────────
    std::vector<int64_t>* findVal(Node* node, const std::string& key);
    void deleteKey(Node* node, const std::string& key);
    void fill(Node* node, int idx);
    void borrowFromPrev(Node* node, int idx);
    void borrowFromNext(Node* node, int idx);
    void merge(Node* node, int idx);
    std::pair<std::string, std::vector<int64_t>> predecessor(const Node* node, int idx);
    std::pair<std::string, std::vector<int64_t>> successor  (const Node* node, int idx);

    // ── traversal ────────────────────────────────────────────────────
    void doInorder(const Node* node,
                   std::function<void(const std::string&,
                                      const std::vector<int64_t>&)>& visitor) const;
};
