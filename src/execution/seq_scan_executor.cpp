//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"
#include <memory>
#include "storage/table/table_iterator.h"

namespace bustub {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  iter_ = std::make_unique<TableIterator>(table_info_->table_->MakeIterator());
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (!iter_->IsEnd()) {
    auto [meta, cur_tuple] = iter_->GetTuple();
    auto cur_rid = iter_->GetRID();
    ++(*iter_);
    if (plan_->filter_predicate_ != nullptr) {
      auto filter_value = plan_->filter_predicate_->Evaluate(&cur_tuple, GetOutputSchema());
      if (filter_value.IsNull() || !filter_value.GetAs<bool>()) {
        continue;
      }
    }
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
