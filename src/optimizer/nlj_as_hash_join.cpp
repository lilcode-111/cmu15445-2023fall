#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include "binder/table_ref/bound_join_ref.h"
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/abstract_expression.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

auto TryExtractEquiJoinKeys(const AbstractExpressionRef &expr, std::vector<AbstractExpressionRef> *left_keys,
                            std::vector<AbstractExpressionRef> *right_keys) -> bool {
  if (expr == nullptr) {
    return false;
  }
  if (auto logic_expr = dynamic_cast<const LogicExpression *>(expr.get()); logic_expr != nullptr) {
    if (logic_expr->logic_type_ != LogicType::And) {
      return false;
    }

    return TryExtractEquiJoinKeys(logic_expr->GetChildAt(0), left_keys, right_keys) &&
           TryExtractEquiJoinKeys(logic_expr->GetChildAt(1), left_keys, right_keys);
  }

  auto comp_expr = dynamic_cast<const ComparisonExpression *>(expr.get());
  if (comp_expr == nullptr || comp_expr->comp_type_ != ComparisonType::Equal) {
    return false;
  }
  auto left_expr = comp_expr->GetChildAt(0);
  auto right_expr = comp_expr->GetChildAt(1);

  auto left_col = dynamic_cast<const ColumnValueExpression *>(left_expr.get());
  auto right_col = dynamic_cast<const ColumnValueExpression *>(right_expr.get());

  if (left_col == nullptr || right_col == nullptr) {
    return false;
  }

  if (left_col->GetTupleIdx() == 0 && right_col->GetTupleIdx() == 1) {
    left_keys->emplace_back(left_expr);
    right_keys->emplace_back(right_expr);
    return true;
  }

  if (left_col->GetTupleIdx() == 1 && right_col->GetTupleIdx() == 0) {
    left_keys->emplace_back(right_expr);
    right_keys->emplace_back(left_expr);
    return true;
  }
  return false;
}

auto Optimizer::OptimizeNLJAsHashJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  std::vector<AbstractPlanNodeRef> children;

  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeNLJAsHashJoin(child));
  }

  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() != PlanType::NestedLoopJoin) {
    return optimized_plan;
  }

  const auto &nlj_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan);

  if ((nlj_plan.GetJoinType() != JoinType::INNER) && (nlj_plan.GetJoinType() != JoinType::LEFT)) {
    return optimized_plan;
  }

  std::vector<AbstractExpressionRef> left_keys;
  std::vector<AbstractExpressionRef> right_keys;

  if (!TryExtractEquiJoinKeys(nlj_plan.Predicate(), &left_keys, &right_keys)) {
    return optimized_plan;
  }

  if (left_keys.empty()) {
    return optimized_plan;
  }

  return std::make_shared<HashJoinPlanNode>(optimized_plan->output_schema_, optimized_plan->GetChildAt(0),
                                            optimized_plan->GetChildAt(1), left_keys, right_keys,
                                            nlj_plan.GetJoinType());
}

}  // namespace bustub
