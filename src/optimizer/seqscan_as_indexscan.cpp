#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include "binder/tokens.h"
#include "catalog/schema.h"
#include "execution/expressions/abstract_expression.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/index_scan_plan.h"
#include "execution/plans/seq_scan_plan.h"
#include "optimizer/optimizer.h"
#include "storage/index/index.h"

namespace bustub {

auto Optimizer::OptimizeSeqScanAsIndexScan(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  std::vector<AbstractPlanNodeRef> children;
  children.reserve(plan->GetChildren().size());
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeSeqScanAsIndexScan(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() != PlanType::SeqScan) {
    return optimized_plan;
  }

  const auto *seq_scan_plan = dynamic_cast<const SeqScanPlanNode *>(optimized_plan.get());
  if (seq_scan_plan == nullptr) {
    return optimized_plan;
  }

  auto predicate = seq_scan_plan->filter_predicate_;
  if (predicate == nullptr) {
    return optimized_plan;
  }

  const auto *comparison_expr = dynamic_cast<const ComparisonExpression *>(predicate.get());
  if (comparison_expr == nullptr) {
    return optimized_plan;
  }

  if (comparison_expr->comp_type_ != ComparisonType::Equal) {
    return optimized_plan;
  }

  auto left_expr = comparison_expr->GetChildAt(0);
  auto right_expr = comparison_expr->GetChildAt(1);

  const auto *left_col = dynamic_cast<const ColumnValueExpression *>(left_expr.get());
  const auto *right_col = dynamic_cast<const ColumnValueExpression *>(right_expr.get());

  const auto *left_const = dynamic_cast<const ConstantValueExpression *>(left_expr.get());
  const auto *right_const = dynamic_cast<const ConstantValueExpression *>(right_expr.get());

  uint32_t col_index;
  ConstantValueExpression *pred_key;

  if (left_col != nullptr && right_const != nullptr) {
    col_index = left_col->GetColIdx();
    pred_key = const_cast<ConstantValueExpression *>(right_const);
  } else if (right_col != nullptr && left_const != nullptr) {
    col_index = right_col->GetColIdx();
    pred_key = const_cast<ConstantValueExpression *>(left_const);
  } else {
    return optimized_plan;
  }

  auto table_info = catalog_.GetTable(seq_scan_plan->GetTableOid());
  auto indexes = catalog_.GetTableIndexes(table_info->name_);

  for (const auto &index_info : indexes) {
    const auto &key_attrs = index_info->index_->GetKeyAttrs();
    if (key_attrs.size() == 1 && key_attrs[0] == col_index) {
      return std::make_shared<const IndexScanPlanNode>(std::make_shared<Schema>(optimized_plan->OutputSchema()),
                                                       seq_scan_plan->GetTableOid(), index_info->index_oid_, predicate,
                                                       pred_key);
    }
  }
  return optimized_plan;
}

}  // namespace bustub
