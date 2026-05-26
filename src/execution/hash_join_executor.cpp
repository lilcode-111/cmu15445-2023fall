//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.cpp
//
// Identification: src/execution/hash_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/hash_join_executor.h"
#include <cstdint>
#include <utility>
#include <vector>
#include "binder/table_ref/bound_join_ref.h"
#include "common/rid.h"
#include "execution/plans/hash_join_plan.h"
#include "storage/table/tuple.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)) {
  if ((plan->GetJoinType() != JoinType::LEFT) && (plan->GetJoinType() != JoinType::INNER)) {
    // Note for 2023 Fall: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void HashJoinExecutor::Init() {
  left_child_->Init();
  right_child_->Init();
  hash_table_.clear();

  matched_right_tuples_ = nullptr;
  matched_right_tuple_idx_ = 0;

  Tuple right_tuple;
  RID right_rid;
  while (right_child_->Next(&right_tuple, &right_rid)) {
    auto right_key = MakeRightJoinKey(right_tuple);
    hash_table_[right_key].push_back(right_tuple);
  }
}

auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (true) {
    if (matched_right_tuples_ != nullptr && matched_right_tuple_idx_ < matched_right_tuples_->size()) {
      const auto &right_tuple = (*matched_right_tuples_)[matched_right_tuple_idx_++];
      *tuple = MakeOutputTuple(left_tuple_, &right_tuple);
      return true;
    }
    matched_right_tuples_ = nullptr;
    matched_right_tuple_idx_ = 0;
    if (!left_child_->Next(&left_tuple_, &left_rid_)) {
      return false;
    }
    auto left_key = MakeLeftJoinKey(left_tuple_);
    auto iter = hash_table_.find(left_key);

    if (iter != hash_table_.end()) {
      matched_right_tuples_ = &iter->second;
      matched_right_tuple_idx_ = 0;
      continue;
    }

    if (plan_->GetJoinType() == JoinType::LEFT) {
      *tuple = MakeOutputTuple(left_tuple_, nullptr);
      return true;
    }
  }
}

auto HashJoinExecutor::MakeLeftJoinKey(const Tuple &tuple) const -> HashJoinKey {
  std::vector<Value> keys;
  for (const auto &expr : plan_->LeftJoinKeyExpressions()) {
    keys.emplace_back(expr->Evaluate(&tuple, left_child_->GetOutputSchema()));
  }
  return {keys};
}

auto HashJoinExecutor::MakeRightJoinKey(const Tuple &tuple) const -> HashJoinKey {
  std::vector<Value> keys;
  for (const auto &expr : plan_->RightJoinKeyExpressions()) {
    keys.emplace_back(expr->Evaluate(&tuple, right_child_->GetOutputSchema()));
  }
  return {keys};
}

auto HashJoinExecutor::MakeOutputTuple(const Tuple &left_tuple, const Tuple *right_tuple) const -> Tuple {
  std::vector<Value> values;
  const auto &left_schema = left_child_->GetOutputSchema();
  const auto &right_schema = right_child_->GetOutputSchema();
  for (uint32_t i = 0; i < left_schema.GetColumnCount(); ++i) {
    values.emplace_back(left_tuple.GetValue(&left_schema, i));
  }
  if (right_tuple != nullptr) {
    for (uint32_t i = 0; i < right_schema.GetColumnCount(); ++i) {
      values.emplace_back(right_tuple->GetValue(&right_schema, i));
    }
  } else {
    for (uint32_t i = 0; i < right_schema.GetColumnCount(); ++i) {
      values.emplace_back(ValueFactory::GetNullValueByType(right_schema.GetColumn(i).GetType()));
    }
  }
  return {values, &GetOutputSchema()};
}

}  // namespace bustub
