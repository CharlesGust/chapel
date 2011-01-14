#include "astutil.h"
#include "expr.h"
#include "passes.h"
#include "stmt.h"
#include "symbol.h"
#include "type.h"
#include "stringutil.h"


static void
check_functions(FnSymbol* fn) {
  if (!strcmp(fn->name, "this") && fn->hasFlag(FLAG_NO_PARENS))
    USR_FATAL(fn, "method 'this' must have parentheses");

  if (!strcmp(fn->name, "these") && fn->hasFlag(FLAG_NO_PARENS))
    USR_FATAL(fn, "method 'these' must have parentheses");

  Vec<CallExpr*> rets;
  Vec<CallExpr*> calls;
  collectCallExprs(fn, calls);
  forv_Vec(CallExpr, call, calls) {
    if (call->isPrimitive(PRIM_RETURN) && call->parentSymbol == fn)
      rets.add(call);
  }
  if (rets.n == 0)
    return;
  bool returns_void = false;
  forv_Vec(CallExpr, ret, rets) {
    if (SymExpr* sym = toSymExpr(ret->get(1)))
      if (sym->var == gVoid)
        returns_void = true;
  }
  forv_Vec(CallExpr, ret, rets) {
    if (fn->getModule()->initFn == fn) {
      USR_FATAL(ret, "return statement is not in a function");
    } else if (returns_void && ret->typeInfo() != dtVoid) {
      USR_FATAL(fn, "Not all function returns return a value");
    }
  }
}


static void
check_parsed_vars(VarSymbol* var) {
  if (var->isParameter() && !var->immediate)
    if (!var->defPoint->init &&
        (toFnSymbol(var->defPoint->parentSymbol) ||
         toModuleSymbol(var->defPoint->parentSymbol)))
      USR_FATAL(var, "Top-level params must be initialized.");
  if (var->hasFlag(FLAG_CONFIG) &&
      var->defPoint->parentSymbol != var->getModule()->initFn) {
    const char *varType = NULL;
    if (var->hasFlag(FLAG_PARAM))
      varType = "parameters";
    else if (var->hasFlag(FLAG_CONST))
      varType = "constants";
    else
      varType = "variables";
    USR_FATAL_CONT(var->defPoint,
                   "Configuration %s only allowed at module scope.", varType);
  }
}


static void
check_named_arguments(CallExpr* call) {
  Vec<const char*> names;
  for_actuals(expr, call) {
    if (NamedExpr* named = toNamedExpr(expr)) {
      forv_Vec(const char, name, names) {
        if (!strcmp(name, named->name))
          USR_FATAL(named, "The named argument '%s' is used more "
                    "than once in the same function call.", name);
      }
      names.add(named->name);
    }
  }
}


void
checkParsed(void) {
  forv_Vec(CallExpr, call, gCallExprs) {
    check_named_arguments(call);
  }

  forv_Vec(DefExpr, def, gDefExprs) {
    if (toVarSymbol(def->sym))
      if (!def->init && !def->exprType && !def->sym->hasFlag(FLAG_TEMP))
        if (isBlockStmt(def->parentExpr) && !isArgSymbol(def->parentSymbol))
          if (def->parentExpr != rootModule->block)
            if (!def->sym->hasFlag(FLAG_INDEX_VAR))
              USR_FATAL_CONT(def->sym,
                             "Variable '%s' is not initialized or has no type",
                             def->sym->name);
  }

  forv_Vec(VarSymbol, var, gVarSymbols) {
    check_parsed_vars(var);
  }

  forv_Vec(FnSymbol, fn, gFnSymbols) {
    check_functions(fn);
  }
}


