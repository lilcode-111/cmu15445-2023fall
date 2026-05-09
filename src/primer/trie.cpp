#include "primer/trie.h"
#include <string_view>
#include "common/exception.h"

namespace bustub {

template <class T>
auto PutHelper(std::shared_ptr<const TrieNode> node, std::string_view key, size_t index,
               const std::shared_ptr<T> &value) -> std::shared_ptr<const TrieNode> {
  std::map<char, std::shared_ptr<const TrieNode>> children;
  if (node != nullptr) {
    children = node->children_;
  }
  if (index == key.size()) {
    return std::make_shared<TrieNodeWithValue<T>>(std::move(children), value);  // 问题1
  }
  char c = key[index];
  std::shared_ptr<const TrieNode> old_child = nullptr;
  auto it = children.find(c);
  if (it != children.end()) {
    old_child = it->second;
  }

  auto new_child = PutHelper(old_child, key, index + 1, value);

  children[c] = new_child;
  if (node != nullptr) {  // 这个会发生吗
    auto cloned = node->Clone();
    cloned->children_ = std::move(children);
    return std::shared_ptr<const TrieNode>(std::move((cloned)));
  }

  return std::make_shared<const TrieNode>(std::move((children)));
}

auto RemoveHelper(std::shared_ptr<const TrieNode> node, std::string_view key,
                  size_t index) -> std::pair<std::shared_ptr<const TrieNode>, bool> {
  if (node == nullptr) {
    return {nullptr, false};
  }

  if (index == key.size()) {
    if (!node->is_value_node_) {
      return {node, false};
    }

    if (node->children_.empty()) {
      return {nullptr, true};
    }

    return {std::make_shared<TrieNode>(node->children_), true};
  }

  char c = key[index];
  auto it = node->children_.find(c);
  if (it == node->children_.end()) {
    return {node, false};
  }

  auto [new_child, removed] = RemoveHelper(it->second, key, index + 1);

  if (!removed) {
    return {node, false};
  }

  auto cloned = node->Clone();

  if (new_child == nullptr) {
    cloned->children_.erase(c);
  } else {
    cloned->children_[c] = new_child;
  }

  if (!cloned->is_value_node_ && cloned->children_.empty()) {
    return {nullptr, true};
  }

  return {std::shared_ptr<const TrieNode>(std::move(cloned)), true};
}

template <class T>
auto Trie::Get(std::string_view key) const -> const T * {
  auto node = root_;
  for (auto it : key) {
    if (node == nullptr) {
      return nullptr;
    }
    if (node->children_.find(it) != node->children_.end()) {
      node = node->children_.at(it);
    } else {
      return nullptr;
    }
  }

  if (node == nullptr) {
    return nullptr;
  }
  auto value_node_ = dynamic_cast<const TrieNodeWithValue<T> *>(node.get());
  if (value_node_ == nullptr) {
    return nullptr;
  }
  return value_node_->value_.get();
}

template <class T>
auto Trie::Put(std::string_view key, T value) const -> Trie {
  auto value_ptr = std::make_shared<T>(std::move(value));
  auto new_root = PutHelper<T>(root_, key, 0, value_ptr);
  return Trie(new_root);
}

auto Trie::Remove(std::string_view key) const -> Trie {
  auto [new_root, removed] = RemoveHelper(root_, key, 0);
  if (!removed) {
    return *this;
  }
  return Trie(new_root);
}

// Below are explicit instantiation of template functions.
//
// Generally people would write the implementation of template classes and functions in the header file. However, we
// separate the implementation into a .cpp file to make things clearer. In order to make the compiler know the
// implementation of the template functions, we need to explicitly instantiate them here, so that they can be picked up
// by the linker.

template auto Trie::Put(std::string_view key, uint32_t value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const uint32_t *;

template auto Trie::Put(std::string_view key, uint64_t value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const uint64_t *;

template auto Trie::Put(std::string_view key, std::string value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const std::string *;

// If your solution cannot compile for non-copy tests, you can remove the below lines to get partial score.

using Integer = std::unique_ptr<uint32_t>;

template auto Trie::Put(std::string_view key, Integer value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const Integer *;

template auto Trie::Put(std::string_view key, MoveBlocked value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const MoveBlocked *;

}  // namespace bustub
