#include "SMTPlan/Algebraist.h"

/* implementation of SMTPlan::Algebraist */
namespace SMTPlan {

	/*--------------*/
	/* FUNCTIONFLOW */
	/*--------------*/

	void FunctionFlow::addExpression(int opID, std::set<int> deps, pexpr &expr) {

		// expression in combination with others
		std::set<int> newOpKey;
		std::vector<SingleFlow> newFlows;
		std::vector<SingleFlow>::iterator fit = flows.begin();
		for(; fit!=flows.end(); fit++) {

			SingleFlow newFlow;
			newFlow.operators = fit->operators;
			newFlow.dependencies = fit->dependencies;
			newFlow.polynomial = fit->polynomial + expr;

			newFlow.operators.insert(opID);
			newFlow.dependencies.insert(deps.begin(), deps.end());
			newFlows.push_back(newFlow);

			std::set<int>::iterator kit = fit->operators.begin();
			for(; kit!=fit->operators.end(); kit++) {
				if(*kit < 0) newOpKey.insert(*kit);
				else newOpKey.insert(-(*kit));
			}

			fit->operators.insert(-opID);
		}

		flows.insert(flows.end(), newFlows.begin(), newFlows.end());

		// new expression
		SingleFlow newFlow;
		newFlow.operators = newOpKey;
		newFlow.operators.insert(opID);
		newFlow.dependencies = deps;
		newFlow.polynomial = expr;
		flows.push_back(newFlow);
	}

	void FunctionFlow::createChildren(std::map<int,FunctionFlow*> &allFlows) {

		std::vector<SingleFlow> resolvedFlows;
		while(flows.size() > 0) {

			SingleFlow currentFlow = flows.back();
			flows.pop_back();

			// this flow has no more dependencies
			if(currentFlow.dependencies.size() == 0) {
				resolvedFlows.push_back(currentFlow);
				continue;
			}

			int depID = *(currentFlow.dependencies.begin());
			FunctionFlow* dep = allFlows[depID];
			currentFlow.dependencies.erase(currentFlow.dependencies.begin());

			// dependency is constant
			if(dep->flows.size() == 0) {
				resolvedFlows.push_back(currentFlow);
				continue;
			}

			// for each single flow of the dependency
			std::vector<SingleFlow>::iterator fit = dep->flows.begin();
			for(; fit!=dep->flows.end(); fit++) {

				std::vector<int> v;
				std::vector<int>::iterator it = std::set_difference(
						fit->operators.begin(),fit->operators.end(),
						currentFlow.operators.begin(), currentFlow.operators.end(), v.begin());
				v.resize(it-v.begin());

				if(v.size() == 0) {

					// the dependent flow's conditions are subsumed by this one's
					currentFlow.polynomial = currentFlow.polynomial.subs(dep->function_string, fit->polynomial);
					flows.push_back(currentFlow);
					
				} else {

					// check if they are incompatible and skip

					// add the difference to the operators and substitute

					// if difference == dep->flows.size() every time, then we can also add a dep==constant entry

				}
			}
		}

		flows = resolvedFlows;
	}

	void FunctionFlow::integrate() {
		std::vector<SingleFlow>::iterator fit = flows.begin();
		for(; fit!=flows.end(); fit++) {
			fit->polynomial = piranha::math::integrate(fit->polynomial,"hasht");
		}
	}

	/*------------*/
	/* ALGEBRAIST */
	/*------------*/

	/* state */
	int alg_opID;
	int alg_funID;
	bool alg_is_continuous;
	VAL::time_spec alg_cond_time;
	VAL::time_spec alg_eff_time;
	VAL::comparison_op alg_comparison_op;

	/**
	 * Main processing method
	 */
	bool Algebraist::processDomain() {

		// for each operator, process effects
		Inst::OpStore::iterator opsItr = Inst::instantiatedOp::opsBegin();
		const Inst::OpStore::iterator opsEnd = Inst::instantiatedOp::opsEnd();
		for (; opsItr != opsEnd; ++opsItr) {
			Inst::instantiatedOp * const currOp = *opsItr;
		    alg_opID = currOp->getID() + 1;
			fe = currOp->getEnv();
			currOp->forOp()->visit(this);
		}

		map<int,FunctionFlow*>::iterator fit = function_flow.begin();
		for (; fit != function_flow.end(); ++fit) {
			fit->second->createChildren(function_flow);
			fit->second->integrate();
		}
	}

	/*-----------*/
	/* operators */
	/*-----------*/

	/**
	 * Visit an operator in order to process its continuous effects
	 */
	void Algebraist::visit_durative_action(VAL::durative_action * da) {
		da->effects->visit(this);
	}

	/*---------*/
	/* effects */
	/*---------*/

	void Algebraist::visit_effect_lists(VAL::effect_lists * e) {
		e->forall_effects.pc_list<VAL::forall_effect*>::visit(this);
		e->cond_effects.pc_list<VAL::cond_effect*>::visit(this);
		e->cond_assign_effects.pc_list<VAL::cond_effect*>::visit(this);
		e->assign_effects.pc_list<VAL::assignment*>::visit(this);
		e->timed_effects.pc_list<VAL::timed_effect*>::visit(this);
	}

