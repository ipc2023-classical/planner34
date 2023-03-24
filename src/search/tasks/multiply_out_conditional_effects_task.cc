#include "multiply_out_conditional_effects_task.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"
#include "../utils/system.h"

#include "../tasks/root_task.h"
#include "../task_utils/task_properties.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <set>

using namespace std;
using utils::ExitCode;

namespace tasks {
MultiplyOutConditionalEffectsTask::MultiplyOutConditionalEffectsTask(
    const shared_ptr<AbstractTask> &parent,
    const options::Options &opts)
    : DelegatingTask(parent),
      dump_tasks(opts.get<bool>("dump_tasks")),
      parent_has_conditional_effects(task_properties::has_conditional_effects(TaskProxy(*parent))) {
    // Create operators for the parent operators only if the task has
    // conditional effects.
    if (parent_has_conditional_effects) {
        for (int op_no = 0; op_no < parent->get_num_operators(); ++op_no) {
            set<int> condition_variables;
            for (int fact_index = 0; fact_index < parent->get_num_operator_effects(op_no, false); ++fact_index) {
                for (int c_index = 0; c_index < parent->get_num_operator_effect_conditions(op_no, fact_index, false); ++c_index) {
                    FactPair fact = parent->get_operator_effect_condition(op_no, fact_index, c_index, false);
                    condition_variables.insert(fact.var);
                }
            }
            if (condition_variables.empty()) {
                // No conditional effects, just push the operator
                add_non_conditional_operator(op_no);
            } else {
                vector<int> cvars(condition_variables.begin(), condition_variables.end());
                vector<FactPair> multiplied_conditions;
                multiply_out_conditions(op_no, cvars, 0, multiplied_conditions);
            }
        }

        TaskProxy task_proxy(*this);
        if (dump_tasks) {
            cout << "original operators:" << endl;
            TaskProxy parent_proxy(*parent);
//            parent_proxy.get_operators().dump_fdr();

            cout << "compiled operators:" << endl;

//            task_proxy.get_operators().dump_fdr();
        }
        task_properties::verify_no_conditional_effects(task_proxy);
    }
}

void MultiplyOutConditionalEffectsTask::add_non_conditional_operator(int op_no) {
    vector<FactPair> conditions;
    for (int fact_index = 0; fact_index < parent->get_num_operator_preconditions(op_no, false); ++fact_index) {
        FactPair fact = parent->get_operator_precondition(op_no, fact_index, false);
        conditions.emplace_back(fact.var, fact.value);
    }
    operators_conditions.push_back(conditions); // Already sorted.

    vector<FactPair> effects;
    for (int fact_index = 0; fact_index < parent->get_num_operator_effects(op_no, false); ++fact_index) {
        FactPair fact = parent->get_operator_effect(op_no, fact_index, false);
        effects.emplace_back(fact.var, fact.value);
    }
    operators_effects.push_back(effects); // Already sorted.

    parent_operator_index.push_back(op_no);
}

void MultiplyOutConditionalEffectsTask::add_conditional_operator(int op_no,
        const std::vector<FactPair>& multiplied_conditions) {
    // multiplied_conditions keeps an assignment to all variables in conditions of effects
    int num_vars = get_num_variables();
    vector<int> assignment(num_vars, -1);
    for (FactPair fact : multiplied_conditions) {
        assignment[fact.var] = fact.value;
    }
    // Going over the effects and collecting those that fire.
    vector<FactPair> effects;
    for (int eff_index = 0; eff_index < parent->get_num_operator_effects(op_no, false); ++eff_index) {
        bool fires = true;
        for (int cond_index = 0;
             cond_index < parent->get_num_operator_effect_conditions(op_no, eff_index, false);
             ++cond_index) {
            FactPair fact = parent->get_operator_effect_condition(op_no, eff_index, cond_index, false);
            assert(assignment[fact.var] != -1);
            if (assignment[fact.var] != fact.value) {
                fires = false;
                break;
            }
        }
        if (fires) {
            FactPair fact = parent->get_operator_effect(op_no, eff_index, false);
            // Check if the operator effect is not redundant because it is
            // already a (multiplied out) precondidtion.
            if (assignment[fact.var] != fact.value)
                effects.emplace_back(fact.var, fact.value);
        }
    }
    if (effects.empty())
        return;

    operators_effects.push_back(effects);

    // Compute a mapping of variables to indices (i.e. an order) according
    // to the effects of the operator.
    vector<int> effect_var_indices(parent->get_num_variables(), -1);
    for (size_t i = 0; i < effects.size(); ++i) {
        effect_var_indices[effects[i].var] = i;
    }

    /*
      Collect preconditions of the operators from the parent's preconditions
      and the multiplied out effect preconditions. We use a set here to filter
      out duplicates. Furthermore, we directly sort the set in the desired
      way, i.e. according to the order of (var, val) of the operator's effects.
    */
    set<FactPair> conditions;
    for (int fact_index = 0; fact_index < parent->get_num_operator_preconditions(op_no, false); ++fact_index) {
        FactPair fact = parent->get_operator_precondition(op_no, fact_index, false);
        conditions.insert(fact);
    }
    for (FactPair fact : multiplied_conditions) {
        conditions.insert(fact);
    }

    // Turn the conditions into a vector.
    operators_conditions.emplace_back(conditions.begin(), conditions.end());

    parent_operator_index.push_back(op_no);
}

void MultiplyOutConditionalEffectsTask::multiply_out_conditions(int op_no, const std::vector<int>& conditional_variables,
        int var_index, std::vector<FactPair>& multiplied_conditions) {
    if (var_index == static_cast<int>(conditional_variables.size())) {
        add_conditional_operator(op_no, multiplied_conditions);
        return;
    }
    int var = conditional_variables[var_index];
    int domain_size = get_variable_domain_size(var);
    for (int value = 0; value < domain_size; ++value) {
        multiplied_conditions.emplace_back(var,value);
        multiply_out_conditions(op_no, conditional_variables, var_index+1, multiplied_conditions);
        multiplied_conditions.pop_back();
    }
}


int MultiplyOutConditionalEffectsTask::get_operator_cost(int index, bool is_axiom) const {
    if (is_axiom || !parent_has_conditional_effects)
        return parent->get_operator_cost(index, is_axiom);
    return parent->get_operator_cost(parent_operator_index[index], is_axiom);
}

std::string MultiplyOutConditionalEffectsTask::get_operator_name(int index, bool is_axiom) const {
    if (is_axiom || !parent_has_conditional_effects)
        return parent->get_operator_name(index, is_axiom);
    return parent->get_operator_name(parent_operator_index[index], is_axiom);
}

int MultiplyOutConditionalEffectsTask::get_num_operators() const {
    if (!parent_has_conditional_effects)
        return parent->get_num_operators();
    return static_cast<int>(parent_operator_index.size());
}

int MultiplyOutConditionalEffectsTask::get_num_operator_preconditions(int index, bool is_axiom) const {
    if (is_axiom || !parent_has_conditional_effects)
        return parent->get_num_operator_preconditions(index, is_axiom);
    return static_cast<int>(operators_conditions[index].size());
}

FactPair MultiplyOutConditionalEffectsTask::get_operator_precondition(
    int op_index, int fact_index, bool is_axiom) const {
    if (is_axiom || !parent_has_conditional_effects)
        return parent->get_operator_precondition(op_index, fact_index, is_axiom);
    return operators_conditions[op_index][fact_index];
}

int MultiplyOutConditionalEffectsTask::get_num_operator_effects(int op_index, bool is_axiom) const {
    if (is_axiom || !parent_has_conditional_effects)
        return parent->get_num_operator_effects(op_index, is_axiom);
    return static_cast<int>(operators_effects[op_index].size());
}

int MultiplyOutConditionalEffectsTask::get_num_operator_effect_conditions(
    int op_index, int eff_index, bool is_axiom) const {
    if (is_axiom || !parent_has_conditional_effects) {
        assert(parent->get_num_operator_effect_conditions(op_index, eff_index, is_axiom) == 0);
        return parent->get_num_operator_effect_conditions(op_index, eff_index, is_axiom);
    }
    return 0;
}

FactPair MultiplyOutConditionalEffectsTask::get_operator_effect_condition(
    int op_index, int eff_index, int cond_index, bool is_axiom) const {
    if (is_axiom || !parent_has_conditional_effects)
        return parent->get_operator_effect_condition(op_index, eff_index, cond_index, is_axiom);
    cerr << "Task does not have conditional effects, cannot query for effect condition" << endl;
    utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
}

FactPair MultiplyOutConditionalEffectsTask::get_operator_effect(
    int op_index, int eff_index, bool is_axiom) const {
    if (is_axiom || !parent_has_conditional_effects)
        return parent->get_operator_effect(op_index, eff_index, is_axiom);
    return operators_effects[op_index][eff_index];
}

int MultiplyOutConditionalEffectsTask::convert_operator_index_to_parent(int index) const {
    if (!parent_has_conditional_effects)
        return index;
    return parent_operator_index[index];
}


static shared_ptr<AbstractTask> _parse(OptionParser &parser) {
    parser.add_option<bool>(
        "dump_tasks",
        "dump the original root task and the compiled one",
        "false");
    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<MultiplyOutConditionalEffectsTask>(g_root_task, opts);
}

static Plugin<AbstractTask> _plugin("multiply_out_conditional_effects", _parse);
}
