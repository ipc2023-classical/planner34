#include "multiply_out_conditional_effects_task.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"

#include "../task_utils/task_properties.h"
#include "../tasks/root_task.h"
#include "../utils/logging.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <set>

using namespace std;
using utils::ExitCode;

namespace tasks {
MultiplyOutConditionalEffectsTask::MultiplyOutConditionalEffectsTask(
    const shared_ptr<AbstractTask> &parent,
    utils::LogProxy &log)
    : DelegatingTask(parent),
      parent_has_conditional_effects(task_properties::has_conditional_effects(TaskProxy(*parent))) {
    if (log.is_at_least_normal()) {
        log << "Computing MultiplyOutConditionalEffectsTask for root task" << endl;
    }
    // Create operators for the parent operators only if the task has
    // conditional effects.
    if (parent_has_conditional_effects) {
        TaskProxy task_proxy(*this);
        TaskProxy parent_proxy(*parent);
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
                size_t previous_num_ops = operators_conditions.size();

                vector<int> cvars(condition_variables.begin(), condition_variables.end());
                vector<FactPair> multiplied_conditions;
                multiply_out_conditions(op_no, cvars, 0, multiplied_conditions);

                // Debugging output for multiplied out operators.
                size_t num_multiplied_out_operators = operators_conditions.size() - previous_num_ops;
                if (log.is_at_least_debug() && num_multiplied_out_operators > 1) {
                    OperatorProxy op = parent_proxy.get_operators()[op_no];
                    log << "Multiplying out operator with id " << op_no << endl;
                    log << op.get_name() << endl;
                    log << "preconditions: ";
                    for (FactProxy pre : op.get_preconditions()) {
                        log << pre.get_pair() << " (" << pre.get_name() << "); ";
                    }
                    log << endl;
                    log << "effects: " << endl;
                    for (EffectProxy eff : op.get_effects()) {
                        log << "effect conditions: ";
                        for (FactProxy cond : eff.get_conditions()) {
                            log << cond.get_pair() << " (" << cond.get_name() << "); ";
                        }
                        FactProxy e = eff.get_fact();
                        log << "effect: " << e.get_pair() << " (" << e.get_name() << "); ";
                        log << endl;
                    }
                    log << endl;

                    log << "Multiplied out operators" << endl;
                    for (size_t i = previous_num_ops; i < operators_conditions.size(); ++i) {
                        log << "preconditions: ";
                        for (FactPair pre : operators_conditions[i]) {
                            log << pre << "; ";
                        }
                        log << endl;
                        log << "effects: ";
                        for (FactPair eff : operators_effects[i]) {
                            log << eff << "; ";
                        }
                        log << endl;
                    }
                    log << endl;
                }
            }
        }

        task_properties::verify_no_conditional_effects(task_proxy);
    }
}

void MultiplyOutConditionalEffectsTask::add_non_conditional_operator(int op_no) {
    vector<FactPair> conditions;
    conditions.reserve(parent->get_num_operator_preconditions(op_no, false));
    for (int fact_index = 0; fact_index < parent->get_num_operator_preconditions(op_no, false); ++fact_index) {
        FactPair fact = parent->get_operator_precondition(op_no, fact_index, false);
        conditions.push_back(move(fact));
    }
    operators_conditions.push_back(move(conditions)); // Already sorted.

    vector<FactPair> effects;
    effects.reserve(parent->get_num_operator_effects(op_no, false));
    for (int fact_index = 0; fact_index < parent->get_num_operator_effects(op_no, false); ++fact_index) {
        FactPair fact = parent->get_operator_effect(op_no, fact_index, false);
        effects.push_back(move(fact));
    }
    operators_effects.push_back(move(effects)); // Already sorted.

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
                effects.push_back(move(fact));
        }
    }
    if (effects.empty())
        return;

    // Effects have to be sorted by var in various places of the planner.
    sort(effects.begin(), effects.end());
    operators_effects.push_back(move(effects));

    /*
      Collect preconditions of the operators from the parent's preconditions
      and the multiplied out effect preconditions. We use a set here to filter
      out duplicates. Furthermore, we directly sort the set in the desired
      way, i.e. according to the order of (var, val) of the operator's effects.
    */
    set<FactPair> conditions;
    for (int fact_index = 0; fact_index < parent->get_num_operator_preconditions(op_no, false); ++fact_index) {
        FactPair fact = parent->get_operator_precondition(op_no, fact_index, false);
        conditions.insert(move(fact));
    }
    for (FactPair fact : multiplied_conditions) {
        conditions.insert(move(fact));
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

extern shared_ptr<AbstractTask> &get_root_task_without_conditional_effects(
    utils::LogProxy &log) {
    static shared_ptr<AbstractTask> task = nullptr;
    if (!task) {
        task = make_shared<MultiplyOutConditionalEffectsTask>(
            g_root_task, log);
    }
    return task;
}


static shared_ptr<AbstractTask> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Task with conditional effects compiled away.",
        "A transformation of the root task that multiplies out all operators"
        "with conditional effects.");
    utils::add_log_options_to_parser(parser);
    Options opts = parser.parse();
    if (parser.dry_run()) {
        return nullptr;
    } else {
        utils::LogProxy log = utils::get_log_from_options(opts);
        return make_shared<MultiplyOutConditionalEffectsTask>(
            g_root_task, log);
    }
}

static Plugin<AbstractTask> _plugin("multiply_out_conditional_effects", _parse);
}
