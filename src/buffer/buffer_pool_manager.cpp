//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager.h"
#include <mutex>
#include <utility>

#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "storage/disk/disk_scheduler.h"
#include "storage/page/page.h"
#include "storage/page/page_guard.h"

namespace bustub {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_scheduler_(std::make_unique<DiskScheduler>(disk_manager)), log_manager_(log_manager) {
  // TODO(students): remove this line after you have implemented the buffer pool manager
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  std::lock_guard<std::mutex> lock(latch_);
  frame_id_t new_frame_id_t;
  if (!free_list_.empty()) {
    new_frame_id_t = free_list_.front();
    free_list_.pop_front();
  } else {
    if (!replacer_->Evict(&new_frame_id_t)) {
      return nullptr;
    }
    auto &page_frame = pages_[new_frame_id_t];
    auto old_page_id = page_frame.page_id_;
    if (page_frame.is_dirty_) {
      auto promise = disk_scheduler_->CreatePromise();  // 这个我不懂
      auto future = promise.get_future();
      DiskRequest request;
      request.is_write_ = true;
      request.callback_ = std::move(promise);
      request.data_ = page_frame.GetData();
      request.page_id_ = old_page_id;
      disk_scheduler_->Schedule(std::move(request));
      future.get();
      page_frame.is_dirty_ = false;
    }
    page_table_.erase(old_page_id);
  }
  *page_id = AllocatePage();  // 这个要不要保存
  pages_[new_frame_id_t].ResetMemory();
  pages_[new_frame_id_t].page_id_ = *page_id;
  pages_[new_frame_id_t].pin_count_ = 1;
  pages_[new_frame_id_t].is_dirty_ = false;
  page_table_[*page_id] = new_frame_id_t;
  replacer_->RecordAccess(new_frame_id_t);
  replacer_->SetEvictable(new_frame_id_t, false);
  return &pages_[new_frame_id_t];
}

auto BufferPoolManager::FetchPage(page_id_t page_id, [[maybe_unused]] AccessType access_type) -> Page * {
  std::lock_guard<std::mutex> lock(latch_);
  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    pages_[it->second].pin_count_++;
    replacer_->RecordAccess(it->second);
    replacer_->SetEvictable(it->second, false);  // 为什么页面有，LRU-K不能做淘汰呢 LRU和这个页面是什么关系
    return &pages_[it->second];
  }
  frame_id_t new_frame_id_t;
  if (!free_list_.empty()) {
    new_frame_id_t = free_list_.front();
    free_list_.pop_front();
  } else {
    if (!replacer_->Evict(&new_frame_id_t)) {
      return nullptr;
    }
    auto &page_frame = pages_[new_frame_id_t];
    auto old_page_id = page_frame.page_id_;
    if (page_frame.is_dirty_) {
      auto promise = disk_scheduler_->CreatePromise();
      auto future = promise.get_future();
      DiskRequest request;
      request.is_write_ = true;
      request.callback_ = std::move(promise);
      request.data_ = page_frame.GetData();
      request.page_id_ = old_page_id;
      disk_scheduler_->Schedule(std::move(request));
      future.get();
      page_frame.is_dirty_ = false;
    }
    page_table_.erase(old_page_id);
  }
  auto promise = disk_scheduler_->CreatePromise();
  auto future = promise.get_future();
  pages_[new_frame_id_t].ResetMemory();
  DiskRequest request;
  request.callback_ = std::move(promise);
  request.data_ = pages_[new_frame_id_t].GetData();
  request.page_id_ = page_id;
  request.is_write_ = false;
  disk_scheduler_->Schedule(std::move(request));
  future.get();
  pages_[new_frame_id_t].page_id_ = page_id;
  pages_[new_frame_id_t].pin_count_ = 1;
  pages_[new_frame_id_t].is_dirty_ = false;
  page_table_[page_id] = new_frame_id_t;
  replacer_->RecordAccess(new_frame_id_t);
  replacer_->SetEvictable(new_frame_id_t, false);
  return &pages_[new_frame_id_t];
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty, [[maybe_unused]] AccessType access_type) -> bool {
  std::lock_guard<std::mutex> lock(latch_);
  if (page_table_.find(page_id) == page_table_.end()) {
    return false;
  }
  auto old_frame_id_t = page_table_.at(page_id);
  if (pages_[old_frame_id_t].pin_count_ <= 0) {
    return false;
  }
  if (is_dirty) {
    pages_[old_frame_id_t].is_dirty_ = true;
  }
  pages_[old_frame_id_t].pin_count_--;
  if (pages_[old_frame_id_t].pin_count_ == 0) {
    replacer_->SetEvictable(old_frame_id_t, true);
  }
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }
  auto old_frame_id_t = it->second;
  auto old_page = &pages_[old_frame_id_t];
  auto promise = disk_scheduler_->CreatePromise();
  auto future = promise.get_future();
  DiskRequest request;
  request.callback_ = std::move(promise);
  request.data_ = old_page->GetData();
  request.page_id_ = page_id;
  request.is_write_ = true;
  disk_scheduler_->Schedule(std::move(request));
  future.get();
  old_page->is_dirty_ = false;
  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::lock_guard<std::mutex> lock(latch_);
  for (auto it : page_table_) {
    auto old_frame_id_t = it.second;
    auto old_page_id = it.first;
    auto old_page = &pages_[old_frame_id_t];
    auto promise = disk_scheduler_->CreatePromise();
    auto future = promise.get_future();
    DiskRequest request;
    request.callback_ = std::move(promise);
    request.data_ = old_page->GetData();
    request.page_id_ = old_page_id;
    request.is_write_ = true;
    disk_scheduler_->Schedule(std::move(request));
    future.get();
    old_page->is_dirty_ = false;
  }
}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    DeallocatePage(page_id);
    return true;
  }
  auto old_frame_id_t = it->second;
  auto old_page = &pages_[old_frame_id_t];
  if (old_page->pin_count_ > 0) {
    return false;
  }
  replacer_->Remove(old_frame_id_t);
  page_table_.erase(page_id);
  free_list_.push_back(old_frame_id_t);
  old_page->ResetMemory();
  old_page->is_dirty_ = false;
  old_page->page_id_ = INVALID_PAGE_ID;
  old_page->pin_count_ = 0;
  DeallocatePage(page_id);
  return true;
}

auto BufferPoolManager::AllocatePage() -> page_id_t { return next_page_id_++; }

auto BufferPoolManager::FetchPageBasic(page_id_t page_id) -> BasicPageGuard {
  Page *page = FetchPage(page_id);
  if (page == nullptr) {
    return {};
  }
  return {this, page};
}

auto BufferPoolManager::FetchPageRead(page_id_t page_id) -> ReadPageGuard {
  Page *page = FetchPage(page_id);
  if (page == nullptr) {
    return {};
  }
  page->RLatch();
  return {this, page};
}

auto BufferPoolManager::FetchPageWrite(page_id_t page_id) -> WritePageGuard {
  Page *page = FetchPage(page_id);
  if (page == nullptr) {
    return {};
  }
  page->WLatch();
  return {this, page};
}

auto BufferPoolManager::NewPageGuarded(page_id_t *page_id) -> BasicPageGuard {
  Page *page = NewPage(page_id);
  if (page == nullptr) {
    return {};
  }
  return {this, page};
}

}  // namespace bustub
