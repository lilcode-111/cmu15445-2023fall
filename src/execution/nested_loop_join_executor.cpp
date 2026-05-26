//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include <cstdint>
#include <utility>
#include <vector>
#include "binder/table_ref/bound_join_ref.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/rid.h"
#include "storage/table/tuple.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_executor_(std::move(left_executor)),
      right_executor_(std::move(right_executor)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for 2023 Fall: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestedLoopJoinExecutor::Init() {
  left_executor_->Init();
  has_left_ = false;
  left_matched_ = false;
}

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  auto left_schema = left_executor_->GetOutputSchema();
  auto right_schema = right_executor_->GetOutputSchema();
  Tuple right_tuple;
  RID right_rid;
  while (true) {
    if (!has_left_) {
      if (!left_executor_->Next(&left_tuple_, &left_rid_)) {
        return false;
      }
      right_executor_->Init();
      has_left_ = true;
      left_matched_ = false;
    }
    while (right_executor_->Next(&right_tuple, &right_rid)) {
      auto pred = plan_->predicate_->EvaluateJoin(&left_tuple_, left_schema, &right_tuple, right_schema);
      if (!pred.IsNull() && pred.GetAs<bool>()) {
        left_matched_ = true;
        std::vector<Value> values;
        for (uint32_t i = 0; i < left_schema.GetColumnCount(); ++i) {
          values.emplace_back(left_tuple_.GetValue(&left_schema, i));
        }
        for (uint32_t i = 0; i < right_schema.GetColumnCount(); ++i) {
          values.emplace_back(right_tuple.GetValue(&right_schema, i));
        }
        *tuple = Tuple(values, &GetOutputSchema());
        return true;
      }
    }
    if (plan_->GetJoinType() == JoinType::LEFT && !left_matched_) {
      std::vector<Value> values;
      for (uint32_t i = 0; i < left_schema.GetColumnCount(); ++i) {
        values.emplace_back(left_tuple_.GetValue(&left_schema, i));
      }
      for (uint32_t i = 0; i < right_schema.GetColumnCount(); ++i) {
        values.emplace_back(ValueFactory::GetNullValueByType(right_schema.GetColumn(i).GetType()));
      }
      *tuple = Tuple(values, &GetOutputSchema());
      has_left_ = false;
      return true;
    }
    has_left_ = false;
  }
}

}  // namespace bustub