	void Algebraist::visit_timed_effect(VAL::timed_effect * e) {
		alg_eff_time = e->ts;
		e->effs->visit(this);
	};

	void Algebraist::visit_assignment(VAL::assignment * e) {

		Inst::PNE * l = new Inst::PNE(e->getFTerm(), fe);	
		Inst::PNE * const lit = Inst::instantiatedOp::findPNE(l);

		if (!lit) return;

		alg_funID = lit->getID();

		// create flow structure
		checkFunction(lit);

		// process expression
		alg_is_continuous = false;
		alg_dependency_stack.clear();
		e->getExpr()->visit(this);
		pexpr expr = alg_expression_stack.back();
		alg_expression_stack.pop_back();

		if(!alg_is_continuous) return;

		// expression as derivative
		expr = expr / hasht;

		// operator
		VAL::assign_op op = e->getOp();
		switch(op) {
		case VAL::E_DECREASE:
			expr = -expr;
		case VAL::E_INCREASE:
			function_flow[alg_funID]->addExpression(alg_opID, alg_dependency_stack, expr);
			alg_dependency_stack.clear();
			break;
		}

		delete l;
	}

	void Algebraist::visit_forall_effect(VAL::forall_effect * e) {std::cout << "not implemented forall" << std::endl;};
	void Algebraist::visit_cond_effect(VAL::cond_effect * e) {std::cout << "not implemented cond" << std::endl;};

	/*-------------*/
	/* expressions */
	/*-------------*/

	void Algebraist::visit_plus_expression(VAL::plus_expression * s) {

		s->getLHS()->visit(this);
		s->getRHS()->visit(this);

		pexpr lhs = alg_expression_stack.back();
		alg_expression_stack.pop_back();
		pexpr rhs = alg_expression_stack.back();
		alg_expression_stack.pop_back();

		alg_expression_stack.push_back(lhs + rhs);
	}

	void Algebraist::visit_minus_expression(VAL::minus_expression * s) {

		s->getLHS()->visit(this);
		s->getRHS()->visit(this);

		pexpr lhs = alg_expression_stack.back();
		alg_expression_stack.pop_back();
		pexpr rhs = alg_expression_stack.back();
		alg_expression_stack.pop_back();

		alg_expression_stack.push_back(lhs - rhs);
	}

	void Algebraist::visit_mul_expression(VAL::mul_expression * s) {

		s->getLHS()->visit(this);
		s->getRHS()->visit(this);

		pexpr lhs = alg_expression_stack.back();
		alg_expression_stack.pop_back();
		pexpr rhs = alg_expression_stack.back();
		alg_expression_stack.pop_back();

		alg_expression_stack.push_back(lhs * rhs);
	}

	void Algebraist::visit_div_expression(VAL::div_expression * s) {

		s->getLHS()->visit(this);
		s->getRHS()->visit(this);

		pexpr lhs = alg_expression_stack.back();
		alg_expression_stack.pop_back();
		pexpr rhs = alg_expression_stack.back();
		alg_expression_stack.pop_back();

		alg_expression_stack.push_back(lhs / rhs);
	}

	void Algebraist::visit_uminus_expression(VAL::uminus_expression * s) {

		s->getExpr()->visit(this);

		pexpr exp = alg_expression_stack.back();
		alg_expression_stack.pop_back();

		alg_expression_stack.push_back(-1 * exp);
	}

	void Algebraist::visit_int_expression(VAL::int_expression * s) {
		std::stringstream ss;
		ss << s->double_value();
		alg_expression_stack.push_back(pexpr{ss.str()});
	}

	void Algebraist::visit_float_expression(VAL::float_expression * s) {
		std::stringstream ss;
		ss << s->double_value();
		alg_expression_stack.push_back(pexpr{ss.str()});
	}

	void Algebraist::visit_func_term(VAL::func_term * s) {

		Inst::PNE * l = new Inst::PNE(s, fe);
		Inst::PNE * const lit = Inst::instantiatedOp::findPNE(l);
		
		if (!lit) {
			std::cerr << "literal not found in expression: " << (*l) << std::endl;
			return;
		}

		checkFunction(lit);

		alg_expression_stack.push_back(function_var[lit->getID()]);
		alg_dependency_stack.insert(lit->getID());

		delete l;
	}

	void Algebraist::visit_special_val_expr(VAL::special_val_expr * s) {

		switch(s->getKind()) {

		case VAL::E_DURATION_VAR:
			alg_expression_stack.push_back(pexpr{"duration"});
			break;
		case VAL::E_HASHT:
			alg_is_continuous = true;
			alg_expression_stack.push_back(hasht);
			break;
		case VAL::E_TOTAL_TIME:
			alg_expression_stack.push_back(pexpr{"totaltime"});
			break;
		}
	}

	/*-------*/
	/* extra */
	/*-------*/

	void Algebraist::visit_timed_initial_literal(VAL::timed_initial_literal * til) {};
	void Algebraist::visit_preference(VAL::preference *){}
	void Algebraist::visit_event(VAL::event * e){}
    void Algebraist::visit_process(VAL::process * p){}
	void Algebraist::visit_derivation_rule(VAL::derivation_rule * o){}

}; // close namespace