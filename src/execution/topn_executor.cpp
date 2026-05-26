#include "execution/executors/topn_executor.h"
#include <algorithm>
#include <queue>
#include <utility>
#include <vector>
#include "common/rid.h"
#include "execution/executors/sort_executor.h"
#include "storage/table/tuple.h"

namespace bustub {

TopNExecutor::TopNExecutor(ExecutorContext *exec_ctx, const TopNPlanNode *plan,
                           std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void TopNExecutor::Init() {
  child_executor_->Init();
  tuples_.clear();
  cursor_ = 0;
  num_in_heap_ = 0;
  const auto n = plan_->GetN();
  if (n == 0) {
    return;
  }
  const auto &order_bys = plan_->GetOrderBy();
  const auto &child_schema = child_executor_->GetOutputSchema();
  TupleComparator comparator(order_bys, &child_schema);
  std::priority_queue<Tuple, std::vector<Tuple>, TupleComparator> heap(comparator);

  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    if (heap.size() < n) {
      heap.push(child_tuple);
      continue;
    }
    if (comparator(child_tuple, heap.top())) {
      heap.pop();
      heap.push(child_tuple);
    }
  }
  num_in_heap_ = heap.size();
  while (!heap.empty()) {
    tuples_.emplace_back(heap.top());
    heap.pop();
  }

  std::sort(tuples_.begin(), tuples_.end(), comparator);
}

auto TopNExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= tuples_.size()) {
    return false;
  }
  *tuple = tuples_[cursor_++];
  *rid = tuple->GetRid();
  return true;
}

auto TopNExecutor::GetNumInHeap() -> size_t { return num_in_heap_; }

}  // namespace bustub
