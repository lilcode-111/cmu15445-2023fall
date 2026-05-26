#include "execution/executors/sort_executor.h"
#include <algorithm>
#include <utility>
#include "catalog/schema.h"
#include "storage/table/tuple.h"

namespace bustub {

SortExecutor::SortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                           std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void SortExecutor::Init() {
  child_executor_->Init();
  tuples_.clear();
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    tuples_.emplace_back(child_tuple);
  }
  cursor_ = 0;
  auto order_bys = plan_->GetOrderBy();
  const auto &child_schema = child_executor_->GetOutputSchema();
  std::sort(tuples_.begin(), tuples_.end(), TupleComparator(order_bys, &child_schema));
}

auto SortExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= tuples_.size()) {
    return false;
  }
  *tuple = tuples_[cursor_++];
  *rid = tuple->GetRid();
  return true;
}

}  // namespace bustub
