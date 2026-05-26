#include "execution/executors/window_function_executor.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>
#include "common/rid.h"
#include "concurrency/transaction.h"
#include "execution/executors/sort_executor.h"
#include "execution/plans/window_plan.h"
#include "storage/table/tuple.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

auto ValueEqual(const Value &a, const Value &b) -> bool {
  if (a.IsNull() && b.IsNull()) {
    return true;
  }
  if (a.IsNull() || b.IsNull()) {
    return false;
  }
  return a.CompareEquals(b) == CmpBool::CmpTrue;
}

auto KeyEqual(const std::vector<Value> &a, const std::vector<Value> &b) -> bool {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (!ValueEqual(a[i], b[i])) {
      return false;
    }
  }
  return true;
}

auto OrderByEqual(const Tuple &a, const Tuple &b,
                  const std::vector<std::pair<OrderByType, AbstractExpressionRef>> &order_bys, const Schema &schema) {
  for (const auto &order_by : order_bys) {
    const auto &expr = order_by.second;
    auto a_val = expr->Evaluate(&a, schema);
    auto b_val = expr->Evaluate(&b, schema);

    if (!ValueEqual(a_val, b_val)) {
      return false;
    }
  }
  return true;
}

auto ComputePartitionAggregate(const std::vector<size_t> &partition, size_t end_pos,
                               const WindowFunctionPlanNode::WindowFunction &windowfunc,
                               const std::vector<Tuple> &input_tuples, const Schema &child_schema, TypeId output_type)
    -> Value {
  int32_t count = 0;
  bool has_value = false;
  Value agg_value = ValueFactory::GetNullValueByType(output_type);

  for (size_t pos = 0; pos <= end_pos; ++pos) {
    const auto row_idx = partition[pos];
    if (windowfunc.type_ == WindowFunctionType::CountStarAggregate) {
      count++;
      continue;
    }

    auto value = windowfunc.function_->Evaluate(&input_tuples[row_idx], child_schema);
    if (windowfunc.type_ == WindowFunctionType::CountAggregate) {
      if (!value.IsNull()) {
        count++;
      }
      continue;
    }

    if (value.IsNull()) {
      continue;
    }

    if (!has_value) {
      agg_value = value;
      has_value = true;
      continue;
    }

    if (windowfunc.type_ == WindowFunctionType::SumAggregate) {
      agg_value = agg_value.Add(value);
    } else if (windowfunc.type_ == WindowFunctionType::MaxAggregate) {
      if (value.CompareGreaterThan(agg_value) == CmpBool::CmpTrue) {
        agg_value = value;
      }
    } else {
      if (value.CompareLessThan(agg_value) == CmpBool::CmpTrue) {
        agg_value = value;
      }
    }
  }

  if (windowfunc.type_ == WindowFunctionType::CountAggregate ||
      windowfunc.type_ == WindowFunctionType::CountStarAggregate) {
    return ValueFactory::GetIntegerValue(count);
  }

  if (!has_value) {
    return ValueFactory::GetNullValueByType(output_type);
  }

  return agg_value;
}

