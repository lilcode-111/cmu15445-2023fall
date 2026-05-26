//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.cpp
//
// Identification: src/execution/delete_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <utility>
#include <vector>
#include "common/rid.h"
#include "storage/table/tuple.h"
#include "type/value.h"
#include "type/value_factory.h"

#include "execution/executors/delete_executor.h"

namespace bustub {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void DeleteExecutor::Init() {
  child_executor_->Init();
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  is_deleted_ = false;
}

auto DeleteExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (is_deleted_) {
    return false;
  }
  int32_t delete_count = 0;
  Tuple child_tuple;
  RID child_rid;
  auto indexes = GetExecutorContext()->GetCatalog()->GetTableIndexes(table_info_->name_);
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    TupleMeta tuple_meta{0, true};
    table_info_->table_->UpdateTupleMeta(tuple_meta, child_rid);
    delete_count++;
    for (const auto &index_info : indexes) {
      auto key =
          child_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      index_info->index_->DeleteEntry(key, child_rid, GetExecutorContext()->GetTransaction());
    }
  }
  std::vector<Value> values;
  values.push_back(ValueFactory::GetIntegerValue(delete_count));
  *tuple = Tuple(values, &GetOutputSchema());
  is_deleted_ = true;
  return true;
}

}  // namespace bustub
