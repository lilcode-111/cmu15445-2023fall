//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include "common/rid.h"
#include "storage/table/tuple.h"
#include "type/value.h"
#include "type/value_factory.h"

#include "execution/executors/update_executor.h"

namespace bustub {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {
  // As of Fall 2022, you DON'T need to implement update executor to have perfect score in project 3 / project 4.
}

void UpdateExecutor::Init() {
  child_executor_->Init();
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  is_update_ = false;
}

auto UpdateExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (is_update_) {
    return false;
  }
  Tuple old_tuple;
  RID old_rid;
  int32_t update_count = 0;
  auto indexes = GetExecutorContext()->GetCatalog()->GetTableIndexes(table_info_->name_);
  while (child_executor_->Next(&old_tuple, &old_rid)) {
    std::vector<Value> values;
    values.reserve(plan_->target_expressions_.size());
    for (const auto &expr : plan_->target_expressions_) {
      values.emplace_back(expr->Evaluate(&old_tuple, child_executor_->GetOutputSchema()));
    }
    Tuple new_tuple = Tuple(values, &table_info_->schema_);

    TupleMeta insert_meta{0, false};
    auto new_rid = table_info_->table_->InsertTuple(insert_meta, new_tuple);
    if (!new_rid.has_value()) {
      return false;
    }

    TupleMeta delete_meta{0, true};
    table_info_->table_->UpdateTupleMeta(delete_meta, old_rid);

    update_count++;
    for (const auto &index_info : indexes) {
      auto old_key =
          old_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      index_info->index_->DeleteEntry(old_key, old_rid, GetExecutorContext()->GetTransaction());
      auto new_key =
          new_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      index_info->index_->InsertEntry(new_key, new_rid.value(), GetExecutorContext()->GetTransaction());
    }
  }
  std::vector<Value> values;
  values.emplace_back(ValueFactory::GetIntegerValue(update_count));
  *tuple = Tuple(values, &GetOutputSchema());
  is_update_ = true;
  return true;
}

}  // namespace bustub
