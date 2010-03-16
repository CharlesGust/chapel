#include "alist.h"
#include "astutil.h"
#include "expr.h"
#include "passes.h"
#include "stmt.h"


//
// returns true if the symbol is defined in an outer function to fn
// third argument not used at call site
//
static bool
isOuterVar(Symbol* sym, FnSymbol* fn, Symbol* parent = NULL) {
  if (!parent)
    parent = fn->defPoint->parentSymbol;
  if (!isFnSymbol(parent))
    return false;
  else if (sym->defPoint->parentSymbol == parent)
    return true;
  else
    return isOuterVar(sym, fn, parent->defPoint->parentSymbol);
}


//
// finds outer vars directly used in a function
//
static void
findOuterVars(FnSymbol* fn, Vec<Symbol*>* uses) {
  Vec<BaseAST*> asts;
  collect_asts(fn, asts);
  forv_Vec(BaseAST, ast, asts) {
    if (SymExpr* symExpr = toSymExpr(ast)) {
      Symbol* sym = symExpr->var;
      if (toVarSymbol(sym) || toArgSymbol(sym))
        if (isOuterVar(sym, fn))
          uses->add_exclusive(sym);
    }
  }
}


static void
addVarsToFormals(FnSymbol* fn, Vec<Symbol*>* vars) {
  Vec<BaseAST*> asts;
  collect_asts(fn->body, asts);
  forv_Vec(Symbol, sym, *vars) {
    if (sym) {
      Type* type = sym->type;
      if (type->refType)
        type = type->refType;
      ArgSymbol* arg = new ArgSymbol(INTENT_BLANK, sym->name, type);
      fn->insertFormalAtTail(new DefExpr(arg));
      forv_Vec(BaseAST, ast, asts) {
        if (SymExpr* se = toSymExpr(ast)) {
          if (se->var == sym) {
            if (!strcmp(fn->name, "_toFollower") ||
                !strcmp(fn->name, "_toFollowerHelp") ||
                !strcmp(fn->name, "_toFastFollower") ||
                !strcmp(fn->name, "_toFastFollowerHelp") ||
                !strcmp(fn->name, "_toLeader")) {
              se->var = arg;
            } else {
              CallExpr* call = toCallExpr(se->parentExpr);
              if (type == sym->type ||
                  (call && call->isPrimitive(PRIM_MOVE) && call->get(1) == se) ||
                  (call && call->isPrimitive(PRIM_SET_MEMBER) && call->get(1) == se) ||
                  (call && call->isPrimitive(PRIM_GET_MEMBER)) ||
                  (call && call->isPrimitive(PRIM_GET_MEMBER_VALUE)) ||
                  //
                  // let GET_LOCALE work apply to the reference
                  //
                  (call && call->isPrimitive(PRIM_GET_LOCALEID))) {
                se->var = arg;
              } else if (call && call->isPrimitive(PRIM_SET_REF)) {
                SET_LINENO(se);
                call->replace(new SymExpr(arg));
              } else {
                SET_LINENO(se);
                VarSymbol* tmp = newTemp(sym->type);
                se->getStmtExpr()->insertBefore(new DefExpr(tmp));
                se->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_GET_REF, arg)));
              se->var = tmp;
              }
            }
          }
        }
      }
    }
  }
}


static void
addVarsToActuals(CallExpr* call, Vec<Symbol*>* vars, bool outerCall) {
  forv_Vec(Symbol, sym, *vars) {
    if (sym) {
      if (!isReferenceType(sym->type) && !outerCall) {
        if (sym->type->refType) {
          VarSymbol* tmp = newTemp(sym->type->refType);
          call->getStmtExpr()->insertBefore(new DefExpr(tmp));
          call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_SET_REF, sym)));
          call->insertAtTail(tmp);
        } else {
          VarSymbol* tmp = newTemp(sym->type);
          call->getStmtExpr()->insertBefore(new DefExpr(tmp));
          call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, sym));
          call->insertAtTail(tmp);
        }
      } else {
        call->insertAtTail(sym);
      }
    }
  }
}


