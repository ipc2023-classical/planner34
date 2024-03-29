#ifndef TASKS_MULTIPLY_OUT_CONDITIONAL_EFFECTS_TASK_H
#define TASKS_MULTIPLY_OUT_CONDITIONAL_EFFECTS_TASK_H

#include "delegating_task.h"

namespace utils {
class LogProxy;
}

namespace tasks {
/*
  Task transformation that changes operators by multiplying out conditional effects.
  If the parent operator does not have conditional effects, then it remains as it was.
  If it has conditional effects, then it is replaced by a set of operators, one for each possible
  assignment to the variables mentioned in the conditions.
*/
class MultiplyOutConditionalEffectsTask : public DelegatingTask {
    const bool parent_has_conditional_effects;
    /*
      The following is indexed by operator ids of this task. Each element,
      i.e. conditions and effects of each operator, is sorted in the same
      order (var, val) as the inducing operator of the parent task.
    */
    std::vector<std::vector<FactPair>> operators_conditions;
    std::vector<std::vector<FactPair>> operators_effects;
    std::vector<int> parent_operator_index;

    void add_non_conditional_operator(int op_no);
    void add_conditional_operator(int op_no, const std::vector<FactPair>& multiplied_conditions);
    void multiply_out_conditions(int op_no, const std::vector<int>& conditional_variables,
            int var_index, std::vector<FactPair>& multiplied_conditions);
public:
    MultiplyOutConditionalEffectsTask(
        const std::shared_ptr<AbstractTask> &parent,
        utils::LogProxy &log);
    virtual ~MultiplyOutConditionalEffectsTask() override = default;

    virtual int get_operator_cost(int index, bool is_axiom) const override;
    virtual std::string get_operator_name(int index, bool is_axiom) const override;
    virtual int get_num_operators() const override;
    virtual int get_num_operator_preconditions(int index, bool is_axiom) const override;
    virtual FactPair get_operator_precondition(
        int op_index, int fact_index, bool is_axiom) const override;
    virtual int get_num_operator_effects(int op_index, bool is_axiom) const override;
    virtual int get_num_operator_effect_conditions(
        int op_index, int eff_index, bool is_axiom) const override;
    virtual FactPair get_operator_effect_condition(
        int op_index, int eff_index, int cond_index, bool is_axiom) const override;
    virtual FactPair get_operator_effect(
        int op_index, int eff_index, bool is_axiom) const override;
    virtual int convert_operator_index_to_parent(int index) const;
};

extern std::shared_ptr<AbstractTask> &get_root_task_without_conditional_effects(
    utils::LogProxy &log);
}

#endif
