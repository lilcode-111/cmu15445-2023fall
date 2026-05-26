//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include "common/rid.h"
#include "execution/executor_context.h"
#include "storage/table/tuple.h"
#include "type/value.h"
#include "type/value_factory.h"

#include "execution/executors/insert_executor.h"

namespace bustub {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() {
  child_executor_->Init();
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  is_insert_ = false;
}

auto InsertExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (is_insert_) {
    return false;
  }
  int32_t insert_count = 0;
  Tuple child_tuple;
  RID child_rid;
  auto indexes = GetExecutorContext()->GetCatalog()->GetTableIndexes(table_info_->name_);
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    TupleMeta tuple_meta{0, false};
    auto inserted_rid = table_info_->table_->InsertTuple(tuple_meta, child_tuple);
    if (!inserted_rid.has_value()) {
      continue;
    }
    insert_count++;
    for (const auto &index_info : indexes) {
      auto key =
          child_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      index_info->index_->InsertEntry(key, inserted_rid.value(), GetExecutorContext()->GetTransaction());
    }
  }
  std::vector<Value> values;
  values.emplace_back(ValueFactory::GetIntegerValue(insert_count));
  *tuple = Tuple(values, &GetOutputSchema());
  is_insert_ = true;
  return true;
}

}  // namespace bustub
