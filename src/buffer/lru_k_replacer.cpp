//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include "common/config.h"
#include "common/exception.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);
  if (curr_size_ == 0) {
    return false;
  }
  frame_id_t victim_id = -1;
  bool found = false;
  size_t best_timestamp = std::numeric_limits<size_t>::max();
  bool found_inf = false;

  for (const auto &it : node_store_) {
    const auto &node = it.second;
    if (!node.is_evictable_) {
      continue;
    }
    size_t timestamp = node.history_.front();
    if (node.history_.size() < k_) {
      if (!found) {
        victim_id = it.first;
        found = true;
        best_timestamp = timestamp;
        found_inf = true;
      } else if (!found_inf) {
        victim_id = it.first;
        best_timestamp = timestamp;
        found_inf = true;
      } else if (timestamp < best_timestamp) {
        victim_id = it.first;
        best_timestamp = timestamp;
      }
    } else {
      if (found_inf) {
        continue;
      }
      if (!found) {
        victim_id = it.first;
        best_timestamp = timestamp;
        found = true;
      } else if (timestamp < best_timestamp) {
        victim_id = it.first;
        best_timestamp = timestamp;
      }
    }
  }
  if (!found) {
    return false;
  }
  *frame_id = victim_id;
  node_store_.erase(*frame_id);
  curr_size_--;
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  if (static_cast<size_t>(frame_id) >= replacer_size_) {
    throw Exception(ExceptionType::OUT_OF_RANGE, "frame id is out of range");
  }
  std::lock_guard<std::mutex> lock(latch_);
  current_timestamp_++;
  auto it = node_store_.find(frame_id);
  if (it == node_store_.end()) {
    LRUKNode new_node;
    new_node.fid_ = frame_id;
    new_node.k_ = k_;
    new_node.is_evictable_ = false;
    new_node.history_.push_back(current_timestamp_);
    node_store_.emplace(frame_id, std::move(new_node));
  } else {
    auto &node = it->second;
    node.history_.push_back(current_timestamp_);
    if (node.history_.size() > k_) {
      node.history_.pop_front();
    }
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  if (static_cast<size_t>(frame_id) >= replacer_size_) {
    throw Exception(ExceptionType::OUT_OF_RANGE, "frame id is out of range");
  }
  std::lock_guard<std::mutex> lock(latch_);
  auto it = node_store_.find(frame_id);
  if (it == node_store_.end()) {
    return;
  }
  auto &node = it->second;
  if (set_evictable && !node.is_evictable_) {
    curr_size_++;
  } else if (!set_evictable && node.is_evictable_) {
    curr_size_--;
  }
  node.is_evictable_ = set_evictable;
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  if (static_cast<size_t>(frame_id) >= replacer_size_) {
    throw Exception(ExceptionType::OUT_OF_RANGE, "frame id is out of range");
  }
  std::lock_guard<std::mutex> lock(latch_);
  auto it = node_store_.find(frame_id);
  if (it == node_store_.end() || !it->second.is_evictable_) {
    return;
  }
  node_store_.erase(frame_id);
  curr_size_--;
}

auto LRUKReplacer::Size() -> size_t {
  std::lock_guard<std::mutex> lock(latch_);
  return curr_size_;
}

}  // namespace bustub