void
flattenNestedFunctions(Vec<FnSymbol*>& nestedFunctions) {
  compute_call_sites();

  Vec<FnSymbol*> nestedFunctionSet;
  forv_Vec(FnSymbol, fn, nestedFunctions)
    nestedFunctionSet.set_add(fn);

  Map<FnSymbol*,Vec<Symbol*>*> args_map;
  forv_Vec(FnSymbol, fn, nestedFunctions) {
    Vec<Symbol*>* uses = new Vec<Symbol*>();
    findOuterVars(fn, uses);
    args_map.put(fn, uses);
  }

  // iterate to get outer vars in a function based on outer vars in
  // functions it calls
  bool change;
  do {
    change = false;
    forv_Vec(FnSymbol, fn, nestedFunctions) {
      Vec<BaseAST*> asts;
      collect_top_asts(fn, asts);
      Vec<Symbol*>* uses = args_map.get(fn);
      forv_Vec(BaseAST, ast, asts) {
        if (CallExpr* call = toCallExpr(ast)) {
          if (call->isResolved()) {
            if (FnSymbol* fcall = call->findFnSymbol()) {
              Vec<Symbol*>* call_uses = args_map.get(fcall);
              if (call_uses) {
                forv_Vec(Symbol, sym, *call_uses) {
                  if (isOuterVar(sym, fn))
                    if (uses->add_exclusive(sym))
                      change = true;
                }
              }
            }
          }
        }
      }
    }
  } while (change);

  // update all call sites of nested functions this must be done
  // before updating the function so that the outer var actuals can be
  // updated with the outer var functions when the formals are updated
  // (in nested functions that call one another)
  forv_Vec(FnSymbol, fn, nestedFunctions) {
    Vec<Symbol*>* uses = args_map.get(fn);
    forv_Vec(CallExpr, call, *fn->calledBy) {

      //
      // call not in a nested function; handle the toFollower/toLeader cases
      //
      bool outerCall = false;
      if (FnSymbol* parent = toFnSymbol(call->parentSymbol)) {
        if (!nestedFunctionSet.set_in(parent)) {
          forv_Vec(Symbol, use, *uses) {
            if (use->defPoint->parentSymbol != parent &&
                !isOuterVar(use, parent))
              outerCall = true;
          }
          if (outerCall) {
            nestedFunctionSet.set_add(parent);
            nestedFunctions.add(parent);
            Vec<Symbol*>* usesCopy = new Vec<Symbol*>();
            forv_Vec(Symbol, use, *uses)
              usesCopy->add(use);
            args_map.put(parent, usesCopy);
          }
        }
      }

      addVarsToActuals(call, uses, outerCall);
    }
  }

  // move nested functions to module level
  forv_Vec(FnSymbol, fn, nestedFunctions) {
    if (isFnSymbol(fn->defPoint->parentSymbol)) {
      ModuleSymbol* mod = fn->getModule();
      Expr* def = fn->defPoint;
      def->remove();
      mod->block->insertAtTail(def);
    }
  }

  // add extra formals to nested functions
  forv_Vec(FnSymbol, fn, nestedFunctions) {
    Vec<Symbol*>* uses = args_map.get(fn);
    addVarsToFormals(fn, uses);
  }

  //
  // remove types from functions
  //
  forv_Vec(TypeSymbol, ts, gTypeSymbols) {
    if (FnSymbol* fn = toFnSymbol(ts->defPoint->parentSymbol))
      fn->defPoint->insertBefore(ts->defPoint->remove());
  }
}


void flattenFunctions(void) {
  Vec<FnSymbol*> nestedFunctions;
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (isFnSymbol(fn->defPoint->parentSymbol))
      nestedFunctions.add(fn);
  }

  flattenNestedFunctions(nestedFunctions);
}