void
checkNormalized(void) {
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->hasFlag(FLAG_ITERATOR_FN)) {
      for_formals(formal, fn) {
        if (formal->intent == INTENT_IN ||
            formal->intent == INTENT_INOUT ||
            formal->intent == INTENT_OUT ||
            formal->intent == INTENT_REF) {
          USR_FATAL(formal, "formal argument of iterator cannot have intent");
        }
      }
      if (fn->retTag == RET_TYPE)
        USR_FATAL(fn, "iterators may not yield or return types");
      if (fn->retTag == RET_PARAM)
        USR_FATAL(fn, "iterators may not yield or return parameters");
    } else if (fn->hasFlag(FLAG_CONSTRUCTOR) &&
               !fn->hasFlag(FLAG_DEFAULT_CONSTRUCTOR)) {
      for_formals(formal, fn) {
        Vec<SymExpr*> symExprs;
        collectSymExprs(formal, symExprs);
        forv_Vec(SymExpr, se, symExprs) {
          if (se->var == fn->_this)
            USR_FATAL(se, "invalid access of class member in constructor header");
        }
      }
    }
  }
}


static int
isDefinedAllPaths(Expr* expr, Symbol* ret) {
  if (!expr)
    return 0;
  if (CallExpr* call = toCallExpr(expr)) {
    if (call->isPrimitive(PRIM_MOVE) || call->isNamed("="))
      if (SymExpr* lhs = toSymExpr(call->get(1)))
        if (lhs->var == ret)
          return 1 + isDefinedAllPaths(expr->next, ret);
    //
    // should mark functions that exit rather than relying on string
    //
    if (call->isNamed("halt"))
      return 1 + isDefinedAllPaths(expr->next, ret);
  } else if (BlockStmt* block = toBlockStmt(expr)) {
    if (!block->blockInfo ||
        block->blockInfo->isPrimitive(PRIM_BLOCK_DOWHILE_LOOP))
      if (int result = isDefinedAllPaths(block->body.head, ret))
        return result;
  } else if (isGotoStmt(expr)) {
    return 0;
  } else if (CondStmt* cond = toCondStmt(expr)) {
    if (isDefinedAllPaths(cond->thenStmt, ret) &&
        isDefinedAllPaths(cond->elseStmt, ret))
      return 1;
  }
  return isDefinedAllPaths(expr->next, ret);
}


static void
checkReturnPaths(FnSymbol* fn) {
  if (fn->hasFlag(FLAG_ITERATOR_FN) ||
      !strcmp(fn->name, "=") ||
      !strcmp(fn->name, "chpl__buildArrayRuntimeType") ||
      fn->retType == dtVoid ||
      fn->retTag == RET_TYPE ||
      fn->hasFlag(FLAG_EXTERN) ||
      fn->hasFlag(FLAG_DEFAULT_CONSTRUCTOR) ||
      fn->hasFlag(FLAG_TYPE_CONSTRUCTOR) ||
      fn->hasFlag(FLAG_AUTO_II))
    return;
  Symbol* ret = fn->getReturnSymbol();
  if (VarSymbol* var = toVarSymbol(ret))
    if (var->immediate)
      return;
  if (isEnumSymbol(ret))
    return;
  int result = isDefinedAllPaths(fn->body, ret);

  //
  // Issue a warning if there is a path that has zero definitions or
  // there is a path that has one definition and the function has a
  // specified return type; we care about there being a specified
  // return type because this specified return type is used to
  // initialize the return symbol but we don't want that to count as a
  // definition of a return value.
  //
  if (result == 0 || (result == 1 && fn->hasFlag(FLAG_SPECIFIED_RETURN_TYPE)))
    USR_WARN(fn->body, "control reaches end of function that returns a value");
}


void
checkResolved(void) {
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    checkReturnPaths(fn);
    if (fn->retType->symbol->hasFlag(FLAG_ITERATOR_RECORD) &&
        !fn->hasFlag(FLAG_ITERATOR_FN) &&
        fn->retType->defaultConstructor->defPoint->parentSymbol == fn)
      USR_FATAL(fn, "functions cannot return nested iterators or loop expressions");
  }

  forv_Vec(TypeSymbol, type, gTypeSymbols) {
    if (EnumType* et = toEnumType(type->type)) {
      for_enums(def, et) {
        if (def->init) {
          SymExpr* sym = toSymExpr(def->init);
          if (!sym || (!sym->var->hasFlag(FLAG_PARAM) &&
                       !toVarSymbol(sym->var)->immediate))
            USR_FATAL(def, "enumerator '%s' is not an int parameter", def->sym->name);
        }
      }
    }
  }
}