WindowFunctionExecutor::WindowFunctionExecutor(ExecutorContext *exec_ctx, const WindowFunctionPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void WindowFunctionExecutor::Init() {
  child_executor_->Init();

  result_tuples_.clear();
  cursor_ = 0;

  std::vector<Tuple> input_tuples;
  Tuple child_tuple;
  RID child_rid;

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    input_tuples.emplace_back(child_tuple);
  }

  const auto &child_schema = child_executor_->GetOutputSchema();
  const auto &output_schema = GetOutputSchema();
  const auto output_col_count = output_schema.GetColumnCount();

  std::vector<std::vector<Value>> window_results(input_tuples.size(), std::vector<Value>(output_col_count));

  std::vector<size_t> output_order;
  bool output_order_set = false;

  for (const auto &window_func_pair : plan_->window_functions_) {
    const auto output_col_idx = window_func_pair.first;
    const auto &window_func = window_func_pair.second;
    const auto output_type = output_schema.GetColumn(output_col_idx).GetType();

    std::vector<std::vector<Value>> partition_keys;
    std::vector<std::vector<size_t>> partitions;

    for (size_t row_idx = 0; row_idx < input_tuples.size(); ++row_idx) {
      std::vector<Value> key;
      key.reserve(window_func.partition_by_.size());

      for (const auto &expr : window_func.partition_by_) {
        key.emplace_back(expr->Evaluate(&input_tuples[row_idx], child_schema));
      }
      bool found = false;
      for (size_t part_idx = 0; part_idx < partition_keys.size(); ++part_idx) {
        if (KeyEqual(key, partition_keys[part_idx])) {
          partitions[part_idx].emplace_back(row_idx);
          found = true;
          break;
        }
      }
      if (!found) {
        partition_keys.emplace_back(std::move(key));
        partitions.emplace_back(std::vector<size_t>{row_idx});
      }
    }

    TupleComparator comparator(window_func.order_by_, &child_schema);

    const bool collect_output_order = !output_order_set && !window_func.order_by_.empty();
    if (collect_output_order) {
      output_order.clear();
      output_order.reserve(input_tuples.size());
    }

    for (auto &partition : partitions) {
      if (!window_func.order_by_.empty()) {
        std::sort(partition.begin(), partition.end(),
                  [&](size_t lhs, size_t rhs) { return comparator(input_tuples[lhs], input_tuples[rhs]); });
      }

      if (collect_output_order) {
        for (const auto row_idx : partition) {
          output_order.emplace_back(row_idx);
        }
      }

      if (window_func.type_ == WindowFunctionType::Rank) {
        size_t rank = 1;
        for (size_t pos = 0; pos < partition.size(); ++pos) {
          const auto row_idx = partition[pos];
          if (pos > 0) {
            const auto prev_row_idx = partition[pos - 1];
            if (!OrderByEqual(input_tuples[row_idx], input_tuples[prev_row_idx], window_func.order_by_, child_schema)) {
              rank = pos + 1;
            }
          }
          window_results[row_idx][output_col_idx] = ValueFactory::GetIntegerValue(static_cast<int>(rank));
        }
        continue;
      }

      if (window_func.order_by_.empty()) {
        if (!partition.empty()) {
          auto result = ComputePartitionAggregate(partition, partition.size() - 1, window_func, input_tuples,
                                                  child_schema, output_type);
          for (const auto row_idx : partition) {
            window_results[row_idx][output_col_idx] = result;
          }
        }
      } else {
        for (size_t pos = 0; pos < partition.size(); ++pos) {
          const auto row_idx = partition[pos];
          auto result = ComputePartitionAggregate(partition, pos, window_func, input_tuples, child_schema, output_type);
          window_results[row_idx][output_col_idx] = result;
        }
      }
    }
    if (collect_output_order) {
      output_order_set = true;
    }
  }

  if (!output_order_set) {
    output_order.reserve(input_tuples.size());
    for (size_t row_idx = 0; row_idx < input_tuples.size(); ++row_idx) {
      output_order.emplace_back(row_idx);
    }
  }

  for (const auto row_idx : output_order) {
    std::vector<Value> values;
    values.reserve(output_col_count);
    for (uint32_t col_idx = 0; col_idx < output_col_count; ++col_idx) {
      const auto iter = plan_->window_functions_.find(col_idx);

      if (iter != plan_->window_functions_.end()) {
        values.emplace_back(window_results[row_idx][col_idx]);
      } else {
        values.emplace_back(plan_->columns_[col_idx]->Evaluate(&input_tuples[row_idx], child_schema));
      }
    }
    result_tuples_.emplace_back(values, &output_schema);
  }
}

auto WindowFunctionExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  *rid = tuple->GetRid();
  return true;
}
}  // namespace bustub
