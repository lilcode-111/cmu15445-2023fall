//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/executors/index_scan_executor.h"
#include <iostream>
#include <vector>
#include "storage/index/extendible_hash_table_index.h"
#include "storage/table/tuple.h"
#include "type/value.h"

namespace bustub {
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  index_info_ = GetExecutorContext()->GetCatalog()->GetIndex(plan_->GetIndexOid());
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(index_info_->table_name_);
  result_rids_.clear();
  cursor_ = 0;
  auto *index = dynamic_cast<HashTableIndexForTwoIntegerColumn *>(index_info_->index_.get());

  std::vector<Value> values;
  values.reserve(1);
  values.emplace_back(plan_->pred_key_->Evaluate(nullptr, GetOutputSchema()));
  Tuple key_tuple(values, &index_info_->key_schema_);
  index->ScanKey(key_tuple, &result_rids_, GetExecutorContext()->GetTransaction());
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (cursor_ < result_rids_.size()) {
    RID cur_rid = result_rids_[cursor_];
    cursor_++;

    auto [meta, cur_tuple] = table_info_->table_->GetTuple(cur_rid);
    if (meta.is_deleted_) {
      continue;
    }

    *tuple = cur_tuple;
    *rid = cur_rid;
    return true;
  }
  return false;
}

}  // namespace bustub
