//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_extendible_hash_table.cpp
//
// Identification: src/container/disk/hash/disk_extendible_hash_table.cpp
//
// Copyright (c) 2015-2023, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "common/config.h"
#include "common/exception.h"
#include "common/logger.h"
#include "common/macros.h"
#include "common/rid.h"
#include "common/util/hash_util.h"
#include "container/disk/hash/disk_extendible_hash_table.h"
#include "storage/index/hash_comparator.h"
#include "storage/page/extendible_htable_bucket_page.h"
#include "storage/page/extendible_htable_directory_page.h"
#include "storage/page/extendible_htable_header_page.h"
#include "storage/page/hash_table_directory_page.h"
#include "storage/page/page_guard.h"
#include "type/value.h"

namespace bustub {

template <typename K, typename V, typename KC>
DiskExtendibleHashTable<K, V, KC>::DiskExtendibleHashTable(std::string name, BufferPoolManager *bpm, const KC &cmp,
                                                           const HashFunction<K> &hash_fn, uint32_t header_max_depth,
                                                           uint32_t directory_max_depth, uint32_t bucket_max_size)
    : index_name_(std::move(name)),
      bpm_(bpm),
      cmp_(cmp),
      hash_fn_(std::move(hash_fn)),
      header_max_depth_(header_max_depth),
      directory_max_depth_(directory_max_depth),
      bucket_max_size_(bucket_max_size) {
  auto header_guard = bpm_->NewPageGuarded(&header_page_id_);
  auto header = header_guard.AsMut<ExtendibleHTableHeaderPage>();
  header->Init(header_max_depth);
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
template <typename K, typename V, typename KC>
auto DiskExtendibleHashTable<K, V, KC>::GetValue(const K &key, std::vector<V> *result, Transaction *transaction) const
    -> bool {
  uint32_t hash = Hash(key);
  auto header_guard = bpm_->FetchPageRead(header_page_id_);
  auto header = header_guard.As<ExtendibleHTableHeaderPage>();
  uint32_t directory_idx = header->HashToDirectoryIndex(hash);
  page_id_t directory_page_id = header->GetDirectoryPageId(directory_idx);
  if (directory_page_id == INVALID_PAGE_ID) {
    return false;
  }

  auto directory_guard = bpm_->FetchPageRead(directory_page_id);
  auto directory = directory_guard.As<ExtendibleHTableDirectoryPage>();
  uint32_t bucket_idx = directory->HashToBucketIndex(hash);
  page_id_t bucket_page_id = directory->GetBucketPageId(bucket_idx);
  if (bucket_page_id == INVALID_PAGE_ID) {
    return false;
  }

  auto bucket_guard = bpm_->FetchPageRead(bucket_page_id);
  auto bucket = bucket_guard.As<ExtendibleHTableBucketPage<K, V, KC>>();
  V value;
  if (bucket->Lookup(key, value, cmp_)) {
    result->push_back(value);
    return true;
  }
  return false;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/

template <typename K, typename V, typename KC>
auto DiskExtendibleHashTable<K, V, KC>::Insert(const K &key, const V &value, Transaction *transaction) -> bool {
  auto header_guard = bpm_->FetchPageWrite(header_page_id_);
  auto header_page = header_guard.AsMut<ExtendibleHTableHeaderPage>();
  uint32_t hash = Hash(key);
  auto directory_idx = header_page->HashToDirectoryIndex(hash);
  page_id_t directory_page_id = header_page->GetDirectoryPageId(directory_idx);
  if (directory_page_id == INVALID_PAGE_ID) {
    return InsertToNewDirectory(header_page, directory_idx, hash, key, value);
  }

  auto directory_guard = bpm_->FetchPageWrite(directory_page_id);
  auto directory_page = directory_guard.AsMut<ExtendibleHTableDirectoryPage>();

  while (true) {
    auto bucket_idx = directory_page->HashToBucketIndex(hash);
    page_id_t bucket_page_id = directory_page->GetBucketPageId(bucket_idx);
    if (bucket_page_id == INVALID_PAGE_ID) {
      return InsertToNewBucket(directory_page, bucket_idx, key, value);
    }

    auto bucket_guard = bpm_->FetchPageWrite(bucket_page_id);
    auto bucket_page = bucket_guard.AsMut<ExtendibleHTableBucketPage<K, V, KC>>();

    V old_value;
    if (bucket_page->Lookup(key, old_value, cmp_)) {
      return false;
    }

    if (!bucket_page->IsFull()) {
      return bucket_page->Insert(key, value, cmp_);
    }

    uint32_t old_local_depth = directory_page->GetLocalDepth(bucket_idx);
    if (old_local_depth == directory_max_depth_) {
      return false;
    }

    if (old_local_depth == directory_page->GetGlobalDepth()) {
      directory_page->IncrGlobalDepth();
    }

    uint32_t new_local_depth = old_local_depth + 1;
    uint32_t local_depth_mask = (1U << new_local_depth) - 1;
    uint32_t old_bucket_idx = bucket_idx;
    uint32_t new_bucket_idx = bucket_idx ^ (1U << old_local_depth);

    page_id_t new_bucket_page_id;
    auto new_bucket_page_guard = bpm_->NewPageGuarded(&new_bucket_page_id);
    auto new_bucket_page = new_bucket_page_guard.AsMut<ExtendibleHTableBucketPage<K, V, KC>>();
    new_bucket_page->Init(bucket_max_size_);

    UpdateDirectoryMapping(directory_page, old_bucket_idx, bucket_page_id, new_local_depth, local_depth_mask);
    UpdateDirectoryMapping(directory_page, new_bucket_idx, new_bucket_page_id, new_local_depth, local_depth_mask);

    std::vector<std::pair<K, V>> entries;
    for (uint32_t i = 0; i < bucket_page->Size(); ++i) {
      entries.push_back(bucket_page->EntryAt(i));
    }

    while (bucket_page->Size() > 0) {
      bucket_page->RemoveAt(bucket_page->Size() - 1);
    }

    for (const auto &entry : entries) {
      uint32_t entry_hash = Hash(entry.first);
      uint32_t entry_bucket_idx = directory_page->HashToBucketIndex(entry_hash);
      page_id_t target_bucket_page_id = directory_page->GetBucketPageId(entry_bucket_idx);

      if (target_bucket_page_id == bucket_page_id) {
        bucket_page->Insert(entry.first, entry.second, cmp_);
      } else {
        new_bucket_page->Insert(entry.first, entry.second, cmp_);
      }
    }
  }
}

template <typename K, typename V, typename KC>
auto DiskExtendibleHashTable<K, V, KC>::InsertToNewDirectory(ExtendibleHTableHeaderPage *header, uint32_t directory_idx,
                                                             uint32_t hash, const K &key, const V &value) -> bool {
  page_id_t new_directory_page_id;
  auto new_directory_page_guard = bpm_->NewPageGuarded(&new_directory_page_id);
  auto new_directory_page = new_directory_page_guard.AsMut<ExtendibleHTableDirectoryPage>();
  new_directory_page->Init(directory_max_depth_);

  header->SetDirectoryPageId(directory_idx, new_directory_page_id);
  uint32_t new_bucket_idx = new_directory_page->HashToBucketIndex(hash);
  return InsertToNewBucket(new_directory_page, new_bucket_idx, key, value);
}

template <typename K, typename V, typename KC>
auto DiskExtendibleHashTable<K, V, KC>::InsertToNewBucket(ExtendibleHTableDirectoryPage *directory, uint32_t bucket_idx,
                                                          const K &key, const V &value) -> bool {
  page_id_t new_bucket_page_id;
  auto new_bucket_page_guard = bpm_->NewPageGuarded(&new_bucket_page_id);
  auto new_bucket_page = new_bucket_page_guard.AsMut<ExtendibleHTableBucketPage<K, V, KC>>();
  new_bucket_page->Init(bucket_max_size_);

  directory->SetBucketPageId(bucket_idx, new_bucket_page_id);
  directory->SetLocalDepth(bucket_idx, directory->GetGlobalDepth());
  return new_bucket_page->Insert(key, value, cmp_);
}

template <typename K, typename V, typename KC>
void DiskExtendibleHashTable<K, V, KC>::UpdateDirectoryMapping(ExtendibleHTableDirectoryPage *directory,
                                                               uint32_t new_bucket_idx, page_id_t new_bucket_page_id,
                                                               uint32_t new_local_depth, uint32_t local_depth_mask) {
  for (uint32_t i = 0; i < directory->Size(); ++i) {
    if ((i & local_depth_mask) == (new_bucket_idx & local_depth_mask)) {
      directory->SetLocalDepth(i, new_local_depth);
      directory->SetBucketPageId(i, new_bucket_page_id);
    }
  }
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
template <typename K, typename V, typename KC>
auto DiskExtendibleHashTable<K, V, KC>::Remove(const K &key, Transaction *transaction) -> bool {
  auto header_guard = bpm_->FetchPageWrite(header_page_id_);
  auto header_page = header_guard.AsMut<ExtendibleHTableHeaderPage>();
  uint32_t hash = Hash(key);
  uint32_t directory_idx = header_page->HashToDirectoryIndex(hash);
  page_id_t directory_page_id = header_page->GetDirectoryPageId(directory_idx);
  if (directory_page_id == INVALID_PAGE_ID) {
    return false;
  }

  auto directory_guard = bpm_->FetchPageWrite(directory_page_id);
  auto directory_page = directory_guard.AsMut<ExtendibleHTableDirectoryPage>();
  uint32_t bucket_idx = directory_page->HashToBucketIndex(hash);
  page_id_t bucket_page_id = directory_page->GetBucketPageId(bucket_idx);
  if (bucket_page_id == INVALID_PAGE_ID) {
    return false;
  }

  {
    auto bucket_guard = bpm_->FetchPageWrite(bucket_page_id);
    auto bucket_page = bucket_guard.AsMut<ExtendibleHTableBucketPage<K, V, KC>>();

    if (!bucket_page->Remove(key, cmp_)) {
      return false;
    }

    if (!bucket_page->IsEmpty()) {
      return true;
    }
  }

  while (true) {
    uint32_t local_depth = directory_page->GetLocalDepth(bucket_idx);
    if (local_depth == 0) {
      break;
    }
    auto bucket_guard = bpm_->FetchPageWrite(bucket_page_id);
    auto bucket_page = bucket_guard.AsMut<ExtendibleHTableBucketPage<K, V, KC>>();
    if (!bucket_page->IsEmpty()) {
      break;
    }

    uint32_t split_idx = directory_page->GetSplitImageIndex(bucket_idx);

    if (directory_page->GetLocalDepth(split_idx) != local_depth) {
      break;
    }

    page_id_t split_page_id = directory_page->GetBucketPageId(split_idx);
    uint32_t new_local_depth = local_depth - 1;
    for (uint32_t i = 0; i < directory_page->Size(); ++i) {
      page_id_t cur_page_id = directory_page->GetBucketPageId(i);

      if (cur_page_id == bucket_page_id || cur_page_id == split_page_id) {
        directory_page->SetBucketPageId(i, split_page_id);
        directory_page->SetLocalDepth(i, new_local_depth);
      }
    }

    while (directory_page->CanShrink()) {
      directory_page->DecrGlobalDepth();
    }

    bucket_page_id = split_page_id;
    bucket_idx = split_idx & (directory_page->Size() - 1);
  }
  return true;
}

template class DiskExtendibleHashTable<int, int, IntComparator>;
template class DiskExtendibleHashTable<GenericKey<4>, RID, GenericComparator<4>>;
template class DiskExtendibleHashTable<GenericKey<8>, RID, GenericComparator<8>>;
template class DiskExtendibleHashTable<GenericKey<16>, RID, GenericComparator<16>>;
template class DiskExtendibleHashTable<GenericKey<32>, RID, GenericComparator<32>>;
template class DiskExtendibleHashTable<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
