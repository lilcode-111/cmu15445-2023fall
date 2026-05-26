#include <memory>
#include <vector>
#include "execution/plans/abstract_plan.h"
#include "execution/plans/limit_plan.h"
#include "execution/plans/sort_plan.h"
#include "execution/plans/topn_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

auto Optimizer::OptimizeSortLimitAsTopN(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeSortLimitAsTopN(child));
  }
  auto optimized_plan = plan->CloneWithChildren(children);
  if (optimized_plan->GetType() != PlanType::Limit) {
    return optimized_plan;
  }

  const auto &limit_plan = dynamic_cast<const LimitPlanNode &>(*optimized_plan);

  auto sort_plan = limit_plan.GetChildPlan();
  if (sort_plan->GetType() != PlanType::Sort) {
    return optimized_plan;
  }

  const auto &sort = dynamic_cast<const SortPlanNode &>(*sort_plan);

  return std::make_shared<const TopNPlanNode>(optimized_plan->output_schema_, sort.GetChildPlan(), sort.GetOrderBy(),
                                              limit_plan.GetLimit());
}

}  // namespace bustub
