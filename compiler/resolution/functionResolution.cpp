#include <sstream>
#include <map>

#include "astutil.h"
#include "build.h"
#include "caches.h"
#include "callInfo.h"
#include "expr.h"
#include "iterator.h"
#include "passes.h"
#include "resolution.h"
#include "scopeResolve.h"
#include "stmt.h"
#include "stringutil.h"
#include "symbol.h"
#include "../ifa/prim_data.h"

bool resolved = false;
bool inDynamicDispatchResolution = false;

SymbolMap paramMap;
static Expr* dropUnnecessaryCast(CallExpr* call);
static void foldEnumOp(int op, EnumSymbol *e1, EnumSymbol *e2, Immediate *imm);
static Expr* preFold(Expr* expr);
static Expr* postFold(Expr* expr);

static void setScalarPromotionType(ClassType* ct);
static void fixTypeNames(ClassType* ct);
static void insertReturnTemps();
static bool canParamCoerce(Type* actualType, Symbol* actualSym, Type* formalType);

static int explainCallLine;
static ModuleSymbol* explainCallModule;

static Vec<CallExpr*> inits;
static Map<FnSymbol*,bool> resolvedFns;
static Vec<FnSymbol*> resolvedFormals;
Vec<CallExpr*> callStack;

static Vec<CondStmt*> tryStack;
static bool tryFailure = false;

static Map<Type*,Type*> runtimeTypeMap; // map static types to runtime types
                                        // e.g. array and domain runtime types
static Map<Type*,FnSymbol*> valueToRuntimeTypeMap; // convertValueToRuntimeType
static Map<Type*,FnSymbol*> runtimeTypeToValueMap; // convertRuntimeTypeToValue

static std::map<std::string, std::pair<ClassType*, FnSymbol*> > functionTypeMap; // lookup table/cache for function types and their representative parents
static std::map<FnSymbol*, FnSymbol*> functionCaptureMap; //loopup table/cache for function captures

// map of compiler warnings that may need to be reissued for repeated
// calls; the inner compiler warning map is from the compilerWarning
// function; the outer compiler warning map is from the function
// containing the compilerWarning function
// to do: this needs to be a map from functions to multiple strings in
//        order to support multiple compiler warnings are allowed to
//        be in a single function
static Map<FnSymbol*,const char*> innerCompilerWarningMap;
static Map<FnSymbol*,const char*> outerCompilerWarningMap;

Map<Type*,FnSymbol*> autoCopyMap; // type to chpl__autoCopy function
Map<Type*,FnSymbol*> autoDestroyMap; // type to chpl__autoDestroy function

Map<FnSymbol*,FnSymbol*> iteratorLeaderMap; // iterator->leader map for promotion
Map<FnSymbol*,FnSymbol*> iteratorFollowerMap; // iterator->leader map for promotion

static Type* resolve_type_expr(Expr* expr);

static void resolveFns(FnSymbol* fn);

static void pruneResolvedTree();

//
// build reference type
//
static FnSymbol*
resolveUninsertedCall(Type* type, CallExpr* call) {
  if (type->defaultConstructor) {
    if (type->defaultConstructor->instantiationPoint)
      type->defaultConstructor->instantiationPoint->insertAtHead(call);
    else
      type->symbol->defPoint->insertBefore(call);
  } else
    chpl_main->insertAtHead(call);
  resolveCall(call);
  call->remove();
  return call->isResolved();
}


static void makeRefType(Type* type) {
  if (!type)
    return;

  if (type == dtMethodToken ||
      type == dtUnknown ||
      type->symbol->hasFlag(FLAG_REF) ||
      type->symbol->hasFlag(FLAG_GENERIC))
    return;

  if (type->refType)
    return;

  CallExpr* call = new CallExpr("_type_construct__ref", type->symbol);
  FnSymbol* fn = resolveUninsertedCall(type, call);
  type->refType = toClassType(fn->retType);
  type->refType->getField(1)->type = type;
}


static void
resolveAutoCopy(Type* type) {
  Symbol* tmp = newTemp(type);
  chpl_main->insertAtHead(new DefExpr(tmp));
  CallExpr* call = new CallExpr("chpl__autoCopy", tmp);
  FnSymbol* fn = resolveUninsertedCall(type, call);
  resolveFns(fn);
  autoCopyMap.put(type, fn);
  tmp->defPoint->remove();
}


static void
resolveAutoDestroy(Type* type) {
  Symbol* tmp = newTemp(type);
  chpl_main->insertAtHead(new DefExpr(tmp));
  CallExpr* call = new CallExpr("chpl__autoDestroy", tmp);
  FnSymbol* fn = resolveUninsertedCall(type, call);
  resolveFns(fn);
  autoDestroyMap.put(type, fn);
  tmp->defPoint->remove();
}


FnSymbol* getAutoCopy(Type *t) {
  return autoCopyMap.get(t);
}


FnSymbol* getAutoDestroy(Type* t) {
  return autoDestroyMap.get(t);
}


const char* toString(Type* type) {
  return type->getValType()->symbol->name;
}


const char* toString(CallInfo* info) {
  bool method = false;
  bool _this = false;
  const char *str = "";
  if (info->actuals.n > 1)
    if (info->actuals.v[0]->type == dtMethodToken)
      method = true;
  if (!strcmp("this", info->name)) {
    _this = true;
    method = false;
  }
  if (method) {
    if (info->actuals.v[1] && info->actuals.v[1]->hasFlag(FLAG_TYPE_VARIABLE))
      str = astr(str, "type ", toString(info->actuals.v[1]->type), ".");
    else
      str = astr(str, toString(info->actuals.v[1]->type), ".");
  }
  if (!strncmp("_type_construct_", info->name, 16)) {
    str = astr(str, info->name+16);
  } else if (!strncmp("_construct_", info->name, 11)) {
    str = astr(str, info->name+11);
  } else if (!_this) {
    str = astr(str, info->name);
  }
  if (!info->call->methodTag) {
    if (info->call->square)
      str = astr(str, "[");
    else
      str = astr(str, "(");
  }
  bool first = false;
  int start = 0;
  if (method)
    start = 2;
  if (_this)
    start = 2;
  for (int i = start; i < info->actuals.n; i++) {
    if (!first)
      first = true;
    else
      str = astr(str, ", ");
    if (info->actualNames.v[i])
      str = astr(str, info->actualNames.v[i], "=");
    VarSymbol* var = toVarSymbol(info->actuals.v[i]);
    if (info->actuals.v[i]->type->symbol->hasFlag(FLAG_ITERATOR_RECORD) &&
        info->actuals.v[i]->type->defaultConstructor->hasFlag(FLAG_PROMOTION_WRAPPER))
      str = astr(str, "promoted expression");
    else if (info->actuals.v[i] && info->actuals.v[i]->hasFlag(FLAG_TYPE_VARIABLE))
      str = astr(str, "type ", toString(info->actuals.v[i]->type));
    else if (var && var->immediate) {
      if (var->immediate->const_kind == CONST_KIND_STRING) {
        str = astr(str, "\"", var->immediate->v_string, "\"");
      } else {
        const size_t bufSize = 512;
        char buff[bufSize];
        snprint_imm(buff, bufSize, *var->immediate);
        str = astr(str, buff);
      }
    } else
      str = astr(str, toString(info->actuals.v[i]->type));
  }
  if (!info->call->methodTag) {
    if (info->call->square)
      str = astr(str, "]");
    else
      str = astr(str, ")");
  }
  return str;
}


const char* toString(FnSymbol* fn) {
  if (fn->userString) {
    if (developer)
      return astr(fn->userString, " [", istr(fn->id), "]");
    else
      return fn->userString;
  }
  const char* str;
  int start = 0;
 if (developer) {
   // report the name as-is and include all args
   str = fn->name;
 } else {
  if (fn->instantiatedFrom)
    fn = fn->instantiatedFrom;
  if (fn->hasFlag(FLAG_TYPE_CONSTRUCTOR)) {
    // if not, make sure 'str' is built as desired
    INT_ASSERT(!strncmp("_type_construct_", fn->name, 16));
    str = astr(fn->name+16);
  } else if (fn->hasFlag(FLAG_CONSTRUCTOR)) {
    INT_ASSERT(!strncmp("_construct_", fn->name, 11));
    str = astr(fn->name+11);
  } else if (fn->hasFlag(FLAG_METHOD)) {
    if (!strcmp(fn->name, "this")) {
      str = astr(toString(fn->getFormal(2)->type));
      start = 1;
    } else {
      str = astr(toString(fn->getFormal(2)->type), ".", fn->name);
      start = 2;
    }
  } else if (fn->hasFlag(FLAG_MODULE_INIT)) {
    INT_ASSERT(!strncmp("chpl__init_", fn->name, 11)); //if not, fix next line
    str = astr("top-level module statements for ", fn->name+11);
  } else
    str = astr(fn->name);
 } // if developer

  bool skipParens =
    fn->hasFlag(FLAG_NO_PARENS) ||
    (fn->hasFlag(FLAG_TYPE_CONSTRUCTOR) && fn->numFormals() == 0) ||
    (fn->hasFlag(FLAG_MODULE_INIT) && !developer);

  if (!skipParens)
    str = astr(str, "(");
  bool first = false;
  for (int i = start; i < fn->numFormals(); i++) {
    ArgSymbol* arg = fn->getFormal(i+1);
    if (arg->hasFlag(FLAG_IS_MEME))
      continue;
    if (!first) {
      first = true;
      if (skipParens)
        str = astr(str, " ");
    } else
      str = astr(str, ", ");
    if (arg->intent == INTENT_PARAM)
      str = astr(str, "param ");
    if (arg->hasFlag(FLAG_TYPE_VARIABLE))
      str = astr(str, "type ", arg->name);
    else if (arg->type == dtUnknown) {
      SymExpr* sym = NULL;
      if (arg->typeExpr)
        sym = toSymExpr(arg->typeExpr->body.tail);
      if (sym)
        str = astr(str, arg->name, ": ", sym->var->name);
      else
        str = astr(str, arg->name);
    } else if (arg->type == dtAny) {
      str = astr(str, arg->name);
    } else
      str = astr(str, arg->name, ": ", toString(arg->type));
    if (arg->variableExpr)
      str = astr(str, " ...");
  }
  if (!skipParens)
    str = astr(str, ")");
  if (developer)
    str = astr(str, " [", istr(fn->id), "]");
  return str;
}


static void
checkResolveRemovedPrims(void) {
  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->primitive) {
      switch(call->primitive->tag) {
        case PRIM_INIT:
        case PRIM_LOGICAL_FOLDER:
        case PRIM_TYPEOF:
        case PRIM_TYPE_TO_STRING:
        case PRIM_IS_STAR_TUPLE_TYPE:
        case PRIM_ISSUBTYPE:
        case PRIM_TUPLE_EXPAND:
        case PRIM_QUERY:
        case PRIM_ERROR:
          if (call->parentSymbol)
            INT_FATAL("Primitive should no longer be in AST");
          break;
        default:
          break;
      }
    }
  }
}


static FnSymbol*
protoIteratorMethod(IteratorInfo* ii, const char* name, Type* retType) {
  FnSymbol* fn = new FnSymbol(name);
  fn->addFlag(FLAG_AUTO_II); 
  if (strcmp(name, "advance"))
    fn->addFlag(FLAG_INLINE);
  fn->insertFormalAtTail(new ArgSymbol(INTENT_BLANK, "_mt", dtMethodToken));
  fn->_this = new ArgSymbol(INTENT_BLANK, "this", ii->iclass);
  fn->retType = retType;
  fn->insertFormalAtTail(fn->_this);
  ii->iterator->defPoint->insertBefore(new DefExpr(fn));
  return fn;
}


static void
protoIteratorClass(FnSymbol* fn) {
  INT_ASSERT(!fn->iteratorInfo);

  SET_LINENO(fn);

  IteratorInfo* ii = new IteratorInfo();
  fn->iteratorInfo = ii;
  fn->iteratorInfo->iterator = fn;

  const char* className = astr(fn->name);
  if (fn->_this)
    className = astr(className, "_", fn->_this->type->symbol->cname);
  ii->iclass = new ClassType(CLASS_CLASS);
  TypeSymbol* cts = new TypeSymbol(astr("_ic_", className), ii->iclass);
  cts->addFlag(FLAG_ITERATOR_CLASS);
  fn->defPoint->insertBefore(new DefExpr(cts));
  ii->irecord = new ClassType(CLASS_RECORD);
  TypeSymbol* rts = new TypeSymbol(astr("_ir_", className), ii->irecord);
  rts->addFlag(FLAG_ITERATOR_RECORD);
  if (fn->retTag == RET_VAR)
    rts->addFlag(FLAG_REF_ITERATOR_CLASS);
  fn->defPoint->insertBefore(new DefExpr(rts));

  ii->tag = it_iterator;
  ii->advance = protoIteratorMethod(ii, "advance", dtVoid);
  ii->zip1 = protoIteratorMethod(ii, "zip1", dtVoid);
  ii->zip2 = protoIteratorMethod(ii, "zip2", dtVoid);
  ii->zip3 = protoIteratorMethod(ii, "zip3", dtVoid);
  ii->zip4 = protoIteratorMethod(ii, "zip4", dtVoid);
  ii->hasMore = protoIteratorMethod(ii, "hasMore", dtInt[INT_SIZE_32]);
  ii->getValue = protoIteratorMethod(ii, "getValue", fn->retType);

  ii->irecord->defaultConstructor = fn;
  ii->irecord->scalarPromotionType = fn->retType;
  fn->retType = ii->irecord;
  fn->retTag = RET_VALUE;

  makeRefType(fn->retType);

  resolvedFns.put(fn->iteratorInfo->zip1, true);
  resolvedFns.put(fn->iteratorInfo->zip2, true);
  resolvedFns.put(fn->iteratorInfo->zip3, true);
  resolvedFns.put(fn->iteratorInfo->zip4, true);
  resolvedFns.put(fn->iteratorInfo->advance, true);
  resolvedFns.put(fn->iteratorInfo->hasMore, true);
  resolvedFns.put(fn->iteratorInfo->getValue, true);

  ii->getIterator = new FnSymbol("_getIterator");
  ii->getIterator->addFlag(FLAG_AUTO_II);
  ii->getIterator->addFlag(FLAG_INLINE);
  ii->getIterator->retType = ii->iclass;
  ii->getIterator->insertFormalAtTail(new ArgSymbol(INTENT_BLANK, "ir", ii->irecord));
  VarSymbol* ret = newTemp("ic", ii->iclass);
  ii->getIterator->insertAtTail(new DefExpr(ret));
  ii->getIterator->insertAtTail(new CallExpr(PRIM_MOVE, ret, new CallExpr(PRIM_CHPL_ALLOC, ii->iclass->symbol, newMemDesc("iterator data"))));
  ii->getIterator->insertAtTail(new CallExpr(PRIM_SETCID, ret));
  ii->getIterator->insertAtTail(new CallExpr(PRIM_RETURN, ret));
  fn->defPoint->insertBefore(new DefExpr(ii->getIterator));
  ii->iclass->defaultConstructor = ii->getIterator;
  resolvedFns.put(ii->getIterator, true);
}


//
// returns true if the field was instantiated
//
static bool
isInstantiatedField(Symbol* field) {
  TypeSymbol* ts = toTypeSymbol(field->defPoint->parentSymbol);
  INT_ASSERT(ts);
  ClassType* ct = toClassType(ts->type);
  INT_ASSERT(ct);
  for_formals(formal, ct->defaultTypeConstructor) {
    if (!strcmp(field->name, formal->name))
      if (formal->hasFlag(FLAG_TYPE_VARIABLE))
        return true;
  }
  return false;
}


//
// determine field associated with query expression
//
static Symbol*
determineQueriedField(CallExpr* call) {
  ClassType* ct = toClassType(call->get(1)->getValType());
  INT_ASSERT(ct);
  SymExpr* last = toSymExpr(call->get(call->numActuals()));
  INT_ASSERT(last);
  VarSymbol* var = toVarSymbol(last->var);
  INT_ASSERT(var && var->immediate);
  if (var->immediate->const_kind == CONST_KIND_STRING) {
    // field queried by name
    return ct->getField(var->immediate->v_string, false);
  } else {
    // field queried by position
    int position = var->immediate->int_value();
    Vec<ArgSymbol*> args;
    for_formals(arg, ct->defaultTypeConstructor) {
      args.add(arg);
    }
    for (int i = 2; i < call->numActuals(); i++) {
      SymExpr* actual = toSymExpr(call->get(i));
      INT_ASSERT(actual);
      VarSymbol* var = toVarSymbol(actual->var);
      INT_ASSERT(var && var->immediate && var->immediate->const_kind == CONST_KIND_STRING);
      for (int j = 0; j < args.n; j++) {
        if (args.v[j] && !strcmp(args.v[j]->name, var->immediate->v_string))
          args.v[j] = NULL;
      }
    }
    forv_Vec(ArgSymbol, arg, args) {
      if (arg) {
        if (position == 1)
          return ct->getField(arg->name, false);
        position--;
      }
    }
  }
  return NULL;
}


static void
resolveSpecifiedReturnType(FnSymbol* fn) {
  resolveBlock(fn->retExprType);
  fn->retType = fn->retExprType->body.tail->typeInfo();
  if (fn->retType != dtUnknown) {
    if (fn->retTag == RET_VAR) {
      makeRefType(fn->retType);
      fn->retType = fn->retType->refType;
    }
    fn->retExprType->remove();
    if (fn->hasFlag(FLAG_ITERATOR_FN) && !fn->iteratorInfo) {
      protoIteratorClass(fn);
    }
  }
}


void
resolveFormals(FnSymbol* fn) {
  static Vec<FnSymbol*> done;

  if (!fn->hasFlag(FLAG_GENERIC)) {
    if (done.set_in(fn))
      return;
    done.set_add(fn);

    for_formals(formal, fn) {
      if (formal->type == dtUnknown) {
        if (!formal->typeExpr) {
          formal->type = dtObject;
        } else {
          resolveBlock(formal->typeExpr);
          formal->type = formal->typeExpr->body.tail->getValType();
        }
      }

      //
      // change type of this on record methods to reference type
      //
      if (!formal->type->symbol->hasFlag(FLAG_REF) &&
          (formal->intent == INTENT_INOUT ||
           formal->intent == INTENT_OUT ||
           formal->hasFlag(FLAG_WRAP_OUT_INTENT) ||
           (formal == fn->_this &&
            (isUnion(formal->type) ||
             isRecord(formal->type) || fn->hasFlag(FLAG_REF_THIS))))) {
        makeRefType(formal->type);
        formal->type = formal->type->refType;
      }
    }
    if (fn->retExprType)
      resolveSpecifiedReturnType(fn);

    resolvedFormals.set_add(fn);
  }
}

static bool
fits_in_int(int width, Immediate* imm) {
  if (imm->const_kind == NUM_KIND_INT && imm->num_index == INT_SIZE_32) {
    int64_t i = imm->int_value();
    switch (width) {
    default: INT_FATAL("bad width in fits_in_int");
    case 8:
      return (i >= -128 && i <= 127);
    case 16:
      return (i >= -32768 && i <= 32767);
    case 32:
      return (i >= -2147483648ll && i <= 2147483647ll);
    case 64:
      return (i >= -9223372036854775807ll-1 && i <= 9223372036854775807ll);
    }
  }
  return false;
}

static bool
fits_in_uint(int width, Immediate* imm) {
  if (imm->const_kind == NUM_KIND_INT && imm->num_index == INT_SIZE_32) {
    int64_t i = imm->int_value();
    if (i < 0)
      return false;
    uint64_t u = (uint64_t)i;
    switch (width) {
    default: INT_FATAL("bad width in fits_in_uint");
    case 8:
      return (u <= 255);
    case 16:
      return (u <= 65535);
    case 32:
      return (u <= 2147483647ull);
    case 64:
      return true;
    }
  } else if (imm->const_kind == NUM_KIND_INT && imm->num_index == INT_SIZE_64) {
    int64_t i = imm->int_value();
    if (i > 0 && width == 64)
      return true;
  }
  return false;
}


// Returns true iff dispatching the actualType to the formalType
// results in an instantiation.
static bool
canInstantiate(Type* actualType, Type* formalType) {
  if (actualType == dtMethodToken)
    return false;
  if (formalType == dtAny)
    return true;
  if (formalType == dtIntegral && (is_int_type(actualType) || is_uint_type(actualType)))
    return true;
  if (formalType == dtAnyEnumerated && (is_enum_type(actualType)))
    return true;
  if (formalType == dtNumeric &&
      (is_int_type(actualType) || is_uint_type(actualType) || is_imag_type(actualType) ||
       is_real_type(actualType) || is_complex_type(actualType)))
    return true;
  if (formalType == dtIteratorRecord && actualType->symbol->hasFlag(FLAG_ITERATOR_RECORD))
    return true;
  if (formalType == dtIteratorClass && actualType->symbol->hasFlag(FLAG_ITERATOR_CLASS))
    return true;
  if (actualType == formalType)
    return true;
  if (actualType->instantiatedFrom && canInstantiate(actualType->instantiatedFrom, formalType))
    return true;
  return false;
}


//
// returns true if dispatching from actualType to formalType results
// in a compile-time coercion; this is a subset of canCoerce below as,
// for example, real(32) cannot be coerced to real(64) at compile-time
//
static bool canParamCoerce(Type* actualType, Symbol* actualSym, Type* formalType) {
  if (is_bool_type(formalType) && is_bool_type(actualType))
    return true;
  if (is_int_type(formalType)) {
    if (is_bool_type(actualType))
      return true;
    if (is_int_type(actualType) &&
        get_width(actualType) < get_width(formalType))
      return true;
    if (is_uint_type(actualType) &&
        get_width(actualType) < get_width(formalType))
      return true;
    if (get_width(formalType) < 64)
      if (VarSymbol* var = toVarSymbol(actualSym))
        if (var->immediate)
          if (fits_in_int(get_width(formalType), var->immediate))
            return true;
    if (toEnumType(actualType))
      return true;
  }
  if (is_uint_type(formalType)) {
    if (is_bool_type(actualType))
      return true;
    if (is_uint_type(actualType) &&
        get_width(actualType) < get_width(formalType))
      return true;
    if (VarSymbol* var = toVarSymbol(actualSym))
      if (var->immediate)
        if (fits_in_uint(get_width(formalType), var->immediate))
          return true;
  }
  return false;
}


//
// returns true iff dispatching the actualType to the formalType
// results in a coercion.
//
bool
canCoerce(Type* actualType, Symbol* actualSym, Type* formalType, FnSymbol* fn, bool* promotes) {
  if (canParamCoerce(actualType, actualSym, formalType))
    return true;
  if (is_real_type(formalType)) {
    if ((is_int_type(actualType) || is_uint_type(actualType))
        && get_width(formalType) >= 64)
      return true;
    if (is_real_type(actualType) && 
        get_width(actualType) < get_width(formalType))
      return true;
  }
  if (is_complex_type(formalType)) {
    if ((is_int_type(actualType) || is_uint_type(actualType))
        && get_width(formalType) >= 128)
      return true;
    if (is_real_type(actualType) && 
        (get_width(actualType) <= get_width(formalType)/2))
      return true;
    if (is_imag_type(actualType) && 
        (get_width(actualType) <= get_width(formalType)/2))
      return true;
    if (is_complex_type(actualType) && 
        (get_width(actualType) < get_width(formalType)))
      return true;
  }
  if (actualType->symbol->hasFlag(FLAG_SYNC)) {
    Type* baseType = actualType->getField("base_type")->type;
    return canDispatch(baseType, NULL, formalType, fn, promotes);
  }
  if (actualType->symbol->hasFlag(FLAG_REF))
    return canDispatch(actualType->getValType(), NULL, formalType, fn, promotes);
  return false;
}

// Returns true iff the actualType can dispatch to the formalType.
// The function symbol is used to avoid scalar promotion on =.
// param is set if the actual is a parameter (compile-time constant).
bool
canDispatch(Type* actualType, Symbol* actualSym, Type* formalType, FnSymbol* fn, bool* promotes, bool paramCoerce) {
  if (promotes)
    *promotes = false;
  if (actualType == formalType)
    return true;
  if (actualType == dtNil && isClass(formalType))
    return true;
  if (actualType->refType == formalType)
    return true;
  if (!paramCoerce && canCoerce(actualType, actualSym, formalType, fn, promotes))
    return true;
  if (paramCoerce && canParamCoerce(actualType, actualSym, formalType))
    return true;
  forv_Vec(Type, parent, actualType->dispatchParents) {
    if (parent == formalType || canDispatch(parent, NULL, formalType, fn, promotes)) {
      return true;
    }
  }
  if (fn &&
      strcmp(fn->name, "=") && 
      actualType->scalarPromotionType && 
      (canDispatch(actualType->scalarPromotionType, NULL, formalType, fn))) {
    if (promotes)
      *promotes = true;
    return true;
  }
  return false;
}

bool
isDispatchParent(Type* t, Type* pt) {
  forv_Vec(Type, p, t->dispatchParents)
    if (p == pt || isDispatchParent(p, pt))
      return true;
  return false;
}

static bool
moreSpecific(FnSymbol* fn, Type* actualType, Type* formalType) {
  if (canDispatch(actualType, NULL, formalType, fn))
    return true;
  if (canInstantiate(actualType, formalType)) {
    return true;
  }
  return false;
}

static bool
computeActualFormalMap(FnSymbol* fn,
                       Vec<Symbol*>& formalActuals,
                       Vec<ArgSymbol*>& actualFormals,
                       CallInfo& info) {
  formalActuals.fill(fn->numFormals());
  actualFormals.fill(info.actuals.n);
  for (int i = 0; i < actualFormals.n; i++) {
    if (info.actualNames.v[i]) {
      bool match = false;
      int j = 0;
      for_formals(formal, fn) {
        if (!strcmp(info.actualNames.v[i], formal->name)) {
          match = true;
          actualFormals.v[i] = formal;
          formalActuals.v[j] = info.actuals.v[i];
          break;
        }
        j++;
      }
      if (!match)
        return false;
    }
  }
  int j = 0;
  ArgSymbol* formal = (fn->numFormals()) ? fn->getFormal(1) : NULL;
  for (int i = 0; i < actualFormals.n; i++) {
    if (!info.actualNames.v[i]) {
      bool match = false;
      while (formal) {
        if (formal->variableExpr)
          return (fn->hasFlag(FLAG_GENERIC)) ? true : false;
        if (!formalActuals.v[j]) {
          match = true;
          actualFormals.v[i] = formal;
          formalActuals.v[j] = info.actuals.v[i];
          break;
        }
        formal = next_formal(formal);
        j++;
      }
      if (!match && !(fn->hasFlag(FLAG_GENERIC) && fn->hasFlag(FLAG_TUPLE)))
        return false;
    }
  }
  while (formal) {
    if (!formalActuals.v[j] && !formal->defaultExpr)
      return false;
    formal = next_formal(formal);
    j++;
  }
  return true;
}


//
// returns the type that a formal type should be instantiated to when
// instantiated by a given actual type
//
static Type*
getInstantiationType(Type* actualType, Type* formalType) {
  if (canInstantiate(actualType, formalType)) {
    return actualType;
  }
  if (Type* st = actualType->scalarPromotionType) {
    if (canInstantiate(st, formalType))
      return st;
  }
  if (Type* vt = actualType->getValType()) {
    if (canInstantiate(vt, formalType))
      return vt;
    else if (Type* st = vt->scalarPromotionType)
      if (canInstantiate(st, formalType))
        return st;
  }
  return NULL;
}


static void
computeGenericSubs(SymbolMap &subs,
                   FnSymbol* fn,
                   Vec<Symbol*>* formalActuals) {
  int i = 0;
  for_formals(formal, fn) {
    if (formal->intent == INTENT_PARAM) {
      if (formalActuals->v[i] && formalActuals->v[i]->isParameter()) {
        if (!formal->type->symbol->hasFlag(FLAG_GENERIC) ||
            canInstantiate(formalActuals->v[i]->type, formal->type))
          subs.put(formal, formalActuals->v[i]);
      } else if (!formalActuals->v[i] && formal->defaultExpr) {

        // break because default expression may reference generic
        // arguments earlier in formal list; make those substitutions
        // first (test/classes/bradc/paramInClass/weirdParamInit4)
        if (subs.n)
          break;

        resolveBlock(formal->defaultExpr);
        SymExpr* se = toSymExpr(formal->defaultExpr->body.tail);
        if (se && se->var->isParameter() &&
            (!formal->type->symbol->hasFlag(FLAG_GENERIC) || canInstantiate(se->var->type, formal->type)))
          subs.put(formal, se->var);
        else
          INT_FATAL(fn, "unable to handle default parameter");
      }
    } else if (formal->type->symbol->hasFlag(FLAG_GENERIC)) {

      //
      // check for field with specified generic type
      //
      if (!formal->hasFlag(FLAG_TYPE_VARIABLE) && formal->type != dtAny &&
          strcmp(formal->name, "outer") && strcmp(formal->name, "meme") &&
          (fn->hasFlag(FLAG_DEFAULT_CONSTRUCTOR) || fn->hasFlag(FLAG_TYPE_CONSTRUCTOR)))
        USR_FATAL(formal, "invalid generic type specification on class field");

      if (formalActuals->v[i]) {
        if (Type* type = getInstantiationType(formalActuals->v[i]->type, formal->type))
          subs.put(formal, type->symbol);
      } else if (formal->defaultExpr) {

        // break because default expression may reference generic
        // arguments earlier in formal list; make those substitutions
        // first (test/classes/bradc/genericTypes)
        if (subs.n)
          break;

        resolveBlock(formal->defaultExpr);
        Type* defaultType = formal->defaultExpr->body.tail->typeInfo();
        if (defaultType == dtTypeDefaultToken)
          subs.put(formal, dtTypeDefaultToken->symbol);
        else if (Type* type = getInstantiationType(defaultType, formal->type))
          subs.put(formal, type->symbol);
      }
    }
    i++;
  }
}


static FnSymbol*
expandVarArgs(FnSymbol* fn, int numActuals) {
  static Map<FnSymbol*,Vec<FnSymbol*>*> cache;

  bool genericArg = false;
  for_formals(arg, fn) {
    if (!genericArg && arg->variableExpr && !isDefExpr(arg->variableExpr->body.tail))
      resolveBlock(arg->variableExpr);

    //
    // set genericArg to true if a generic argument appears before the
    // argument with the variable expression
    //
    if (arg->type->symbol->hasFlag(FLAG_GENERIC))
      genericArg = true;

    if (!arg->variableExpr)
      continue;

    // handle unspecified variable number of arguments
    if (DefExpr* def = toDefExpr(arg->variableExpr->body.tail)) {

      // check for cached stamped out function
      if (Vec<FnSymbol*>* cfns = cache.get(fn)) {
        forv_Vec(FnSymbol, cfn, *cfns) {
          if (cfn->numFormals() == numActuals)
            return cfn;
        }
      }

      int numCopies = numActuals - fn->numFormals() + 1;
      if (numCopies <= 0)
        return NULL;

      SymbolMap map;
      FnSymbol* newFn = fn->copy(&map);
      newFn->addFlag(FLAG_INVISIBLE_FN);
      fn->defPoint->insertBefore(new DefExpr(newFn));
      Symbol* sym = map.get(def->sym);
      sym->defPoint->replace(new SymExpr(new_IntSymbol(numCopies)));

      subSymbol(newFn, sym, new_IntSymbol(numCopies));

      // add new function to cache
      Vec<FnSymbol*>* cfns = cache.get(fn);
      if (!cfns)
        cfns = new Vec<FnSymbol*>();
      cfns->add(newFn);
      cache.put(fn, cfns);

      return expandVarArgs(newFn, numActuals);
    } else if (SymExpr* sym = toSymExpr(arg->variableExpr->body.tail)) {

      // handle specified number of variable arguments
      if (VarSymbol* n_var = toVarSymbol(sym->var)) {
        if (n_var->type == dtInt[INT_SIZE_32] && n_var->immediate) {
          int n = n_var->immediate->int_value();
          CallExpr* tupleCall = new CallExpr((arg->hasFlag(FLAG_TYPE_VARIABLE)) ?
                                             "_type_construct__tuple" : "_construct__tuple");
          for (int i = 0; i < n; i++) {
            DefExpr* new_arg_def = arg->defPoint->copy();
            ArgSymbol* new_arg = toArgSymbol(new_arg_def->sym);
            new_arg->variableExpr = NULL;
            tupleCall->insertAtTail(new SymExpr(new_arg));
            new_arg->name = astr("_e", istr(i), "_", arg->name);
            new_arg->cname = astr("_e", istr(i), "_", arg->cname);
            arg->defPoint->insertBefore(new_arg_def);
          }
          VarSymbol* var = new VarSymbol(arg->name);
          if (arg->hasFlag(FLAG_TYPE_VARIABLE))
            var->addFlag(FLAG_TYPE_VARIABLE);

          if (arg->intent == INTENT_OUT || arg->intent == INTENT_INOUT) {
            int i = 1;
            for_actuals(actual, tupleCall) {
              VarSymbol* tmp = newTemp();
              fn->insertBeforeReturnAfterLabel(new DefExpr(tmp));
              fn->insertBeforeReturnAfterLabel(new CallExpr(PRIM_MOVE, tmp, new CallExpr(var, new_IntSymbol(i))));
              fn->insertBeforeReturnAfterLabel(new CallExpr(PRIM_MOVE, actual->copy(),
                                                            new CallExpr("=", actual->copy(), tmp)));
              i++;
            }
          }

          tupleCall->insertAtHead(new_IntSymbol(n));
          fn->insertAtHead(new CallExpr(PRIM_MOVE, var, tupleCall));
          fn->insertAtHead(new DefExpr(var));
          arg->defPoint->remove();
          subSymbol(fn->body, arg, var);
          if (fn->where) {
            VarSymbol* var = new VarSymbol(arg->name);
            if (arg->hasFlag(FLAG_TYPE_VARIABLE))
              var->addFlag(FLAG_TYPE_VARIABLE);
            fn->where->insertAtHead(new CallExpr(PRIM_MOVE, var, tupleCall->copy()));
            fn->where->insertAtHead(new DefExpr(var));
            subSymbol(fn->where, arg, var);
          }
        }
      }
      
    } else if (!fn->hasFlag(FLAG_GENERIC))
      INT_FATAL("bad variableExpr");
  }
  return fn;
}


// Return actual-formal map if FnSymbol is viable candidate to call
static void
addCandidate(Vec<FnSymbol*>* candidateFns,
             Vec<Vec<ArgSymbol*>*>* candidateActualFormals,
             FnSymbol* fn,
             CallInfo& info) {
  fn = expandVarArgs(fn, info.actuals.n);

  if (!fn)
    return;

  Vec<ArgSymbol*> actualFormals;
  Vec<Symbol*> formalActuals;

  bool valid = computeActualFormalMap(fn, formalActuals, actualFormals, info);

  if (!valid)
    return;

  if (fn->hasFlag(FLAG_GENERIC)) {

    //
    // try to avoid excessive over-instantiation
    //
    int i = 0;
    for_formals(formal, fn) {
      if (formal->type != dtUnknown) {
        if (Symbol* actual = formalActuals.v[i]) {
          if (actual->hasFlag(FLAG_TYPE_VARIABLE) != formal->hasFlag(FLAG_TYPE_VARIABLE))
            return;
          if (formal->type->symbol->hasFlag(FLAG_GENERIC)) {
            Type* vt = actual->getValType();
            Type* st = actual->type->scalarPromotionType;
            Type* svt = (vt) ? vt->scalarPromotionType : NULL;
            if (!canInstantiate(actual->type, formal->type) &&
                (!vt || !canInstantiate(vt, formal->type)) &&
                (!st || !canInstantiate(st, formal->type)) &&
                (!svt || !canInstantiate(svt, formal->type)))
              return;
          } else {
            if (!canDispatch(actual->type, actual, formal->type, fn, NULL, formal->instantiatedParam))
              return;
          }
        }
      }
      i++;
    }

    SymbolMap subs;
    computeGenericSubs(subs, fn, &formalActuals);
    if (subs.n) {
      if (FnSymbol* ifn = instantiate(fn, &subs, info.call))
        addCandidate(candidateFns, candidateActualFormals, ifn, info);
    }
    return;
  }

  //
  // make sure that type constructor is resolved before other constructors
  //
  if (fn->hasFlag(FLAG_DEFAULT_CONSTRUCTOR)) {
    CallExpr* typeConstructorCall = new CallExpr(astr("_type", fn->name));
    for_formals(formal, fn) {
      if (strcmp(formal->name, "meme")) {
        if (fn->_this->type->symbol->hasFlag(FLAG_TUPLE)) {
          if (formal->instantiatedFrom) {
            typeConstructorCall->insertAtTail(formal->type->symbol);
          } else if (formal->instantiatedParam) {
            typeConstructorCall->insertAtTail(paramMap.get(formal));
          }
        } else {
          if (!strcmp(formal->name, "outer") || formal->type == dtMethodToken) {
            typeConstructorCall->insertAtTail(formal);
          } else if (formal->instantiatedFrom) {
            typeConstructorCall->insertAtTail(new NamedExpr(formal->name, new SymExpr(formal->type->symbol)));
          } else if (formal->instantiatedParam) {
            typeConstructorCall->insertAtTail(new NamedExpr(formal->name, new SymExpr(paramMap.get(formal))));
          }
        }
      }
    }
    info.call->insertBefore(typeConstructorCall);
    resolveCall(typeConstructorCall);
    INT_ASSERT(typeConstructorCall->isResolved());
    resolveFns(typeConstructorCall->isResolved());
    fn->_this->type = typeConstructorCall->isResolved()->retType;
    typeConstructorCall->remove();
  }

  resolveFormals(fn);

  if (!strcmp(fn->name, "=")) {
    Symbol* actual = formalActuals.v[0];
    Symbol* formal = fn->getFormal(1);
    if (actual->type != formal->type &&
        actual->type != formal->type->refType)
      return;
  }

  int j = 0;
  for_formals(formal, fn) {
    if (Symbol* actual = formalActuals.v[j]) {
      if (!canDispatch(actual->type, actual, formal->type, fn, NULL, formal->instantiatedParam))
        return;
      if (actual->hasFlag(FLAG_TYPE_VARIABLE) != formal->hasFlag(FLAG_TYPE_VARIABLE))
        return;
    }
    j++;
  }
  candidateFns->add(fn);
  Vec<ArgSymbol*>* actualFormalsCopy = new Vec<ArgSymbol*>(actualFormals);
  candidateActualFormals->add(actualFormalsCopy);
}


static BlockStmt*
getParentBlock(Expr* expr) {
  for (Expr* tmp = expr->parentExpr; tmp; tmp = tmp->parentExpr) {
    if (BlockStmt* block = toBlockStmt(tmp))
      return block;
  }
  if (expr->parentSymbol) {
    FnSymbol* parentFn = toFnSymbol(expr->parentSymbol);
    if (parentFn && parentFn->instantiationPoint)
      return parentFn->instantiationPoint;
    else if (expr->parentSymbol->defPoint)
      return getParentBlock(expr->parentSymbol->defPoint);
  }
  return NULL;
}


//
// helper routine for isMoreVisible (below);
//
static bool
isMoreVisibleInternal(BlockStmt* block, FnSymbol* fn1, FnSymbol* fn2,
                      Vec<BlockStmt*>& visited) {
  //
  // fn1 is more visible
  //
  if (fn1->defPoint->parentExpr == block)
    return true;

  //
  // fn2 is more visible
  //
  if (fn2->defPoint->parentExpr == block)
    return false;

  visited.set_add(block);

  //
  // default to true if neither are visible
  //
  bool moreVisible = true;

  //
  // ensure f2 is not more visible via parent block, and recurse
  //
  if (BlockStmt* parentBlock = getParentBlock(block))
    if (!visited.set_in(parentBlock))
      moreVisible &= isMoreVisibleInternal(parentBlock, fn1, fn2, visited);

  //
  // ensure f2 is not more visible via module uses, and recurse
  //
  if (block && block->modUses) {
    for_actuals(expr, block->modUses) {
      SymExpr* se = toSymExpr(expr);
      INT_ASSERT(se);
      ModuleSymbol* mod = toModuleSymbol(se->var);
      INT_ASSERT(mod);
      if (!visited.set_in(mod->block))
        moreVisible &= isMoreVisibleInternal(mod->block, fn1, fn2, visited);
    }
  }

  return moreVisible;
}


//
// return true if fn1 is more visible than fn2 from expr
//
// assumption: fn1 and fn2 are visible from expr; if this assumption
//             is violated, this function will return true
//
static bool
isMoreVisible(Expr* expr, FnSymbol* fn1, FnSymbol* fn2) {
  //
  // common-case check to see if functions have equal visibility
  //
  if (fn1->defPoint->parentExpr == fn2->defPoint->parentExpr)
    return false;

  //
  // call helper function with visited set to avoid infinite recursion
  //
  Vec<BlockStmt*> visited;
  BlockStmt* block = toBlockStmt(expr);
  if (!block)
    block = getParentBlock(expr);
  return isMoreVisibleInternal(block, fn1, fn2, visited);
}


static FnSymbol*
disambiguate_by_match(Vec<FnSymbol*>* candidateFns,
                      Vec<Vec<ArgSymbol*>*>* candidateActualFormals,
                      Vec<Symbol*>* actuals,
                      Vec<ArgSymbol*>** ret_afs,
                      Expr* scope) {
  for (int i = 0; i < candidateFns->n; i++) {
    FnSymbol* fn1 = candidateFns->v[i];
    Vec<ArgSymbol*>* actualFormals1 = candidateActualFormals->v[i];
    bool best = true; // is fn1 the best candidate?
    for (int j = 0; j < candidateFns->n; j++) {
      if (i != j) {
        FnSymbol* fn2 = candidateFns->v[j];
        bool worse = false; // is fn1 worse than fn2?
        bool equal = true;  // is fn1 as good as fn2?
        bool fnPromotes1 = false; // does fn1 require promotion?
        bool fnPromotes2 = false; // does fn2 require promotion?
        Vec<ArgSymbol*>* actualFormals2 = candidateActualFormals->v[j];
        for (int k = 0; k < actualFormals1->n; k++) {
          Symbol* actual = actuals->v[k];

          ArgSymbol* arg = actualFormals1->v[k];
          bool argPromotes1;
          canDispatch(actual->type, actual, arg->type, fn1, &argPromotes1);
          fnPromotes1 |= argPromotes1;

          ArgSymbol* arg2 = actualFormals2->v[k];
          bool argPromotes2;
          canDispatch(actual->type, actual, arg2->type, fn1, &argPromotes2);
          fnPromotes2 |= argPromotes2;

          if (arg->type == arg2->type && arg->instantiatedParam && !arg2->instantiatedParam)
            equal = false;
          else if (arg->type == arg2->type && !arg->instantiatedParam && arg2->instantiatedParam)
            worse = true;
          else if (!argPromotes1 && argPromotes2)
            equal = false;
          else if (argPromotes1 && !argPromotes2)
            worse = true;
          else if (arg->type == arg2->type && !arg->instantiatedFrom && arg2->instantiatedFrom)
            equal = false;
          else if (arg->type == arg2->type && arg->instantiatedFrom && !arg2->instantiatedFrom)
            worse = true;
          else if (arg->instantiatedFrom!=dtAny && arg2->instantiatedFrom==dtAny)
            equal = false;
          else if (arg->instantiatedFrom==dtAny && arg2->instantiatedFrom!=dtAny)
            worse = true;
          else if (actual->type == arg->type && actual->type != arg2->type)
            equal = false;
          else if (actual->type == arg2->type && actual->type != arg->type)
            worse = true;
          else if (moreSpecific(fn1, arg->type, arg2->type) &&
                   arg2->type != arg->type)
            equal = false;
          else if (moreSpecific(fn1, arg2->type, arg->type) && 
                   arg2->type != arg->type)
            worse = true;
          else if (is_int_type(arg->type) &&
                   is_uint_type(arg2->type))
            equal = false;
          else if (is_int_type(arg2->type) &&
                   is_uint_type(arg->type))
            worse = true;
        }
        if (!fnPromotes1 && fnPromotes2)
          continue;
        if (!worse && equal) {
          if (isMoreVisible(scope, fn1, fn2))
            equal = false;
          else if (isMoreVisible(scope, fn2, fn1))
            worse = true;
          else if (fn1->where && !fn2->where)
            equal = false;
          else if (!fn1->where && fn2->where)
            worse = true;
        }
        if (worse || equal) {
          best = false;
          break;
        }
      }
    }
    if (best) {
      *ret_afs = actualFormals1;
      return fn1;
    }
  }
  *ret_afs = NULL;
  return NULL;
}


static bool
explainCallMatch(CallExpr* call) {
  if (!call->isNamed(fExplainCall))
    return false;
  if (explainCallModule && explainCallModule != call->getModule())
    return false;
  if (explainCallLine != -1 && explainCallLine != call->lineno)
    return false;
  return true;
}


static CallExpr*
userCall(CallExpr* call) {
  if (developer)
    return call;
  if (call->getFunction()->hasFlag(FLAG_TEMP) ||
      call->getModule()->modTag == MOD_INTERNAL) {
    for (int i = callStack.n-1; i >= 0; i--) {
      if (!callStack.v[i]->getFunction()->hasFlag(FLAG_TEMP) &&
          callStack.v[i]->getModule()->modTag != MOD_INTERNAL)
        return callStack.v[i];
    }
  }
  return call;
}


static void
printResolutionError(const char* error,
                     Vec<FnSymbol*>& candidates,
                     CallInfo* info) {
  CallExpr* call = userCall(info->call);
  if (!strcmp("_cast", info->name)) {
    if (!info->actuals.v[0]->hasFlag(FLAG_TYPE_VARIABLE)) {
      USR_FATAL(call, "illegal cast to non-type",
                toString(info->actuals.v[1]->type),
                toString(info->actuals.v[0]->type));
    } else {
      USR_FATAL(call, "illegal cast from %s to %s",
                toString(info->actuals.v[1]->type),
                toString(info->actuals.v[0]->type));
    }
  } else if (info->actuals.n == 2 &&
             info->actuals.v[0]->type == dtMethodToken &&
             !strcmp("these", info->name)) {
    USR_FATAL(call, "cannot iterate over values of type %s",
              toString(info->actuals.v[1]->type));
  } else if (!strcmp("_type_construct__tuple", info->name)) {
    if (info->call->argList.length == 0)
      USR_FATAL(call, "tuple size must be specified");
    SymExpr* sym = toSymExpr(info->call->get(1));
    if (!sym || !sym->var->isParameter()) {
      USR_FATAL(call, "tuple size must be static");
    } else {
      USR_FATAL(call, "invalid tuple");
    }
  } else if (!strcmp("=", info->name)) {
    if (info->actuals.v[0] && !info->actuals.v[0]->hasFlag(FLAG_TYPE_VARIABLE) &&
        info->actuals.v[1] && info->actuals.v[1]->hasFlag(FLAG_TYPE_VARIABLE)) {
      USR_FATAL(call, "illegal assignment of type to value");
    } else if (info->actuals.v[0] && info->actuals.v[0]->hasFlag(FLAG_TYPE_VARIABLE) &&
               info->actuals.v[1] && !info->actuals.v[1]->hasFlag(FLAG_TYPE_VARIABLE)) {
      USR_FATAL(call, "illegal assignment of value to type");
    } else if (info->actuals.v[1]->type == dtNil) {
      USR_FATAL(call, "type mismatch in assignment from nil to %s",
                toString(info->actuals.v[0]->type));
    } else {
      USR_FATAL(call, "type mismatch in assignment from %s to %s",
                toString(info->actuals.v[1]->type),
                toString(info->actuals.v[0]->type));
    }
  } else if (!strcmp("this", info->name)) {
    Type* type = info->actuals.v[1]->getValType();
    if (type->symbol->hasFlag(FLAG_ITERATOR_RECORD)) {
      USR_FATAL(call, "illegal access of iterator or promoted expression");
    } else if (type->symbol->hasFlag(FLAG_FUNCTION_CLASS)) {
      USR_FATAL(call, "illegal access of first class function");
    } else {
      USR_FATAL(call, "%s access of '%s' by '%s'", error,
                toString(info->actuals.v[1]->type),
                toString(info));
    }
  } else {
    const char* entity = "call";
    if (!strncmp("_type_construct_", info->name, 16))
      entity = "type specifier";
    const char* str = toString(info);
    if (info->scope) {
      ModuleSymbol* mod = toModuleSymbol(info->scope->parentSymbol);
      INT_ASSERT(mod);
      str = astr(mod->name, ".", str);
    }
    USR_FATAL_CONT(call, "%s %s '%s'", error, entity, str);
    if (candidates.n > 0) {
      if (developer) {
        for (int i = callStack.n-1; i>=0; i--) {
          CallExpr* cs = callStack.v[i];
          FnSymbol* f = cs->getFunction();
          if (f->instantiatedFrom)
            USR_PRINT(callStack.v[i], "  instantiated from %s", f->name);
          else
            break;
        }
      }
      bool printed_one = false;
      forv_Vec(FnSymbol, fn, candidates) {
        USR_PRINT(fn, "%s %s",
                  printed_one ? "               " : "candidates are:",
                  toString(fn));
        printed_one = true;
      }
    }
    if (candidates.n == 1 &&
        candidates.v[0]->numFormals() == 0
        && !strncmp("_type_construct_", info->name, 16))
      USR_PRINT(call, "did you forget the 'new' keyword?");
    USR_STOP();
  }
}

static void issueCompilerError(CallExpr* call) {
  //
  // Disable compiler warnings in internal modules that are triggered
  // within a dynamic dispatch context because of potential user
  // confusion.  Removed the following code and See the following
  // tests:
  //
  //   test/arrays/bradc/workarounds/arrayOfSpsArray.chpl
  //   test/arrays/deitz/part4/test_array_of_associative_arrays.chpl
  //   test/classes/bradc/arrayInClass/genericArrayInClass-otharrs.chpl
  //
  if (call->isPrimitive(PRIM_WARNING))
    if (inDynamicDispatchResolution)
      if (call->getModule()->modTag == MOD_INTERNAL &&
          callStack.v[0]->getModule()->modTag == MOD_INTERNAL)
        return;
  //
  // If an errorDepth was specified, report a diagnostic about the call
  // that deep into the callStack. The default depth is 1.
  //
  FnSymbol* fn = toFnSymbol(call->parentSymbol);
  VarSymbol* depthParam = toVarSymbol(paramMap.get(toDefExpr(fn->formals.tail)->sym));
  int64_t depth;
  bool foundDepthVal;
  if (depthParam && depthParam->immediate &&
      depthParam->immediate->const_kind == NUM_KIND_INT) {
    depth = depthParam->immediate->int_value();
    foundDepthVal = true;
  } else {
    depth = 1;
    foundDepthVal = false;
  }
  if (depth+1 > callStack.n) {
    USR_WARN(call, "compiler diagnostic depth value exceeds call stack depth");
    depth = callStack.n - 1;
  }
  if (depth < 0) {
    USR_WARN(call, "compiler diagnostic depth value can not be negative");
    depth = 1;
  }
  CallExpr* from = NULL;
  for (int i = callStack.n-(1+depth); i >= 0; i--) {
    from = callStack.v[i];
    if (from->lineno > 0 && 
        from->getModule()->modTag != MOD_INTERNAL &&
        !from->getFunction()->hasFlag(FLAG_TEMP))
      break;
  }

  const char* str = "";
  for_formals(arg, fn) {
    if (foundDepthVal && arg->defPoint == fn->formals.tail)
      continue;
    VarSymbol* var = toVarSymbol(paramMap.get(arg));
    INT_ASSERT(var && var->immediate && var->immediate->const_kind == CONST_KIND_STRING);
    str = astr(str, var->immediate->v_string);
  }
  if (call->isPrimitive(PRIM_ERROR)) {
    USR_FATAL(from, "%s", str);
  } else {
    USR_WARN(from, "%s", str);
  }
  if (FnSymbol* fn = toFnSymbol(callStack.v[callStack.n-1]->isResolved()))
    innerCompilerWarningMap.put(fn, str);
  if (FnSymbol* fn = toFnSymbol(callStack.v[callStack.n-(1+depth)]->isResolved()))
    outerCompilerWarningMap.put(fn, str);
}

static void reissueCompilerWarning(const char* str, int offset) {
  //
  // Disable compiler warnings in internal modules that are triggered
  // within a dynamic dispatch context because of potential user
  // confusion.  See note in 'issueCompileError' above.
  //
  if (inDynamicDispatchResolution)
    if (callStack.v[callStack.n-1]->getModule()->modTag == MOD_INTERNAL &&
        callStack.v[0]->getModule()->modTag == MOD_INTERNAL)
      return;

  CallExpr* from = NULL;
  for (int i = callStack.n-offset; i >= 0; i--) {
    from = callStack.v[i];
    if (from->lineno > 0 && 
        from->getModule()->modTag != MOD_INTERNAL &&
        !from->getFunction()->hasFlag(FLAG_TEMP))
      break;
  }
  USR_WARN(from, "%s", str);
}

class VisibleFunctionBlock {
 public:
  Map<const char*,Vec<FnSymbol*>*> visibleFunctions;
  VisibleFunctionBlock() { }
};

static Map<BlockStmt*,VisibleFunctionBlock*> visibleFunctionMap;
static int nVisibleFunctions = 0; // for incremental build
static Map<BlockStmt*,BlockStmt*> visibilityBlockCache;
static Vec<BlockStmt*> standardModuleSet;

//
// return the innermost block for searching for visible functions
//
BlockStmt*
getVisibilityBlock(Expr* expr) {
  if (BlockStmt* block = toBlockStmt(expr->parentExpr)) {
    if (block->blockTag == BLOCK_SCOPELESS)
      return getVisibilityBlock(block);
    else
      return block;
  } else if (expr->parentExpr) {
    return getVisibilityBlock(expr->parentExpr);
  } else {
    FnSymbol* fn = toFnSymbol(expr->parentSymbol);
    if (fn && fn->instantiationPoint)
      return fn->instantiationPoint;
    else
      return getVisibilityBlock(expr->parentSymbol->defPoint);
  }
}

static void buildVisibleFunctionMap() {
  for (int i = nVisibleFunctions; i < gFnSymbols.n; i++) {
    FnSymbol* fn = gFnSymbols.v[i];
    if (!fn->hasFlag(FLAG_INVISIBLE_FN) && fn->defPoint->parentSymbol && !isArgSymbol(fn->defPoint->parentSymbol)) {
      BlockStmt* block = NULL;
      if (fn->hasFlag(FLAG_AUTO_II)) {
        block = theProgram->block;
      } else {
        block = getVisibilityBlock(fn->defPoint);
        //
        // add all functions in standard modules to theProgram
        //
        if (standardModuleSet.set_in(block))
          block = theProgram->block;
      }
      VisibleFunctionBlock* vfb = visibleFunctionMap.get(block);
      if (!vfb) {
        vfb = new VisibleFunctionBlock();
        visibleFunctionMap.put(block, vfb);
      }
      Vec<FnSymbol*>* fns = vfb->visibleFunctions.get(fn->name);
      if (!fns) {
        fns = new Vec<FnSymbol*>();
        vfb->visibleFunctions.put(fn->name, fns);
      }
      fns->add(fn);
    }
  }
  nVisibleFunctions = gFnSymbols.n;
}

static BlockStmt*
getVisibleFunctions(BlockStmt* block,
                    const char* name,
                    Vec<FnSymbol*>& visibleFns,
                    Vec<BlockStmt*>& visited) {
  //
  // all functions in standard modules are stored in a single block
  //
  if (standardModuleSet.set_in(block))
    block = theProgram->block;

  //
  // avoid infinite recursion due to modules with mutual uses
  //
  if (visited.set_in(block))
    return NULL;
  else if (isModuleSymbol(block->parentSymbol))
    visited.set_add(block);

  bool canSkipThisBlock = true;

  VisibleFunctionBlock* vfb = visibleFunctionMap.get(block);
  if (vfb) {
    canSkipThisBlock = false; // cannot skip if this block defines functions
    Vec<FnSymbol*>* fns = vfb->visibleFunctions.get(name);
    if (fns) {
      visibleFns.append(*fns);
    }
  }

  if (block->modUses) {
    for_actuals(expr, block->modUses) {
      SymExpr* se = toSymExpr(expr);
      INT_ASSERT(se);
      ModuleSymbol* mod = toModuleSymbol(se->var);
      INT_ASSERT(mod);
      canSkipThisBlock = false; // cannot skip if this block uses modules
      getVisibleFunctions(mod->block, name, visibleFns, visited);
    }
  }

  //
  // visibilityBlockCache contains blocks that can be skipped
  //
  if (BlockStmt* next = visibilityBlockCache.get(block)) {
    getVisibleFunctions(next, name, visibleFns, visited);
    return (canSkipThisBlock) ? next : block;
  }

  if (block != rootModule->block) {
    BlockStmt* next = getVisibilityBlock(block);
    BlockStmt* cache = getVisibleFunctions(next, name, visibleFns, visited);
    if (cache)
      visibilityBlockCache.put(block, cache);
    return (canSkipThisBlock) ? cache : block;
  }

  return NULL;
}


static Type*
resolve_type_expr(Expr* expr) {
  bool stop = false;
  for_exprs_postorder(e, expr) {
    if (expr == e)
      stop = true;
    e = preFold(e);
    if (CallExpr* call = toCallExpr(e)) {
      if (call->parentSymbol) {
        callStack.add(call);
        resolveCall(call);
        FnSymbol* fn = call->isResolved();
        if (fn && call->parentSymbol) {
          resolveFormals(fn);
          if (fn->retTag == RET_PARAM || fn->retTag == RET_TYPE ||
              fn->retType == dtUnknown)
            resolveFns(fn);
        }
        callStack.pop();
      }
    }
    e = postFold(e);
    if (stop) {
      expr = e;
      break;
    }
  }
  Type* t = expr->typeInfo();
  if (t == dtUnknown)
    INT_FATAL(expr, "Unable to resolve type expression");
  return t;
}


static void
makeNoop(CallExpr* call) {
  if (call->baseExpr)
    call->baseExpr->remove();
  while (call->numActuals())
    call->get(1)->remove();
  call->primitive = primitives[PRIM_NOOP];
}


static bool
isTypeExpr(Expr* expr) {
  if (SymExpr* sym = toSymExpr(expr)) {
    if (sym->var->hasFlag(FLAG_TYPE_VARIABLE) || isTypeSymbol(sym->var))
      return true;
  } else if (CallExpr* call = toCallExpr(expr)) {
    if (call->isPrimitive(PRIM_TYPEOF))
      return true;
    if (call->isPrimitive(PRIM_GET_MEMBER_VALUE) ||
        call->isPrimitive(PRIM_GET_MEMBER)) {
      ClassType* ct = toClassType(call->get(1)->typeInfo());
      INT_ASSERT(ct);
      if (ct->symbol->hasFlag(FLAG_REF))
        ct = toClassType(ct->getValType());
      SymExpr* left = toSymExpr(call->get(1));
      SymExpr* right = toSymExpr(call->get(2));
      INT_ASSERT(left && right);
      VarSymbol* var = toVarSymbol(right->var);
      INT_ASSERT(var);
      if (var->immediate) {
        const char* name = var->immediate->v_string;
        for_fields(field, ct) {
          if (!strcmp(field->name, name))
            if (field->hasFlag(FLAG_TYPE_VARIABLE))
              return true;
        }
      } else if (var->hasFlag(FLAG_TYPE_VARIABLE))
        return true;
      if (left->var->type->symbol->hasFlag(FLAG_TUPLE) &&
          left->var->hasFlag(FLAG_TYPE_VARIABLE))
        return true;
    }
    if (FnSymbol* fn = call->isResolved())
      if (fn->retTag == RET_TYPE)
        return true;
  }
  return false;
}


//
// special case cast of class w/ type variables that is not generic
//   i.e. type variables are type definitions (have default types)
//
static void
resolveDefaultGenericType(CallExpr* call) {
  SET_LINENO(call);
  for_actuals(actual, call) {
    if (NamedExpr* ne = toNamedExpr(actual))
      actual = ne->actual;
    if (SymExpr* te = toSymExpr(actual)) {
      if (TypeSymbol* ts = toTypeSymbol(te->var)) {
        if (ClassType* ct = toClassType(ts->type)) {
          if (ct->symbol->hasFlag(FLAG_GENERIC)) {
            CallExpr* cc = new CallExpr(ct->defaultTypeConstructor->name);
            te->replace(cc);
            resolveCall(cc);
            cc->replace(new SymExpr(cc->typeInfo()->symbol));
          }
        }
      }
    }
  }
}


void
resolveCall(CallExpr* call, bool errorCheck) {
  if (!call->primitive) {

    resolveDefaultGenericType(call);

    CallInfo info(call);

    Vec<FnSymbol*> visibleFns;                    // visible functions
    Vec<FnSymbol*> candidateFns;
    Vec<Vec<ArgSymbol*>*> candidateActualFormals; // candidate functions

    //
    // update visible function map as necessary
    //
    if (gFnSymbols.n != nVisibleFunctions)
      buildVisibleFunctionMap();

    if (!call->isResolved()) {
      if (!info.scope) {
        Vec<BlockStmt*> visited;
        getVisibleFunctions(getVisibilityBlock(call), info.name, visibleFns, visited);
      } else {
        if (VisibleFunctionBlock* vfb = visibleFunctionMap.get(info.scope))
          if (Vec<FnSymbol*>* fns = vfb->visibleFunctions.get(info.name))
            visibleFns.append(*fns);
      }
    } else {
      visibleFns.add(call->isResolved());
    }

    if (explainCallLine && explainCallMatch(call)) {
      USR_PRINT(call, "call: %s", toString(&info));
      if (visibleFns.n == 0)
        USR_PRINT(call, "no visible functions found");
      bool first = true;
      forv_Vec(FnSymbol, visibleFn, visibleFns) {
        USR_PRINT(visibleFn, "%s %s",
                  first ? "visible functions are:" : "                      ",
                  toString(visibleFn));
        first = false;
      }
    }

    forv_Vec(FnSymbol, visibleFn, visibleFns) {
      if (call->methodTag && !visibleFn->hasFlag(FLAG_NO_PARENS) && !visibleFn->hasFlag(FLAG_TYPE_CONSTRUCTOR))
        continue;
      addCandidate(&candidateFns, &candidateActualFormals, visibleFn, info);
    }

    if (explainCallLine && explainCallMatch(call)) {
      if (candidateFns.n == 0)
        USR_PRINT(call, "no candidates found");
      bool first = true;
      forv_Vec(FnSymbol, candidateFn, candidateFns) {
        USR_PRINT(candidateFn, "%s %s",
                  first ? "candidates are:" : "               ",
                  toString(candidateFn));
        first = false;
      }
    }

    FnSymbol* best = NULL;
    Vec<ArgSymbol*>* actualFormals = 0;
    Expr* scope = (info.scope) ? info.scope : getVisibilityBlock(call);
    best = disambiguate_by_match(&candidateFns, &candidateActualFormals,
                                 &info.actuals, &actualFormals, scope);

    if (best && explainCallLine && explainCallMatch(call)) {
      USR_PRINT(best, "best candidate is: %s", toString(best));
    }

    if (call->partialTag && (!best || !best->hasFlag(FLAG_NO_PARENS))) {
      best = NULL;
    } else if (!best) {
      if (tryStack.n) {
        tryFailure = true;
        return;
      } else if (candidateFns.n > 0) {
        if (errorCheck)
          printResolutionError("ambiguous", candidateFns, &info);
      } else {
        if (errorCheck)
          printResolutionError("unresolved", visibleFns, &info);
      }
    } else {
      best = defaultWrap(best, actualFormals, &info);
      best = orderWrap(best, actualFormals, &info);
      best = coercionWrap(best, &info);
      best = promotionWrap(best, &info);
    }

    for (int i = 0; i < candidateActualFormals.n; i++)
      delete candidateActualFormals.v[i];

    FnSymbol* resolvedFn = best;

    if (!resolvedFn && !errorCheck) {
      return;
    }

    if (call->partialTag) {
      if (!resolvedFn) {
        return;
      }
      call->partialTag = false;
    }
    if (resolvedFn && resolvedFn->hasFlag(FLAG_DATA_SET_ERROR)) {
      Type* elt_type = resolvedFn->getFormal(1)->type->substitutions.v[0].value->type;
      if (!elt_type)
        INT_FATAL(call, "Unexpected substitution of ddata class");
      USR_FATAL(userCall(call), "type mismatch in assignment from %s to %s",
                toString(info.actuals.v[3]->type), toString(elt_type));
    }
    if (resolvedFn &&
        !strcmp("=", resolvedFn->name) &&
        isRecord(resolvedFn->getFormal(1)->type) &&
        resolvedFn->getFormal(2)->type == dtNil)
      USR_FATAL(userCall(call), "type mismatch in assignment from nil to %s",
                toString(resolvedFn->getFormal(1)->type));
    if (!resolvedFn) {
      INT_FATAL(call, "unable to resolve call");
    }
    if (call->parentSymbol) {
      call->baseExpr->replace(new SymExpr(resolvedFn));
    }

    for_formals_actuals(formal, actual, call) {
      if (formal->intent == INTENT_OUT || formal->intent == INTENT_INOUT) {
        if (SymExpr* se = toSymExpr(actual)) {
          if (se->var->hasFlag(FLAG_EXPR_TEMP) || se->var->isConstant() || se->var->isParameter()) {
            if (formal->intent == INTENT_OUT) {
              USR_FATAL(se, "non-lvalue actual passed to out argument");
            } else {
              USR_FATAL(se, "non-lvalue actual passed to inout argument");
            }
          }
        }
      }
    }

    if (const char* str = innerCompilerWarningMap.get(resolvedFn)) {
      reissueCompilerWarning(str, 2);
      if (FnSymbol* fn = toFnSymbol(callStack.v[callStack.n-2]->isResolved()))
        outerCompilerWarningMap.put(fn, str);
    }
    if (const char* str = outerCompilerWarningMap.get(resolvedFn))
      reissueCompilerWarning(str, 1);

  } else if (call->isPrimitive(PRIM_TUPLE_AND_EXPAND)) {
    SymExpr* se = toSymExpr(call->get(1));
    int size = 0;
    for (int i = 0; i < se->var->type->substitutions.n; i++) {
      if (se->var->type->substitutions.v[i].key) {
        if (!strcmp("size", se->var->type->substitutions.v[i].key->name)) {
          size = toVarSymbol(se->var->type->substitutions.v[i].value)->immediate->int_value();
          break;
        }
      }
    }
    INT_ASSERT(size);
    CallExpr* noop = new CallExpr(PRIM_NOOP);
    call->getStmtExpr()->insertBefore(noop);
    VarSymbol* tmp = gTrue;
    for (int i = 1; i <= size; i++) {
      VarSymbol* tmp1 = newTemp();
      tmp1->addFlag(FLAG_MAYBE_PARAM);
      tmp1->addFlag(FLAG_MAYBE_TYPE);
      VarSymbol* tmp2 = newTemp();
      tmp2->addFlag(FLAG_MAYBE_PARAM);
      tmp2->addFlag(FLAG_MAYBE_TYPE);
      VarSymbol* tmp3 = newTemp();
      tmp3->addFlag(FLAG_MAYBE_PARAM);
      tmp3->addFlag(FLAG_MAYBE_TYPE);
      VarSymbol* tmp4 = newTemp();
      tmp4->addFlag(FLAG_MAYBE_PARAM);
      tmp4->addFlag(FLAG_MAYBE_TYPE);
      call->getStmtExpr()->insertBefore(new DefExpr(tmp1));
      call->getStmtExpr()->insertBefore(new DefExpr(tmp2));
      call->getStmtExpr()->insertBefore(new DefExpr(tmp3));
      call->getStmtExpr()->insertBefore(new DefExpr(tmp4));
      call->getStmtExpr()->insertBefore(
        new CallExpr(PRIM_MOVE, tmp1,
          new CallExpr(se->copy(), new_IntSymbol(i))));
      CallExpr* query = new CallExpr(PRIM_QUERY, tmp1);
      for (int i = 2; i < call->numActuals(); i++)
        query->insertAtTail(call->get(i)->copy());
      call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp2, query));
      call->getStmtExpr()->insertBefore(
        new CallExpr(PRIM_MOVE, tmp3,
          new CallExpr("==", tmp2, call->get(3)->copy())));
      call->getStmtExpr()->insertBefore(
        new CallExpr(PRIM_MOVE, tmp4,
          new CallExpr("&", tmp3, tmp)));
      tmp = tmp4;
    }
    call->replace(new SymExpr(tmp));
    noop->replace(call); // put call back in ast for function resolution
    makeNoop(call);
  } else if (call->isPrimitive(PRIM_TUPLE_EXPAND)) {
    SymExpr* sym = toSymExpr(call->get(1));
    Type* type = sym->var->getValType();

    if (!type->symbol->hasFlag(FLAG_TUPLE))
      USR_FATAL(call, "invalid tuple expand primitive");

    int size = 0;
    for (int i = 0; i < type->substitutions.n; i++) {
      if (type->substitutions.v[i].key) {
        if (!strcmp("size", type->substitutions.v[i].key->name)) {
          size = toVarSymbol(type->substitutions.v[i].value)->immediate->int_value();
          break;
        }
      }
    }
    if (size == 0)
      INT_FATAL(call, "Invalid tuple expand primitive");
    CallExpr* parent = toCallExpr(call->parentExpr);
    CallExpr* noop = new CallExpr(PRIM_NOOP);
    call->getStmtExpr()->insertBefore(noop);
    for (int i = 1; i <= size; i++) {
      VarSymbol* tmp = newTemp();
      tmp->addFlag(FLAG_MAYBE_TYPE);
      DefExpr* def = new DefExpr(tmp);
      call->getStmtExpr()->insertBefore(def);
      CallExpr* e = NULL;
      if (!call->parentSymbol->hasFlag(FLAG_EXPAND_TUPLES_WITH_VALUES)) {
        e = new CallExpr(sym->copy(), new_IntSymbol(i));
      } else {
        e = new CallExpr(PRIM_GET_MEMBER_VALUE, sym->copy(),
                         new_StringSymbol(astr("x", istr(i))));
      }
      CallExpr* move = new CallExpr(PRIM_MOVE, tmp, e);
      call->getStmtExpr()->insertBefore(move);
      call->insertBefore(new SymExpr(tmp));
    }
    call->remove();
    noop->replace(call); // put call back in ast for function resolution
    makeNoop(call);
    // increase tuple rank
    if (parent && parent->isNamed("_type_construct__tuple")) {
      parent->get(1)->replace(new SymExpr(new_IntSymbol(parent->numActuals()-1)));
    }
  } else if (call->isPrimitive(PRIM_SET_MEMBER)) {
    SymExpr* sym = toSymExpr(call->get(2));
    if (!sym)
      INT_FATAL(call, "bad set member primitive");
    VarSymbol* var = toVarSymbol(sym->var);
    if (!var || !var->immediate)
      INT_FATAL(call, "bad set member primitive");
    const char* name = var->immediate->v_string;

    {
      long i;
      if (get_int(sym, &i)) {
        name = astr("x", istr(i));
        call->get(2)->replace(new SymExpr(new_StringSymbol(name)));
      }
    }

    ClassType* ct = toClassType(call->get(1)->typeInfo());
    if (!ct)
      INT_FATAL(call, "bad set member primitive");
    bool found = false;
    for_fields(field, ct) {
      if (!strcmp(field->name, name)) {
        Type* t = call->get(3)->typeInfo();
        if (t == dtUnknown)
          INT_FATAL(call, "Unable to resolve field type");
        if (t == dtNil && field->type == dtUnknown)
          USR_FATAL(call->parentSymbol, "unable to determine type of field from nil");
        if (field->type == dtUnknown)
          field->type = t;
        if (t != field->type && t != dtNil && t != dtObject)
          USR_FATAL(userCall(call), "cannot assign expression of type %s to field of type %s",
                    toString(t), toString(field->type));
        found = true;
      }
    }
    if (!found)
      INT_FATAL(call, "bad set member primitive");
  } else if (call->isPrimitive(PRIM_MOVE)) {
    Expr* rhs = call->get(2);
    Symbol* lhs = NULL;
    if (SymExpr* se = toSymExpr(call->get(1)))
      lhs = se->var;
    INT_ASSERT(lhs);

    if (CallExpr* assignment = toCallExpr(rhs)) {
      if (FnSymbol* fn = assignment->isResolved()) {
        if (!strcmp(fn->name, "=") && fn->retType == dtVoid) {
          //          call->replace(assignment->remove());
          return;
        }
      }
    }

    FnSymbol* fn = toFnSymbol(call->parentSymbol);

    if (lhs->hasFlag(FLAG_TYPE_VARIABLE) && !isTypeExpr(rhs)) {
      if (lhs == fn->getReturnSymbol()) {
        if (!fn->hasFlag(FLAG_HAS_RUNTIME_TYPE))
          USR_FATAL(call, "illegal return of value where type is expected");
      } else {
        USR_FATAL(call, "illegal assignment of value to type");
      }
    }

    if (!lhs->hasFlag(FLAG_TYPE_VARIABLE) && !lhs->hasFlag(FLAG_MAYBE_TYPE) && isTypeExpr(rhs)) {
      if (lhs == fn->getReturnSymbol()) {
        USR_FATAL(call, "illegal return of type where value is expected");
      } else {
        USR_FATAL(call, "illegal assignment of type to value");
      }
    }

    // do not resolve function return type yet
    // except for constructors
    if (fn && fn->getReturnSymbol() == lhs && fn->_this != lhs)
      if (fn->retType == dtUnknown) {
        return;
      }

    Type* rhsType = rhs->typeInfo();

    if (rhsType == dtVoid) {
      if (CallExpr* rhsFn = toCallExpr(rhs)) {
        if (FnSymbol* rhsFnSym = rhsFn->isResolved()) {
          USR_FATAL(userCall(call), 
                    "illegal use of function that does not return a value: '%s'", 
                    rhsFnSym->name);
        }
      }
      USR_FATAL(userCall(call), 
                "illegal use of function that does not return a value");
    }

    if (lhs->type == dtUnknown || lhs->type == dtNil)
      lhs->type = rhsType;

    Type* lhsType = lhs->type;

    if (CallExpr* call = toCallExpr(rhs)) {
      if (FnSymbol* fn = call->isResolved()) {
        if (rhsType == dtUnknown) {
          USR_FATAL_CONT(fn, "unable to resolve return type of function '%s'", fn->name);
          USR_FATAL(rhs, "called recursively at this point");
        }
      }
    }
    if (rhsType == dtUnknown)
      USR_FATAL(call, "unable to resolve type");

    if (rhsType == dtNil && lhsType != dtNil && !isClass(lhsType))
      USR_FATAL(userCall(call), "type mismatch in assignment from nil to %s",
                toString(lhsType));
    Type* lhsBaseType = lhsType->getValType();
    Type* rhsBaseType = rhsType->getValType();
    if (rhsType != dtNil &&
        rhsBaseType != lhsBaseType &&
        !isDispatchParent(rhsBaseType, lhsBaseType))
      USR_FATAL(userCall(call), "type mismatch in assignment from %s to %s",
                toString(rhsType), toString(lhsType));
    if (rhsType != lhsType && isDispatchParent(rhsBaseType, lhsBaseType)) {
      Symbol* tmp = newTemp(rhsType);
      call->insertBefore(new DefExpr(tmp));
      call->insertBefore(new CallExpr(PRIM_MOVE, tmp, rhs->remove()));
      call->insertAtTail(new CallExpr(PRIM_CAST, lhsBaseType->symbol, tmp));
    }
  } else if (call->isPrimitive(PRIM_INIT)) {
    resolveDefaultGenericType(call);
  }
}

static bool
formalRequiresTemp(ArgSymbol* formal) {
  if (formal->intent == INTENT_PARAM ||
      formal->intent == INTENT_TYPE ||
      formal->intent == INTENT_REF ||
      !strcmp("this", formal->name) ||
      formal->hasFlag(FLAG_IS_MEME) ||
      (formal == toFnSymbol(formal->defPoint->parentSymbol)->_outer) ||
      formal->hasFlag(FLAG_TYPE_VARIABLE) ||
      formal->instantiatedParam ||
      formal->type == dtMethodToken ||
      (formal->type->symbol->hasFlag(FLAG_REF) &&
       formal->intent == INTENT_BLANK) ||
      formal->hasFlag(FLAG_NO_FORMAL_TMP))
    return false;
  return true;
}

static void
insertFormalTemps(FnSymbol* fn) {
  if (!strcmp(fn->name, "_init") ||
      !strcmp(fn->name, "_cast") ||
      !strcmp(fn->name, "chpl__initCopy") ||
      !strcmp(fn->name, "chpl__autoCopy") ||
      !strcmp(fn->name, "_getIterator") ||
      !strcmp(fn->name, "_getIteratorHelp") ||
      !strcmp(fn->name, "iteratorIndex") ||
      !strcmp(fn->name, "iteratorIndexHelp") ||
      !strcmp(fn->name, "=") ||
      !strcmp(fn->name, "_createFieldDefault") ||
      !strcmp(fn->name, "chpl__autoDestroy") ||
      !strcmp(fn->name, "chpldev_refToString") ||
      fn->hasFlag(FLAG_ALLOW_REF) ||
      fn->hasFlag(FLAG_REF))
    return;
  SymbolMap formals2vars;
  for_formals(formal, fn) {
    if (formalRequiresTemp(formal)) {
      VarSymbol* tmp = newTemp(astr("_formal_tmp_", formal->name));
      Type* formalType = formal->type->getValType();
      if ((formal->intent == INTENT_BLANK ||
           formal->intent == INTENT_CONST) &&
          !formalType->symbol->hasFlag(FLAG_DOMAIN) &&
          !formalType->symbol->hasFlag(FLAG_SYNC) &&
          !formalType->symbol->hasFlag(FLAG_ARRAY))
        tmp->addFlag(FLAG_CONST);
      formals2vars.put(formal, tmp);
    }
  }
  if (formals2vars.n > 0) {
    update_symbols(fn->body, &formals2vars);
    form_Map(SymbolMapElem, e, formals2vars) {
      ArgSymbol* formal = toArgSymbol(e->key);
      Symbol* tmp = e->value;
      if (formal->intent == INTENT_OUT) {
        if (formal->defaultExpr && formal->defaultExpr->body.tail->typeInfo() != dtTypeDefaultToken) {
          BlockStmt* defaultExpr = formal->defaultExpr->copy();
          fn->insertAtHead(new CallExpr(PRIM_MOVE, tmp, defaultExpr->body.tail->remove()));
          fn->insertAtHead(defaultExpr);
        } else {
          VarSymbol* refTmp = newTemp();
          VarSymbol* typeTmp = newTemp();
          typeTmp->addFlag(FLAG_MAYBE_TYPE);
          fn->insertAtHead(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_INIT, typeTmp)));
          fn->insertAtHead(new CallExpr(PRIM_MOVE, typeTmp, new CallExpr(PRIM_TYPEOF, refTmp)));
          fn->insertAtHead(new CallExpr(PRIM_MOVE, refTmp, new CallExpr(PRIM_GET_REF, formal)));
          fn->insertAtHead(new DefExpr(refTmp));
          fn->insertAtHead(new DefExpr(typeTmp));
        }
      } else if (formal->intent == INTENT_INOUT || formal->intent == INTENT_IN) {
        fn->insertAtHead(new CallExpr(PRIM_MOVE, tmp, new CallExpr("chpl__initCopy", formal)));
        tmp->addFlag(FLAG_INSERT_AUTO_DESTROY);
      } else {
        TypeSymbol* ts = formal->type->symbol;
        if (!ts->hasFlag(FLAG_DOMAIN) &&
            !ts->hasFlag(FLAG_ARRAY) &&
            !ts->hasFlag(FLAG_DISTRIBUTION) &&
            !ts->hasFlag(FLAG_ITERATOR_CLASS) &&
            !ts->hasFlag(FLAG_ITERATOR_RECORD) &&
            !ts->hasFlag(FLAG_REF) &&
            !ts->hasFlag(FLAG_SYNC)) {
          fn->insertAtHead(new CallExpr(PRIM_MOVE, tmp, new CallExpr("chpl__autoCopy", formal)));
          tmp->addFlag(FLAG_INSERT_AUTO_DESTROY);
        } else
          fn->insertAtHead(new CallExpr(PRIM_MOVE, tmp, formal));
      }
      fn->insertAtHead(new DefExpr(tmp));
      if (formal->intent == INTENT_INOUT || formal->intent == INTENT_OUT) {
        fn->insertBeforeReturnAfterLabel(new CallExpr(PRIM_MOVE, formal, new CallExpr("=", formal, tmp)));
      }
    }
  }
}

//
// Calculate the index type for a param for loop by checking the type of
// the range that would be built using the same low and high values.
// 
static Type* param_for_index_type(CallExpr* loop) {
  BlockStmt* block = toBlockStmt(loop->parentExpr);
  SymExpr* lse = toSymExpr(loop->get(2));
  SymExpr* hse = toSymExpr(loop->get(3));
  CallExpr* range = new CallExpr("_build_range", lse->copy(), hse->copy());
  block->insertBefore(range);
  resolveCall(range);
  if (!range->isResolved()) {
    INT_FATAL("unresolved range");
  }
  resolveFormals(range->isResolved());
  DefExpr* formal = toDefExpr(range->isResolved()->formals.get(1));
  Type* formalType;
  if (toArgSymbol(formal->sym)->typeExpr) {
    // range->isResolved() is the coercion wrapper for _build_range
    formalType = toArgSymbol(formal->sym)->typeExpr->body.tail->typeInfo();
  } else {
    formalType = formal->sym->type;
  }
  range->remove();
  return formalType;
}


static void fold_param_for(CallExpr* loop) {
  BlockStmt* block = toBlockStmt(loop->parentExpr);
  SymExpr* lse = toSymExpr(loop->get(2));
  SymExpr* hse = toSymExpr(loop->get(3));
  SymExpr* sse = toSymExpr(loop->get(4));
  if (!block || !lse || !hse || !sse)
    USR_FATAL(loop, "param for loop must be defined over a param range");
  VarSymbol* lvar = toVarSymbol(lse->var);
  VarSymbol* hvar = toVarSymbol(hse->var);
  VarSymbol* svar = toVarSymbol(sse->var);
  if (!lvar || !hvar || !svar)
    USR_FATAL(loop, "param for loop must be defined over a param range");
  if (!lvar->immediate || !hvar->immediate || !svar->immediate)
    USR_FATAL(loop, "param for loop must be defined over a param range");
  Expr* index_expr = loop->get(1);
  Type* formalType = param_for_index_type(loop);
  IF1_int_type idx_size;
  if (get_width(formalType) == 32) {
    idx_size = INT_SIZE_32;
  } else {
    idx_size = INT_SIZE_64;
  }
  if (block->blockTag != BLOCK_NORMAL)
    INT_FATAL("ha");
  loop->remove();
  CallExpr* noop = new CallExpr(PRIM_NOOP);
  block->insertAfter(noop);
  Symbol* index = toSymExpr(index_expr)->var;

  if (is_int_type(formalType)) {
    int64_t low = lvar->immediate->int_value();
    int64_t high = hvar->immediate->int_value();
    int64_t stride = svar->immediate->int_value();
    if (stride <= 0) {
      for (int64_t i = high; i >= low; i += stride) {
        SymbolMap map;
        map.put(index, new_IntSymbol(i, idx_size));
        noop->insertBefore(block->copy(&map));
      }
    } else {
      for (int64_t i = low; i <= high; i += stride) {
        SymbolMap map;
        map.put(index, new_IntSymbol(i, idx_size));
        noop->insertBefore(block->copy(&map));
      }
    }
  } else {
    INT_ASSERT(is_uint_type(formalType) || is_bool_type(formalType));
    uint64_t low = lvar->immediate->uint_value();
    uint64_t high = hvar->immediate->uint_value();
    int64_t stride = svar->immediate->int_value();
    if (stride <= 0) {
      for (uint64_t i = high; i >= low; i += stride) {
        SymbolMap map;
        map.put(index, new_UIntSymbol(i, idx_size));
        noop->insertBefore(block->copy(&map));
      }
    } else {
      for (uint64_t i = low; i <= high; i += stride) {
        SymbolMap map;
        map.put(index, new_UIntSymbol(i, idx_size));
        noop->insertBefore(block->copy(&map));
      }
    }
  }
  block->replace(loop);
  makeNoop(loop);
}


static Expr* dropUnnecessaryCast(CallExpr* call) {
  // Check for and remove casts to the original type and size
  Expr* result = call;
  if (!call->isNamed("_cast"))
    INT_FATAL("dropUnnecessaryCasts called on non _cast call");

  if (SymExpr* sym = toSymExpr(call->get(2))) {
    if (VarSymbol* var = toVarSymbol(sym->var)) {
      if (SymExpr* sym = toSymExpr(call->get(1))) {
        Type* src = var->type;
        Type* dst = sym->var->type;

        if (dst == src) {
          result = new SymExpr(var);
          call->replace(result);
        }
      }
    } else if (EnumSymbol* e = toEnumSymbol(sym->var)) {
      if (SymExpr* sym = toSymExpr(call->get(1))) {
        EnumType* src = toEnumType(e->type);
        EnumType* dst = toEnumType(sym->var->type);
        if (dst && src == dst) {
          result = new SymExpr(e);
          call->replace(result);
        }
      }
    }
  }
  return result;
}

/*
  Creates the parent class which will represent the function's type.  Children of the parent class will capture different functions which
  happen to share the same function type.  By using the parent class we can assign new values onto variable that match the function type
  but may currently be pointing at a different function.
*/
static ClassType* createAndInsertFunParentClass(CallExpr *call, const char *name) {
  ClassType *parent = new ClassType(CLASS_CLASS);
  TypeSymbol *parent_ts = new TypeSymbol(name, parent);

  parent_ts->addFlag(FLAG_FUNCTION_CLASS);

  // Because this function type needs to be globally visible (because we don't know the modules it will be passed to), we put
  // it at the highest scope
  theProgram->block->body.insertAtTail(new DefExpr(parent_ts));
    
  parent->dispatchParents.add(dtObject);
  dtObject->dispatchChildren.add(parent);
  VarSymbol* parent_super = new VarSymbol("super", dtObject);
  parent_super->addFlag(FLAG_SUPER_CLASS);
  parent->fields.insertAtHead(new DefExpr(parent_super));
  build_constructor(parent);
  build_type_constructor(parent);

  return parent;
}

/*
  To mimic a function call, we create a .this method for the parent class.  This will allow the object to look and feel like a 
  first-class function, by both being an object and being invoked using parentheses syntax.  Children of the parent class will
  override this method and wrap the function that is being used as a first-class value.

  To focus on just the types of the arguments and not their names or default values, we use the parent method's names and types
  as the basis for all children which override it.  

  The function is put at the highest scope so that all functions of a given type will share the same parent class.
*/
static FnSymbol* createAndInsertFunParentMethod(CallExpr *call, ClassType *parent, AList &arg_list, bool isFormal, Type *retType) {
  FnSymbol *parent_method = new FnSymbol("this");
  ArgSymbol *thisParentSymbol = new ArgSymbol(INTENT_BLANK, "this", parent);
  parent_method->insertFormalAtTail(new ArgSymbol(INTENT_BLANK, "_mt", dtMethodToken));
  parent_method->insertFormalAtTail(thisParentSymbol);
  parent_method->_this = thisParentSymbol;

  int i = 0, alength = arg_list.length;

  //We handle the arg list differently depending on if it's a list of formal args or actual args
  if (isFormal) {
    
    for_alist(formalExpr, arg_list) {
      DefExpr* dExp = toDefExpr(formalExpr);
      ArgSymbol* fArg = toArgSymbol(dExp->sym);

      if (fArg->type != dtVoid) {
        ArgSymbol* newFormal = new ArgSymbol(INTENT_BLANK, fArg->name, fArg->type);
        if (fArg->typeExpr)
          newFormal->typeExpr = fArg->typeExpr->copy();

        parent_method->insertFormalAtTail(newFormal);
      }
    }
  }
  else {
    char name_buffer[100];
    int name_index = 0;
    
    for_alist(actualExpr, arg_list) {
      sprintf(name_buffer, "name%i", name_index++);
      if (i != (alength-1)) {
        SymExpr* sExpr = toSymExpr(actualExpr);
        if (sExpr->var->type != dtVoid) {
          ArgSymbol* newFormal = new ArgSymbol(INTENT_BLANK, name_buffer, sExpr->var->type);
        
          parent_method->insertFormalAtTail(newFormal);
        }
      }
      ++i;
    }
  }

  if (retType != dtVoid) {
    VarSymbol *tmp = newTemp(retType); 
    parent_method->insertAtTail(new DefExpr(tmp));
    parent_method->insertAtTail(new CallExpr(PRIM_RETURN, tmp));
  }

  // Because this function type needs to be globally visible (because we don't know the modules it will be passed to), we put
  // it at the highest scope
  theProgram->block->body.insertAtTail(new DefExpr(parent_method));
    
  normalize(parent_method);
    
  parent->methods.add(parent_method);
  
  return parent_method;
}

/*
  Builds up the name of the parent for lookup by looking through the types of the arguments, either formal or actual
*/
static std::string buildParentName(AList &arg_list, bool isFormal, Type *retType) {
  std::ostringstream oss;
  oss << "chpl__fcf_type_";
  
  bool isFirst = true;

  if (isFormal) {
    if (arg_list.length == 0) {
      oss << "void";
    }
    else {
      for_alist(formalExpr, arg_list) {
        DefExpr* dExp = toDefExpr(formalExpr);
        ArgSymbol* fArg = toArgSymbol(dExp->sym);
        
        if (!isFirst)
          oss << "_";
    
        oss << fArg->type->symbol->name;
    
        isFirst = false;
      }
    }     
    oss << "_";
    oss << retType->symbol->name;
  }
  else {
    int i = 0, alength = arg_list.length;

    if (alength == 1) {
      oss << "void_";
    }
    
    for_alist(actualExpr, arg_list) {
      if (!isFirst)
        oss << "_";
      
      SymExpr* sExpr = toSymExpr(actualExpr);
      
      ++i;
   
      oss << sExpr->var->type->symbol->name;
      
      isFirst = false;
    }
  }

  return oss.str();
}

/*
  Helper function for creating or finding the parent class for a given function type specified 
  by the type signature.  The last type given in the signature is the return type, the remainder
  represent arguments to the function.
*/
static ClassType* createOrFindFunTypeFromAnnotation(AList &arg_list, CallExpr *call) {
  ClassType *parent;
  FnSymbol *parent_method;

  SymExpr *retTail = toSymExpr(arg_list.tail);
  Type *retType = retTail->var->type;

  std::string parent_name = buildParentName(arg_list, false, retType);
  
  if (functionTypeMap.find(parent_name) != functionTypeMap.end()) {
    std::pair<ClassType*, FnSymbol*> ctfs = functionTypeMap[parent_name];
    parent = ctfs.first;
    parent_method = ctfs.second;
  }
  else {
    parent = createAndInsertFunParentClass(call, parent_name.c_str());
    parent_method = createAndInsertFunParentMethod(call, parent, arg_list, false, retType);

    functionTypeMap[parent_name] = std::pair<ClassType*, FnSymbol*>(parent, parent_method);
  }

  return parent;
}

/*
  Captures a function as a first-class value by creating an object that will represent the function.  The class is 
  created at the same scope as the function being referenced.  Each class is unique and shared among all 
  uses of that function as a value.  Once built, the class will override the .this method of the parent and wrap 
  the call to the function being captured as a value.  Then, an instance of the class is instantiated and returned.
*/
static Expr*
createFunctionAsValue(CallExpr *call) {
  static int unique_fcf_id = 0;
  UnresolvedSymExpr* use = toUnresolvedSymExpr(call->get(1));
  INT_ASSERT(use);
  const char *fname = use->unresolved;
      
  Vec<FnSymbol*> visibleFns;
  Vec<BlockStmt*> visited;
  getVisibleFunctions(getVisibilityBlock(call), fname, visibleFns, visited);

  if (visibleFns.n > 1) {
    USR_FATAL(call, "Can not capture overloaded functions as values");
  }

  INT_ASSERT(visibleFns.n == 1);
  
  FnSymbol* captured_fn = visibleFns.v[0];

  //Check to see if we've already cached the capture somewhere
  if (functionCaptureMap.find(captured_fn) != functionCaptureMap.end()) {
    return new CallExpr(functionCaptureMap[captured_fn]);
  }

  resolveFormals(captured_fn);
  resolveFns(captured_fn);

  ClassType *parent;
  FnSymbol *thisParentMethod;

  std::string parent_name = buildParentName(captured_fn->formals, true, captured_fn->retType); 
  
  if (functionTypeMap.find(parent_name) != functionTypeMap.end()) {
    std::pair<ClassType*, FnSymbol*> ctfs = functionTypeMap[parent_name];
    parent = ctfs.first;
    thisParentMethod = ctfs.second;
  }
  else {
    parent = createAndInsertFunParentClass(call, parent_name.c_str());
    thisParentMethod = createAndInsertFunParentMethod(call, parent, captured_fn->formals, true, captured_fn->retType);
    functionTypeMap[parent_name] = std::pair<ClassType*, FnSymbol*>(parent, thisParentMethod);
  }

  ClassType *ct = new ClassType(CLASS_CLASS);
  std::ostringstream fcf_name;
  fcf_name << "_chpl_fcf_" << unique_fcf_id++ << "_" << fname;
  
  TypeSymbol *ts = new TypeSymbol(astr(fcf_name.str().c_str()), ct);

  call->parentExpr->insertBefore(new DefExpr(ts));
  
  ct->dispatchParents.add(parent);
  parent->dispatchChildren.add(ct);
  VarSymbol* super = new VarSymbol("super", parent);
  super->addFlag(FLAG_SUPER_CLASS);
  ct->fields.insertAtHead(new DefExpr(super));

  build_constructor(ct);
  build_type_constructor(ct);

  FnSymbol *thisMethod = new FnSymbol("this");
  ArgSymbol *thisSymbol = new ArgSymbol(INTENT_BLANK, "this", ct);
  thisMethod->insertFormalAtTail(new ArgSymbol(INTENT_BLANK, "_mt", dtMethodToken));
  thisMethod->insertFormalAtTail(thisSymbol);
  thisMethod->_this = thisSymbol;

  CallExpr *innerCall = new CallExpr(captured_fn);
      
  int skip = 2;
  for_alist(formalExpr, thisParentMethod->formals) {
    //Skip the first two arguments from the parent, which are _mt and this
    if (skip) {
      --skip;
      continue;
    }

    DefExpr* dExp = toDefExpr(formalExpr);
    ArgSymbol* fArg = toArgSymbol(dExp->sym);

    ArgSymbol* newFormal = new ArgSymbol(INTENT_BLANK, fArg->name, fArg->type);
    if (fArg->typeExpr) 
      newFormal->typeExpr = fArg->typeExpr->copy();
    SymExpr* argSym = new SymExpr(newFormal);
    innerCall->insertAtTail(argSym);
              
    thisMethod->insertFormalAtTail(newFormal);
  }
      
  Vec<CallExpr*> calls;
  collectCallExprs(captured_fn, calls);

  forv_Vec(CallExpr, cl, calls) {
    if (cl->isPrimitive(PRIM_YIELD)) {
      USR_FATAL_CONT(cl, "Iterators not allowed in first class functions");
    }
  }
      
  if (captured_fn->retType == dtVoid) {
    thisMethod->insertAtTail(innerCall);
  }
  else {
    VarSymbol *tmp = newTemp();
    thisMethod->insertAtTail(new DefExpr(tmp));
    thisMethod->insertAtTail(new CallExpr(PRIM_MOVE, tmp, innerCall));
      
    thisMethod->insertAtTail(new CallExpr(PRIM_RETURN, tmp));
  }
      
  call->parentExpr->insertBefore(new DefExpr(thisMethod));
  normalize(thisMethod);

  ct->methods.add(thisMethod);
  
  FnSymbol *wrapper = new FnSymbol("wrapper");
  wrapper->addFlag(FLAG_INLINE);

  wrapper->insertAtTail(new CallExpr(PRIM_RETURN, new CallExpr(PRIM_CAST, parent->symbol, new CallExpr(ct->defaultConstructor))));

  call->getStmtExpr()->insertBefore(new DefExpr(wrapper));

  normalize(wrapper);

  CallExpr *call_wrapper = new CallExpr(wrapper);
  functionCaptureMap[captured_fn] = wrapper;
  
  return call_wrapper;
}

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
static bool
usesOuterVars(FnSymbol* fn, Vec<FnSymbol*> &seen) {
  Vec<BaseAST*> asts;
  collect_asts(fn, asts);
  forv_Vec(BaseAST, ast, asts) {
    if (toCallExpr(ast)) {
      CallExpr *call = toCallExpr(ast);
                
      //dive into calls
      Vec<FnSymbol*> visibleFns;
      Vec<BlockStmt*> visited;

      getVisibleFunctions(getVisibilityBlock(call), call->parentSymbol->name, visibleFns, visited);
    
      forv_Vec(FnSymbol, called_fn, visibleFns) {
        bool seen_this_fn = false;
        forv_Vec(FnSymbol, seen_fn, seen) {
          if (called_fn == seen_fn) {
            seen_this_fn = true;
            break;
          }
        }
        if (!seen_this_fn) {
          seen.add(called_fn);
          if (usesOuterVars(called_fn, seen)) {
            return true;
          }
        }
      }
    }
    if (SymExpr* symExpr = toSymExpr(ast)) {
      Symbol* sym = symExpr->var;
      
      if (toVarSymbol(sym) || toArgSymbol(sym))
        if (isOuterVar(sym, fn))
          return true;
    }
  }
  return false;
}

static Expr*
preFold(Expr* expr) {
  Expr* result = expr;
  if (CallExpr* call = toCallExpr(expr)) {
    if (SymExpr* sym = toSymExpr(call->baseExpr)) {
      if (TypeSymbol* type = toTypeSymbol(sym->var)) {
        if (call->numActuals() == 1) {
          if (SymExpr* arg = toSymExpr(call->get(1))) {
            if (VarSymbol* var = toVarSymbol(arg->var)) {
              if (var->immediate) {
                if (NUM_KIND_INT == var->immediate->const_kind ||
                    NUM_KIND_UINT == var->immediate->const_kind) {
                  int size;
                  if (NUM_KIND_INT == var->immediate->const_kind) {
                    size = var->immediate->int_value();
                  } else {
                    size = (int)var->immediate->uint_value();
                  }
                  TypeSymbol* tsize = NULL;
                  if (type == dtBools[BOOL_SIZE_SYS]->symbol) {
                    switch (size) {
                    case 8: tsize = dtBools[BOOL_SIZE_8]->symbol; break;
                    case 16: tsize = dtBools[BOOL_SIZE_16]->symbol; break;
                    case 32: tsize = dtBools[BOOL_SIZE_32]->symbol; break;
                    case 64: tsize = dtBools[BOOL_SIZE_64]->symbol; break;
                    default:
                      USR_FATAL( call, "illegal size %d for bool", size);
                    }
                    result = new SymExpr(tsize);
                    call->replace(result);
                  } else if (type == dtInt[INT_SIZE_32]->symbol) {
                    switch (size) {
                    case 8: tsize = dtInt[INT_SIZE_8]->symbol; break;
                    case 16: tsize = dtInt[INT_SIZE_16]->symbol; break;
                    case 32: tsize = dtInt[INT_SIZE_32]->symbol; break;
                    case 64: tsize = dtInt[INT_SIZE_64]->symbol; break;
                    default:
                      USR_FATAL( call, "illegal size %d for int", size);
                    }
                    result = new SymExpr(tsize);
                    call->replace(result);
                  } else if (type == dtUInt[INT_SIZE_32]->symbol) {
                    switch (size) {
                    case  8: tsize = dtUInt[INT_SIZE_8]->symbol;  break;
                    case 16: tsize = dtUInt[INT_SIZE_16]->symbol; break;
                    case 32: tsize = dtUInt[INT_SIZE_32]->symbol; break;
                    case 64: tsize = dtUInt[INT_SIZE_64]->symbol; break;
                    default:
                      USR_FATAL( call, "illegal size %d for uint", size);
                    }
                    result = new SymExpr(tsize);
                    call->replace(result);
                  } else if (type == dtReal[FLOAT_SIZE_64]->symbol) {
                    switch (size) {
                    case 32:  tsize = dtReal[FLOAT_SIZE_32]->symbol;  break;
                    case 64:  tsize = dtReal[FLOAT_SIZE_64]->symbol;  break;
                    default:
                      USR_FATAL( call, "illegal size %d for real", size);
                    }
                    result = new SymExpr(tsize);
                    call->replace(result);
                  } else if (type == dtImag[FLOAT_SIZE_64]->symbol) {
                    switch (size) {
                    case 32:  tsize = dtImag[FLOAT_SIZE_32]->symbol;  break;
                    case 64:  tsize = dtImag[FLOAT_SIZE_64]->symbol;  break;
                    default:
                      USR_FATAL( call, "illegal size %d for imag", size);
                    }
                    result = new SymExpr(tsize);
                    call->replace(result);
                  } else if (type == dtComplex[COMPLEX_SIZE_128]->symbol) {
                    switch (size) {
                    case 64:  tsize = dtComplex[COMPLEX_SIZE_64]->symbol;  break;
                    case 128: tsize = dtComplex[COMPLEX_SIZE_128]->symbol; break;
                    default:
                      USR_FATAL( call, "illegal size %d for complex", size);
                    }
                    result = new SymExpr(tsize);
                    call->replace(result);
                  }
                }
              }
            }
          }
        }
      }
    }

    if (SymExpr* sym = toSymExpr(call->baseExpr)) {
      if (toVarSymbol(sym->var) || toArgSymbol(sym->var)) {
        Expr* base = call->baseExpr;
        base->replace(new UnresolvedSymExpr("this"));
        call->insertAtHead(base);
        call->insertAtHead(gMethodToken);
      }
    }

    if (CallExpr* base = toCallExpr(call->baseExpr)) {
      if (base->partialTag) {
        for_actuals_backward(actual, base) {
          actual->remove();
          call->insertAtHead(actual);
        }
        base->replace(base->baseExpr->remove());
      } else {
        VarSymbol* this_temp = newTemp("_this_temp");
        this_temp->addFlag(FLAG_EXPR_TEMP);
        base->replace(new UnresolvedSymExpr("this"));
        CallExpr* move = new CallExpr(PRIM_MOVE, this_temp, base);
        call->insertAtHead(new SymExpr(this_temp));
        call->insertAtHead(gMethodToken);
        call->getStmtExpr()->insertBefore(new DefExpr(this_temp));
        call->getStmtExpr()->insertBefore(move);
        result = move;
        return result;
      }
    }

    if (call->isNamed("this")) {
      SymExpr* base = toSymExpr(call->get(2));
      INT_ASSERT(base);
      if (isVarSymbol(base->var) && base->var->hasFlag(FLAG_TYPE_VARIABLE)) {
        if (call->numActuals() == 2)
          USR_FATAL(call, "illegal call of type");
        long index;
        if (!get_int(call->get(3), &index))
          USR_FATAL(call, "illegal type index expression");
        char field[8];
        sprintf(field, "x%ld", index);
        result = new SymExpr(base->var->type->getField(field)->type->symbol);
        call->replace(result);
      } else if (base && (isVarSymbol(base->var) || isArgSymbol(base->var))) {
        //
        // resolve tuple indexing by an integral parameter
        //
        Type* t = base->var->getValType();
        if (t->symbol->hasFlag(FLAG_TUPLE)) {
          if (call->numActuals() != 3)
            USR_FATAL(call, "illegal tuple indexing expression");
          Type* indexType = call->get(3)->getValType();
          if (!is_int_type(indexType) && !is_uint_type(indexType))
            USR_FATAL(call, "tuple indexing expression is not of integral type");
          long index;
          unsigned long uindex;
          if (get_int(call->get(3), &index)) {
            char field[8];
            sprintf(field, "x%ld", index);
            if (index <= 0 || index >= toClassType(t)->fields.length)
              USR_FATAL(call, "tuple index out-of-bounds error (%ld)", index);
            if (toClassType(t)->getField(field)->type->symbol->hasFlag(FLAG_REF))
              result = new CallExpr(PRIM_GET_MEMBER_VALUE, base->var, new_StringSymbol(field));
            else
              result = new CallExpr(PRIM_GET_MEMBER, base->var, new_StringSymbol(field));
            call->replace(result);
          } else if (get_uint(call->get(3), &uindex)) {
            char field[8];
            sprintf(field, "x%lu", uindex);
            if (uindex <= 0 || uindex >= (unsigned long)toClassType(t)->fields.length)
              USR_FATAL(call, "tuple index out-of-bounds error (%lu)", uindex);
            if (toClassType(t)->getField(field)->type->symbol->hasFlag(FLAG_REF))
              result = new CallExpr(PRIM_GET_MEMBER_VALUE, base->var, new_StringSymbol(field));
            else
              result = new CallExpr(PRIM_GET_MEMBER, base->var, new_StringSymbol(field));
            call->replace(result);
          }
        }
      }
    } else if (call->isPrimitive(PRIM_INIT)) {
      SymExpr* se = toSymExpr(call->get(1));
      INT_ASSERT(se);
      if (!se->var->hasFlag(FLAG_TYPE_VARIABLE))
        USR_FATAL(call, "invalid type specification");
      Type* type = call->get(1)->getValType();
      if (type->symbol->hasFlag(FLAG_ITERATOR_CLASS)) {
        result = new CallExpr(PRIM_CAST, type->symbol, gNil);
        call->replace(result);
      } else if (type->defaultValue == gNil) {
        result = new CallExpr("_cast", type->symbol, type->defaultValue);
        call->replace(result);
      } else if (type->defaultValue) {
        result = new SymExpr(type->defaultValue);
        call->replace(result);
      } else {
        inits.add(call);
      }
    } else if (call->isPrimitive(PRIM_TYPEOF)) {
      Type* type = call->get(1)->getValType();
      if (type->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE)) {
        result = new CallExpr("chpl__convertValueToRuntimeType", call->get(1)->remove());
        call->replace(result);
      }
    } else if (call->isPrimitive(PRIM_QUERY)) {
      Symbol* field = determineQueriedField(call);
      if (field && (field->hasFlag(FLAG_PARAM) || field->hasFlag(FLAG_TYPE_VARIABLE))) {
        result = new CallExpr(field->name, gMethodToken, call->get(1)->remove());
        call->replace(result);
      } else if (isInstantiatedField(field)) {
        VarSymbol* tmp = newTemp();
        call->getStmtExpr()->insertBefore(new DefExpr(tmp));
        if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_TUPLE) && field->name[0] == 'x')
          result = new CallExpr(PRIM_GET_MEMBER_VALUE, call->get(1)->remove(), new_StringSymbol(field->name));
        else
          result = new CallExpr(field->name, gMethodToken, call->get(1)->remove());
        call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, result));
        call->replace(new CallExpr(PRIM_TYPEOF, tmp));
      } else
        USR_FATAL(call, "invalid query -- queried field must be a type of parameter");
    } else if (call->isPrimitive(PRIM_CAPTURE_FN)) {
      result = createFunctionAsValue(call);
      call->replace(result);
    } else if (call->isPrimitive(PRIM_CREATE_FN_TYPE)) {
      ClassType *parent = createOrFindFunTypeFromAnnotation(call->argList, call);

      result = new SymExpr(parent->symbol);
      call->replace(result);
    } else if (call->isNamed("chpl__initCopy") ||
               call->isNamed("chpl__autoCopy")) {
      if (call->numActuals() == 1) {
        if (SymExpr* symExpr = toSymExpr(call->get(1))) {
          if (VarSymbol* var = toVarSymbol(symExpr->var)) {
            if (var->immediate) {
              result = new SymExpr(var);
              call->replace(result);
            }
          } else {
            if (EnumSymbol* var = toEnumSymbol(symExpr->var)) {
              // Treat enum values as immediates
              result = new SymExpr(var);
              call->replace(result);
            }
          }
        }
      }
    } else if (call->isNamed("_cast")) {
      result = dropUnnecessaryCast(call);
      if (result == call) {
        // The cast was not dropped.  Remove integer casts on immediate values.
        if (SymExpr* sym = toSymExpr(call->get(2))) {
          if (VarSymbol* var = toVarSymbol(sym->var)) {
            if (var->immediate) {
              if (SymExpr* sym = toSymExpr(call->get(1))) {
                Type* src = var->type;
                Type* dst = sym->var->type;
                if ((is_int_type(src) || is_uint_type(src) ||
                     is_bool_type(src)) &&
                    (is_int_type(dst) || is_uint_type(dst) ||
                     is_bool_type(dst) || is_enum_type(dst) ||
                     dst == dtString)) {
                  VarSymbol* typevar = toVarSymbol(dst->defaultValue);
                  EnumType* typeenum = toEnumType(dst);
                  if (typevar) {
                    if (!typevar->immediate)
                      INT_FATAL("unexpected case in cast_fold");

                    Immediate coerce = *typevar->immediate;
                    coerce_immediate(var->immediate, &coerce);
                    result = new SymExpr(new_ImmediateSymbol(&coerce));
                    call->replace(result);
                  } else if (typeenum) {
                    long value, count = 0;
                    bool replaced = false;
                    if (!get_int(call->get(2), &value)) {
                      INT_FATAL("unexpected case in cast_fold");
                    }
                    for_enums(constant, typeenum) {
                      if (!get_int(constant->init, &count)) {
                        count++;
                      }
                      if (count == value) {
                        result = new SymExpr(constant->sym);
                        call->replace(result);
                        replaced = true;
                        break;
                      }
                    }
                    if (!replaced) {
                      USR_FATAL(call->get(2), "enum cast out of bounds");
                    }
                  } else {
                    INT_FATAL("unexpected case in cast_fold");
                  }
                }
              }
            }
          } else if (EnumSymbol* enumSym = toEnumSymbol(sym->var)) {
            if (SymExpr* sym = toSymExpr(call->get(1))) {
              Type* dst = sym->var->type;
              if (dst == dtString) {
                result = new SymExpr(new_StringSymbol(enumSym->name));
                call->replace(result);
              }
            }
          }
        }
      }
    } else if (call->isNamed("==")) {
      if (isTypeExpr(call->get(1)) && isTypeExpr(call->get(2))) {
        Type* lt = call->get(1)->getValType();
        Type* rt = call->get(2)->getValType();
        if (lt != dtUnknown && rt != dtUnknown &&
            !lt->symbol->hasFlag(FLAG_GENERIC) &&
            !rt->symbol->hasFlag(FLAG_GENERIC)) {
          result = (lt == rt) ? new SymExpr(gTrue) : new SymExpr(gFalse);
          call->replace(result);
        }
      }
    } else if (call->isNamed("!=")) {
      if (isTypeExpr(call->get(1)) && isTypeExpr(call->get(2))) {
        Type* lt = call->get(1)->getValType();
        Type* rt = call->get(2)->getValType();
        if (lt != dtUnknown && rt != dtUnknown &&
            !lt->symbol->hasFlag(FLAG_GENERIC) &&
            !rt->symbol->hasFlag(FLAG_GENERIC)) {
          result = (lt != rt) ? new SymExpr(gTrue) : new SymExpr(gFalse);
          call->replace(result);
        }
      }
    } else if (call->isNamed("_type_construct__tuple") && !call->isResolved()) {
      if (SymExpr* sym = toSymExpr(call->get(1))) {
        if (VarSymbol* var = toVarSymbol(sym->var)) {
          if (var->immediate) {
            int rank = var->immediate->int_value();
            if (rank != call->numActuals() - 1) {
              if (call->numActuals() != 2)
                INT_FATAL(call, "bad homogeneous tuple");
              Expr* actual = call->get(2);
              for (int i = 1; i < rank; i++) {
                call->insertAtTail(actual->copy());
              }
            }
          }
        }
      }
    } else if (call->isPrimitive(PRIM_BLOCK_PARAM_LOOP)) {
      fold_param_for(call);
//     } else if (call->isPrimitive(PRIM_BLOCK_FOR_LOOP) &&
//                call->numActuals() == 2) {
//       result = expand_for_loop(call);
    } else if (call->isPrimitive(PRIM_LOGICAL_FOLDER)) {
      bool removed = false;
      SymExpr* sym1 = toSymExpr(call->get(1));
      if (VarSymbol* sym = toVarSymbol(sym1->var)) {
        if (sym->immediate || paramMap.get(sym)) {
          CallExpr* mvCall = toCallExpr(call->parentExpr);
          SymExpr* sym = toSymExpr(mvCall->get(1));
          VarSymbol* var = toVarSymbol(sym->var);
          removed = true;
          var->addFlag(FLAG_MAYBE_PARAM);
          result = call->get(2)->remove();
          call->replace(result);
        }
      }
      if (!removed) {
        result = call->get(2)->remove();
        call->replace(result);
      }
    } else if (call->isPrimitive(PRIM_SET_REF)) {
      // remove set ref if already a reference
      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_REF)) {
        result = call->get(1)->remove();
        call->replace(result);
      } else {
        FnSymbol* fn = call->getFunction();
        if (!fn->hasFlag(FLAG_WRAPPER)) {
          // check legal var function return
          if (CallExpr* move = toCallExpr(call->parentExpr)) {
            if (move->isPrimitive(PRIM_MOVE)) {
              SymExpr* lhs = toSymExpr(move->get(1));
              if (lhs->var == fn->getReturnSymbol()) {
                SymExpr* ret = toSymExpr(call->get(1));
                INT_ASSERT(ret);
                if (ret->var->defPoint->getFunction() == move->getFunction() &&
                    !ret->var->type->symbol->hasFlag(FLAG_ITERATOR_RECORD) &&
                    !ret->var->type->symbol->hasFlag(FLAG_ARRAY))
                  USR_FATAL(ret, "illegal return expression in var function");
                if (ret->var->isConstant() || ret->var->isParameter())
                  USR_FATAL(ret, "var function returns constant value");
              }
            }
          }
          // check legal lvalue
          if (SymExpr* rhs = toSymExpr(call->get(1))) {
            if (rhs->var->hasFlag(FLAG_EXPR_TEMP) || rhs->var->isConstant() || rhs->var->isParameter())
              USR_FATAL(call, "illegal lvalue in assignment");
          }
        }
      }
    } else if (call->isPrimitive(PRIM_GET_REF)) {
      // remove get ref if already a value
      if (!call->get(1)->typeInfo()->symbol->hasFlag(FLAG_REF)) {
        result = call->get(1)->remove();
        call->replace(result);
      }
    } else if (call->isPrimitive(PRIM_TYPE_TO_STRING)) {
      SymExpr* se = toSymExpr(call->get(1));
      INT_ASSERT(se && se->var->hasFlag(FLAG_TYPE_VARIABLE));
      result = new SymExpr(new_StringSymbol(se->var->type->symbol->name));
      call->replace(result);
    } else if (call->isPrimitive(PRIM_GET_LOCALEID)) {
      Type* type = call->get(1)->getValType();

      //
      // ensure .locale (and on) are applied to lvalues or classes
      // (locale type is a class)
      //
      SymExpr* se = toSymExpr(call->get(1));
      if (se->var->hasFlag(FLAG_EXPR_TEMP) && !isClass(type))
        USR_WARN(se, "accessing the locale of a local expression");

      //
      // if .locale is applied to an expression of array or domain
      // wrapper type, apply .locale to the _value field of the
      // wrapper
      //
      if (type->symbol->hasFlag(FLAG_ARRAY) ||
          type->symbol->hasFlag(FLAG_DISTRIBUTION) ||
          type->symbol->hasFlag(FLAG_DOMAIN)) {
        VarSymbol* tmp = newTemp();
        call->getStmtExpr()->insertBefore(new DefExpr(tmp));
        result = new CallExpr("_value", gMethodToken, call->get(1)->remove());
        call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, result));
        call->insertAtTail(tmp);
      }
    } else if (call->isPrimitive(PRIM_TO_LEADER)) {
      FnSymbol* iterator = call->get(1)->typeInfo()->defaultConstructor->getFormal(1)->type->defaultConstructor;
      CallExpr* leaderCall;
      if (FnSymbol* leader = iteratorLeaderMap.get(iterator))
        leaderCall = new CallExpr(leader);
      else
        leaderCall = new CallExpr(iterator->name);
      leaderCall->insertAtTail(new NamedExpr("tag", new SymExpr(gLeaderTag)));
      for_formals(formal, iterator) {
        leaderCall->insertAtTail(new NamedExpr(formal->name, new SymExpr(formal)));
      }
      call->replace(leaderCall);
      result = leaderCall;
    } else if (call->isPrimitive(PRIM_TO_FOLLOWER)) {
      FnSymbol* iterator = call->get(1)->typeInfo()->defaultConstructor->getFormal(1)->type->defaultConstructor;
      CallExpr* followerCall;
      if (FnSymbol* follower = iteratorFollowerMap.get(iterator))
        followerCall = new CallExpr(follower);
      else
        followerCall = new CallExpr(iterator->name);
      followerCall->insertAtTail(new NamedExpr("tag", new SymExpr(gFollowerTag)));
      followerCall->insertAtTail(new NamedExpr("follower", call->get(2)->remove()));
      for_formals(formal, iterator) {
        followerCall->insertAtTail(new NamedExpr(formal->name, new SymExpr(formal)));
      }
      if (call->numActuals() > 1) {
        followerCall->insertAtTail(new NamedExpr("fast", call->get(2)->remove()));
      }
      call->replace(followerCall);
      result = followerCall;
    } else if (call->isPrimitive(PRIM_NEXT_UINT32)) {
      static unsigned int next_region_id = 0;
      result = new SymExpr(new_UIntSymbol(next_region_id, INT_SIZE_32));
      ++next_region_id;
      call->replace(result);
    } else if (call->isPrimitive(PRIM_COUNT_NUM_REALMS)) {
      result = new SymExpr(new_IntSymbol(getNumRealms()));
      call->replace(result);
    } else if (call->isPrimitive(PRIM_IS_STAR_TUPLE_TYPE)) {
      Type* tupleType = call->get(1)->typeInfo();
      INT_ASSERT(tupleType->symbol->hasFlag(FLAG_TUPLE));
      if (tupleType->symbol->hasFlag(FLAG_STAR_TUPLE))
        result = new SymExpr(gTrue);
      else
        result = new SymExpr(gFalse);
      call->replace(result);
    }
  }
  //
  // ensure result of pre-folding is in the AST
  //
  INT_ASSERT(result->parentSymbol);
  return result;
}

static void foldEnumOp(int op, EnumSymbol *e1, EnumSymbol *e2, Immediate *imm) {
  long val1 = -1, val2 = -1, count = 0;
  // ^^^ This is an assumption that "long" on the compiler host is at
  // least as big as "int" on the target.  This is not guaranteed to be true.
  EnumType *type1, *type2;

  type1 = toEnumType(e1->type);
  type2 = toEnumType(e2->type);
  INT_ASSERT(type1 && type2);

  // Loop over the enum values to find the int value of e1
  for_enums(constant, type1) {
    if (!get_int(constant->init, &count)) {
      count++;
    }
    if (constant->sym == e1) {
      val1 = count;
      break;
    }
  }
  // Loop over the enum values to find the int value of e2
  count = 0;
  for_enums(constant, type2) {
    if (!get_int(constant->init, &count)) {
      count++;
    }
    if (constant->sym == e2) {
      val2 = count;
      break;
    }
  }

  // All operators on enum types result in a bool
  imm->const_kind = NUM_KIND_UINT;
  imm->num_index = INT_SIZE_1;
  switch (op) {
    default: INT_FATAL("fold constant op not supported"); break;
    case P_prim_equal:
      imm->v_bool = val1 == val2;
      break;
    case P_prim_notequal:
      imm->v_bool = val1 != val2;
      break;
    case P_prim_less:
      imm->v_bool = val1 < val2;
      break;
    case P_prim_lessorequal:
      imm->v_bool = val1 <= val2;
      break;
    case P_prim_greater:
      imm->v_bool = val1 > val2;
      break;
    case P_prim_greaterorequal:
      imm->v_bool = val1 >= val2;
      break;
  }
}

#define FOLD_CALL1(prim)                                                \
  if (SymExpr* sym = toSymExpr(call->get(1))) {            \
    if (VarSymbol* lhs = toVarSymbol(sym->var)) {          \
      if (lhs->immediate) {                                             \
        Immediate i3;                                                   \
        fold_constant(prim, lhs->immediate, NULL, &i3);                 \
        result = new SymExpr(new_ImmediateSymbol(&i3));                 \
        call->replace(result);                                          \
      }                                                                 \
    }                                                                   \
  }

#define FOLD_CALL2(prim)                                                \
  if (SymExpr* sym = toSymExpr(call->get(1))) {            \
    if (VarSymbol* lhs = toVarSymbol(sym->var)) {          \
      if (lhs->immediate) {                                             \
        if (SymExpr* sym = toSymExpr(call->get(2))) {      \
          if (VarSymbol* rhs = toVarSymbol(sym->var)) {    \
            if (rhs->immediate) {                                       \
              Immediate i3;                                             \
              fold_constant(prim, lhs->immediate, rhs->immediate, &i3); \
              result = new SymExpr(new_ImmediateSymbol(&i3));           \
              call->replace(result);                                    \
            }                                                           \
          }                                                             \
        }                                                               \
      }                                                                 \
    } else if (EnumSymbol* lhs = toEnumSymbol(sym->var)) { \
      if (SymExpr* sym = toSymExpr(call->get(2))) {        \
        if (EnumSymbol* rhs = toEnumSymbol(sym->var)) {    \
          Immediate imm;                                                \
          foldEnumOp(prim, lhs, rhs, &imm);                             \
          result = new SymExpr(new_ImmediateSymbol(&imm));              \
          call->replace(result);                                        \
        }                                                               \
      }                                                                 \
    }                                                                   \
  }

static bool
isSubType(Type* sub, Type* super) {
  if (sub == super)
    return true;
  forv_Vec(Type, parent, sub->dispatchParents) {
    if (isSubType(parent, super))
      return true;
  }
  return false;
}

static void
insertValueTemp(Expr* insertPoint, Expr* actual) {
  if (SymExpr* se = toSymExpr(actual)) {
    if (!se->var->type->refType) {
      VarSymbol* tmp = newTemp(se->var->getValType());
      insertPoint->insertBefore(new DefExpr(tmp));
      insertPoint->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_GET_REF, se->var)));
      se->var = tmp;
    }
  }
}


//
// returns resolved function if the function requires an implicit
// destroy of its returned value
//
FnSymbol*
requiresImplicitDestroy(CallExpr* call) {
  if (FnSymbol* fn = call->isResolved()) {
    FnSymbol* parent = call->getFunction();
    INT_ASSERT(parent);
    if (strcmp(parent->name, "chpl__autoCopy") &&
        (isRecord(fn->retType) ||
         (fn->retType->symbol->hasFlag(FLAG_REF) &&
         (fn->retType->getValType()->symbol->hasFlag(FLAG_ARRAY) ||
          fn->retType->getValType()->symbol->hasFlag(FLAG_DOMAIN)))) &&
        !fn->hasFlag(FLAG_NO_IMPLICIT_COPY) &&
        !fn->hasFlag(FLAG_ITERATOR_FN) &&
        !fn->retType->symbol->hasFlag(FLAG_RUNTIME_TYPE_VALUE) &&
        strcmp(fn->name, "chpl__initCopy") &&
        strcmp(fn->name, "chpl__autoCopy") &&
        strcmp(fn->name, "=") &&
        !fn->hasFlag(FLAG_AUTO_II) &&
        !fn->hasFlag(FLAG_CONSTRUCTOR) &&
        !fn->hasFlag(FLAG_TYPE_CONSTRUCTOR)) {
      return fn;
    }
  }
  return NULL;
}


static Expr*
postFold(Expr* expr) {
  Expr* result = expr;
  if (!expr->parentSymbol)
    return result;
  if (CallExpr* call = toCallExpr(expr)) {
    if (FnSymbol* fn = call->isResolved()) {
      if (fn->retTag == RET_PARAM || fn->hasFlag(FLAG_MAYBE_PARAM)) {
        VarSymbol* ret = toVarSymbol(fn->getReturnSymbol());
        if (ret && ret->immediate) {
          result = new SymExpr(ret);
          expr->replace(result);
        } else if (EnumSymbol* es = toEnumSymbol(fn->getReturnSymbol())) {
          result = new SymExpr(es);
          expr->replace(result);
        } else if (fn->retTag == RET_PARAM) {
          USR_FATAL(call, "param function does not resolve to a param symbol");
        }
      }
      if (fn->hasFlag(FLAG_MAYBE_TYPE) && fn->getReturnSymbol()->hasFlag(FLAG_TYPE_VARIABLE))
        fn->retTag = RET_TYPE;
      if (fn->retTag == RET_TYPE) {
        Symbol* ret = fn->getReturnSymbol();
        if (!ret->type->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE)) {
          result = new SymExpr(ret->type->symbol);
          expr->replace(result);
        }
      }
      if (!strcmp("=", fn->name) && !call->getFunction()->hasFlag(FLAG_WRAPPER)) {
        SymExpr* lhs = toSymExpr(call->get(1));
        if (!lhs)
          INT_FATAL(call, "unexpected case");
        if (lhs->var->hasFlag(FLAG_EXPR_TEMP) || lhs->var->isConstant() || lhs->var->isParameter())
          USR_FATAL(call, "illegal lvalue in assignment");
      }
    } else if (call->isPrimitive(PRIM_MOVE)) {
      bool set = false;
      if (SymExpr* lhs = toSymExpr(call->get(1))) {
        if (lhs->var->hasFlag(FLAG_MAYBE_PARAM) || lhs->var->isParameter()) {
          if (paramMap.get(lhs->var))
            INT_FATAL(call, "parameter set multiple times");
          if (VarSymbol* lhsVar = toVarSymbol(lhs->var))
            INT_ASSERT(!lhsVar->immediate);
          if (SymExpr* rhs = toSymExpr(call->get(2))) {
            if (VarSymbol* rhsVar = toVarSymbol(rhs->var)) {
              if (rhsVar->immediate) {
                paramMap.put(lhs->var, rhsVar);
                lhs->var->defPoint->remove();
                makeNoop(call);
                set = true;
              }
            } else if (EnumSymbol* rhsv = toEnumSymbol(rhs->var)) {
              paramMap.put(lhs->var, rhsv);
              lhs->var->defPoint->remove();
              makeNoop(call);
              set = true;
            } 
          }
          if (!set && lhs->var->isParameter())
            USR_FATAL(call, "Initializing parameter '%s' to value not known at compile time", lhs->var->name);
        }
        if (!set) {
          if (lhs->var->hasFlag(FLAG_MAYBE_TYPE)) {
            if (SymExpr* rhs = toSymExpr(call->get(2))) {
              if (rhs->var->hasFlag(FLAG_TYPE_VARIABLE))
                lhs->var->addFlag(FLAG_TYPE_VARIABLE);
            }
            if (CallExpr* rhs = toCallExpr(call->get(2))) {
              if (FnSymbol* fn = rhs->isResolved()) {
                if (fn->retTag == RET_TYPE)
                  lhs->var->addFlag(FLAG_TYPE_VARIABLE);
              }
              if (rhs->isPrimitive(PRIM_GET_REF)) {
                if (isTypeExpr(rhs->get(1)))
                  lhs->var->addFlag(FLAG_TYPE_VARIABLE);
              }
            }
          }
          if (CallExpr* rhs = toCallExpr(call->get(2))) {
            if (rhs->isPrimitive(PRIM_TYPEOF)) {
              lhs->var->addFlag(FLAG_TYPE_VARIABLE);
            }
            if (FnSymbol* fn = rhs->isResolved()) {
              if (!strcmp(fn->name, "=") && fn->retType == dtVoid) {
                call->replace(rhs->remove());
                result = rhs;
                set = true;
              }
            }
          }
        }
        if (!set) {
          if (lhs->var->hasFlag(FLAG_EXPR_TEMP) &&
              !lhs->var->hasFlag(FLAG_TYPE_VARIABLE)) {
            if (CallExpr* rhsCall = toCallExpr(call->get(2))) {
              if (requiresImplicitDestroy(rhsCall)) {
                lhs->var->addFlag(FLAG_INSERT_AUTO_COPY);
                lhs->var->addFlag(FLAG_INSERT_AUTO_DESTROY);
              }
            }
          }

          if (isReferenceType(lhs->var->type) ||
              lhs->var->type->symbol->hasFlag(FLAG_REF_ITERATOR_CLASS) ||
              lhs->var->type->symbol->hasFlag(FLAG_ARRAY))
            lhs->var->removeFlag(FLAG_EXPR_TEMP);
        }
      }
    } else if (call->isPrimitive(PRIM_GET_MEMBER)) {
      Type* baseType = call->get(1)->getValType();
      const char* memberName = get_string(call->get(2));
      Symbol* sym = baseType->getField(memberName);
      SymExpr* left = toSymExpr(call->get(1));
      if (left && left->var->hasFlag(FLAG_TYPE_VARIABLE)) {
        result = new SymExpr(sym->type->symbol);
        call->replace(result);
      } else if (sym->isParameter()) {
        Vec<Symbol*> keys;
        baseType->substitutions.get_keys(keys);
        forv_Vec(Symbol, key, keys) {
          if (!strcmp(sym->name, key->name)) {
            if (Symbol* value = baseType->substitutions.get(key)) {
              result = new SymExpr(value);
              call->replace(result);
            }
          }
        }
      }
    } else if (call->isPrimitive(PRIM_ISSUBTYPE)) {
      if (isTypeExpr(call->get(1)) || isTypeExpr(call->get(2))) {
        Type* lt = call->get(2)->getValType(); // a:t cast is cast(t,a)
        Type* rt = call->get(1)->getValType();
        if (lt != dtUnknown && rt != dtUnknown && lt != dtAny &&
            rt != dtAny && !lt->symbol->hasFlag(FLAG_GENERIC)) {
          bool is_true = false;
          if (lt->instantiatedFrom == rt)
            is_true = true;
          if (isSubType(lt, rt))
            is_true = true;
          result = (is_true) ? new SymExpr(gTrue) : new SymExpr(gFalse);
          call->replace(result);
        }
      }
    } else if (call->isPrimitive(PRIM_CAST)) {
      Type* t= call->get(1)->typeInfo();
      if (t == dtUnknown)
        INT_FATAL(call, "Unable to resolve type");
      call->get(1)->replace(new SymExpr(t->symbol));
    } else if (call->isPrimitive("chpl_string_compare")) {
      SymExpr* lhs = toSymExpr(call->get(1));
      SymExpr* rhs = toSymExpr(call->get(2));
      INT_ASSERT(lhs && rhs);
      if (lhs->var->isParameter() && rhs->var->isParameter()) {
        const char* lstr = get_string(lhs);
        const char* rstr = get_string(rhs);
        int comparison = strcmp(lstr, rstr);
        result = new SymExpr(new_IntSymbol(comparison));
        call->replace(result);
      }
    } else if (call->isPrimitive("string_concat")) {
      SymExpr* lhs = toSymExpr(call->get(1));
      SymExpr* rhs = toSymExpr(call->get(2));
      INT_ASSERT(lhs && rhs);
      if (lhs->var->isParameter() && rhs->var->isParameter()) {
        const char* lstr = get_string(lhs);
        const char* rstr = get_string(rhs);
        result = new SymExpr(new_StringSymbol(astr(lstr, rstr)));
        call->replace(result);
      }
    } else if (call->isPrimitive("string_length")) {
      SymExpr* se = toSymExpr(call->get(1));
      INT_ASSERT(se);
      if (se->var->isParameter()) {
        const char* str = get_string(se);
        result = new SymExpr(new_IntSymbol(strlen(str), INT_SIZE_32));
        call->replace(result);
      }
    } else if (call->isPrimitive("ascii")) {
      SymExpr* se = toSymExpr(call->get(1));
      INT_ASSERT(se);
      if (se->var->isParameter()) {
        const char* str = get_string(se);
        result = new SymExpr(new_IntSymbol((int)str[0], INT_SIZE_32));
        call->replace(result);
      }
    } else if (call->isPrimitive("string_contains")) {
      SymExpr* lhs = toSymExpr(call->get(1));
      SymExpr* rhs = toSymExpr(call->get(2));
      INT_ASSERT(lhs && rhs);
      if (lhs->var->isParameter() && rhs->var->isParameter()) {
        const char* lstr = get_string(lhs);
        const char* rstr = get_string(rhs);
        result = new SymExpr(strstr(lstr, rstr) ? gTrue : gFalse);
        call->replace(result);
      }
    } else if (call->isPrimitive(PRIM_UNARY_MINUS)) {
      FOLD_CALL1(P_prim_minus);
    } else if (call->isPrimitive(PRIM_UNARY_PLUS)) {
      FOLD_CALL1(P_prim_plus);
    } else if (call->isPrimitive(PRIM_UNARY_NOT)) {
      FOLD_CALL1(P_prim_not);
    } else if (call->isPrimitive(PRIM_UNARY_LNOT)) {
      FOLD_CALL1(P_prim_lnot);
    } else if (call->isPrimitive(PRIM_ADD)) {
      FOLD_CALL2(P_prim_add);
    } else if (call->isPrimitive(PRIM_SUBTRACT)) {
      FOLD_CALL2(P_prim_subtract);
    } else if (call->isPrimitive(PRIM_MULT)) {
      FOLD_CALL2(P_prim_mult);
    } else if (call->isPrimitive(PRIM_DIV)) {
      FOLD_CALL2(P_prim_div);
    } else if (call->isPrimitive(PRIM_MOD)) {
      FOLD_CALL2(P_prim_mod);
    } else if (call->isPrimitive(PRIM_EQUAL)) {
      FOLD_CALL2(P_prim_equal);
    } else if (call->isPrimitive(PRIM_NOTEQUAL)) {
      FOLD_CALL2(P_prim_notequal);
    } else if (call->isPrimitive(PRIM_LESSOREQUAL)) {
      FOLD_CALL2(P_prim_lessorequal);
    } else if (call->isPrimitive(PRIM_GREATEROREQUAL)) {
      FOLD_CALL2(P_prim_greaterorequal);
    } else if (call->isPrimitive(PRIM_LESS)) {
      FOLD_CALL2(P_prim_less);
    } else if (call->isPrimitive(PRIM_GREATER)) {
      FOLD_CALL2(P_prim_greater);
    } else if (call->isPrimitive(PRIM_AND)) {
      FOLD_CALL2(P_prim_and);
    } else if (call->isPrimitive(PRIM_OR)) {
      FOLD_CALL2(P_prim_or);
    } else if (call->isPrimitive(PRIM_XOR)) {
      FOLD_CALL2(P_prim_xor);
    } else if (call->isPrimitive(PRIM_POW)) {
      FOLD_CALL2(P_prim_pow);
    } else if (call->isPrimitive(PRIM_LSH)) {
      FOLD_CALL2(P_prim_lsh);
    } else if (call->isPrimitive(PRIM_RSH)) {
      FOLD_CALL2(P_prim_rsh);
    } else if (call->isPrimitive(PRIM_ARRAY_ALLOC) ||
               call->isPrimitive(PRIM_GPU_ALLOC) ||
               call->isPrimitive(PRIM_SYNC_INIT) ||
               call->isPrimitive(PRIM_SYNC_LOCK) ||
               call->isPrimitive(PRIM_SYNC_UNLOCK) ||
               call->isPrimitive(PRIM_SYNC_WAIT_FULL) ||
               call->isPrimitive(PRIM_SYNC_WAIT_EMPTY) ||
               call->isPrimitive(PRIM_SYNC_SIGNAL_FULL) ||
               call->isPrimitive(PRIM_SYNC_SIGNAL_EMPTY) ||
               call->isPrimitive(PRIM_SINGLE_INIT) ||
               call->isPrimitive(PRIM_SINGLE_LOCK) ||
               call->isPrimitive(PRIM_SINGLE_UNLOCK) ||
               call->isPrimitive(PRIM_SINGLE_WAIT_FULL) ||
               call->isPrimitive(PRIM_SINGLE_SIGNAL_FULL) ||
               call->isPrimitive(PRIM_WRITEEF) ||
               call->isPrimitive(PRIM_WRITEFF) ||
               call->isPrimitive(PRIM_WRITEXF) ||
               call->isPrimitive(PRIM_SYNC_RESET) ||
               call->isPrimitive(PRIM_READFF) ||
               call->isPrimitive(PRIM_READFE) ||
               call->isPrimitive(PRIM_READXX) ||
               call->isPrimitive(PRIM_SYNC_ISFULL) ||
               call->isPrimitive(PRIM_SINGLE_WRITEEF) ||
               call->isPrimitive(PRIM_SINGLE_RESET) ||
               call->isPrimitive(PRIM_SINGLE_READFF) ||
               call->isPrimitive(PRIM_SINGLE_READXX) ||
               call->isPrimitive(PRIM_SINGLE_ISFULL) ||
               call->isPrimitive(PRIM_EXECUTE_TASKS_IN_LIST) ||
               call->isPrimitive(PRIM_FREE_TASK_LIST) ||
               call->isPrimitive(PRIM_CHPL_FREE) ||
               (call->primitive && 
                (!strncmp("_fscan", call->primitive->name, 6) ||
                 !strcmp("_readToEndOfLine", call->primitive->name) ||
                 !strcmp("_now_timer", call->primitive->name)))) {
      //
      // these primitives require temps to dereference actuals
      //   why not do this to all primitives?
      //
      for_actuals(actual, call) {
        insertValueTemp(call->getStmtExpr(), actual);
      }
    }
  } else if (SymExpr* sym = toSymExpr(expr)) {
    if (Symbol* val = paramMap.get(sym->var)) {
      if (sym->var->type != dtUnknown && sym->var->type != val->type) {
        CallExpr* cast = new CallExpr("_cast", sym->var, val);
        sym->replace(cast);
        result = preFold(cast);
      } else {
        sym->var = val;
      }
    }
  }

  if (CondStmt* cond = toCondStmt(result->parentExpr)) {
    if (cond->condExpr == result) {
      if (Expr* expr = cond->fold_cond_stmt()) {
        result = expr;
      } else {
        //
        // push try block
        //
        if (SymExpr* se = toSymExpr(result))
          if (se->var == gTryToken)
            tryStack.add(cond);
      }
    }
  }

  //
  // pop try block and delete else
  //
  if (tryStack.n) {
    if (BlockStmt* block = toBlockStmt(result)) {
      if (tryStack.v[tryStack.n-1]->thenStmt == block) {
        tryStack.v[tryStack.n-1]->replace(block->remove());
        tryStack.pop();
      }
    }
  }

  return result;
}


void
resolveBlock(Expr* body) {
  FnSymbol* fn = toFnSymbol(body->parentSymbol);
  for_exprs_postorder(expr, body) {
    SET_LINENO(expr);
    if (SymExpr* se = toSymExpr(expr))
      if (se->var)
        makeRefType(se->var->type);
    expr = preFold(expr);

    if (fn && fn->retTag == RET_PARAM) {
      if (BlockStmt* block = toBlockStmt(expr)) {
        if (block->blockInfo) {
          USR_FATAL(expr, "param function cannot contain a non-param loop");
        }
      }
      if (BlockStmt* block = toBlockStmt(expr->parentExpr)) {
        if (isCondStmt(block->parentExpr)) {
          USR_FATAL(block->parentExpr,
                    "param function cannot contain a non-param conditional");
        }
      }
      if (paramMap.get(fn->getReturnSymbol())) {
        CallExpr* call = toCallExpr(fn->body->body.tail);
        INT_ASSERT(call);
        INT_ASSERT(call->isPrimitive(PRIM_RETURN));
        call->get(1)->replace(new SymExpr(paramMap.get(fn->getReturnSymbol())));
        return; // param function is resolved
      }
    }

    if (CallExpr* call = toCallExpr(expr)) {
      if (call->isPrimitive(PRIM_ERROR) ||
          call->isPrimitive(PRIM_WARNING)) {
        issueCompilerError(call);
      }
      callStack.add(call);
      resolveCall(call);
      if (!tryFailure && call->isResolved())
        resolveFns(call->isResolved());
      if (tryFailure) {
        if (tryStack.v[tryStack.n-1]->parentSymbol == fn) {
          while (callStack.v[callStack.n-1]->isResolved() != tryStack.v[tryStack.n-1]->elseStmt->parentSymbol) {
            callStack.pop();
            if (callStack.n == 1)
              INT_FATAL(call, "unable to roll back stack due to try block failure");
          }
          BlockStmt* block = tryStack.v[tryStack.n-1]->elseStmt;
          tryStack.v[tryStack.n-1]->replace(block->remove());
          tryStack.pop();
          if (!block->prev)
            block->insertBefore(new CallExpr(PRIM_NOOP));
          expr = block->prev;
          tryFailure = false;
          continue;
        } else {
          return;
        }
      }
      callStack.pop();
    } else if (SymExpr* sym = toSymExpr(expr)) {

      // avoid record constructors via cast
      //  should be fixed by out-of-order resolution
      CallExpr* parent = toCallExpr(sym->parentExpr);
      if (!parent ||
          !parent->isPrimitive(PRIM_ISSUBTYPE) ||
          !sym->var->hasFlag(FLAG_TYPE_VARIABLE)) {

        if (ClassType* ct = toClassType(sym->typeInfo())) {
          if (!ct->symbol->hasFlag(FLAG_GENERIC) &&
              !ct->symbol->hasFlag(FLAG_ITERATOR_CLASS) &&
              !ct->symbol->hasFlag(FLAG_ITERATOR_RECORD)) {
            resolveFormals(ct->defaultTypeConstructor);
            if (resolvedFormals.set_in(ct->defaultTypeConstructor))
              resolveFns(ct->defaultTypeConstructor);
          }
        }
      }
    }
    expr = postFold(expr);
  }
}


static void
computeReturnTypeParamVectors(BaseAST* ast,
                              Symbol* retSymbol,
                              Vec<Type*>& retTypes,
                              Vec<Symbol*>& retParams) {
  if (CallExpr* call = toCallExpr(ast)) {
    if (call->isPrimitive(PRIM_MOVE)) {
      if (SymExpr* sym = toSymExpr(call->get(1))) {
        if (sym->var == retSymbol) {
          if (SymExpr* sym = toSymExpr(call->get(2)))
            retParams.add(sym->var);
          else
            retParams.add(NULL);
          retTypes.add(call->get(2)->typeInfo());
        }
      }
    }
  }
  AST_CHILDREN_CALL(ast, computeReturnTypeParamVectors, retSymbol, retTypes, retParams);
}


static void
replaceSetterArgWithTrue(BaseAST* ast, FnSymbol* fn) {
  if (SymExpr* se = toSymExpr(ast)) {
    if (se->var == fn->setter->sym) {
      se->var = gTrue;
      if (fn->hasFlag(FLAG_ITERATOR_FN))
        USR_WARN(fn, "setter argument is not supported in iterators");
    }
  }
  AST_CHILDREN_CALL(ast, replaceSetterArgWithTrue, fn);
}


static void
replaceSetterArgWithFalse(BaseAST* ast, FnSymbol* fn, Symbol* ret) {
  if (SymExpr* se = toSymExpr(ast)) {
    if (se->var == fn->setter->sym)
      se->var = gFalse;
    else if (se->var == ret) {
      if (CallExpr* move = toCallExpr(se->parentExpr))
        if (move->isPrimitive(PRIM_MOVE))
          if (CallExpr* call = toCallExpr(move->get(2)))
            if (call->isPrimitive(PRIM_SET_REF))
              call->primitive = primitives[PRIM_GET_REF];
    }
  }
  AST_CHILDREN_CALL(ast, replaceSetterArgWithFalse, fn, ret);
}


static void
insertCasts(BaseAST* ast, FnSymbol* fn, Vec<CallExpr*>& casts) {
  if (CallExpr* call = toCallExpr(ast)) {
    if (call->parentSymbol == fn) {
      if (call->isPrimitive(PRIM_MOVE)) {
        if (SymExpr* lhs = toSymExpr(call->get(1))) {
          Expr* rhs = call->get(2);
          Type* rhsType = rhs->typeInfo();
          if (rhsType != lhs->var->type &&
              rhsType->refType != lhs->var->type &&
              rhsType != lhs->var->type->refType) {
            rhs->remove();
            Symbol* tmp = NULL;
            if (SymExpr* se = toSymExpr(rhs)) {
              tmp = se->var;
            } else {
              tmp = newTemp(rhs->typeInfo());
              call->insertBefore(new DefExpr(tmp));
              call->insertBefore(new CallExpr(PRIM_MOVE, tmp, rhs));
            }
            CallExpr* cast = new CallExpr("_cast", lhs->var->type->symbol, tmp);
            call->insertAtTail(cast);
            casts.add(cast);
          }
        }
      }
    }
  }
  AST_CHILDREN_CALL(ast, insertCasts, fn, casts);
}


static void
resolveFns(FnSymbol* fn) {
  if (resolvedFns.get(fn))
    return;
  resolvedFns.put(fn, true);

  if (fn->hasFlag(FLAG_EXTERN))
    return;

  if (fn->hasFlag(FLAG_AUTO_II))
    return;

  //
  // build value function for var functions
  //
  if (fn->retTag == RET_VAR) {
    if (!fn->hasFlag(FLAG_ITERATOR_FN)) {
      FnSymbol* copy = fn->copy();
      copy->addFlag(FLAG_INVISIBLE_FN);
      if (fn->hasFlag(FLAG_NO_IMPLICIT_COPY))
        copy->addFlag(FLAG_NO_IMPLICIT_COPY);
      copy->retTag = RET_VALUE;
      fn->defPoint->insertBefore(new DefExpr(copy));
      fn->valueFunction = copy;
      Symbol* ret = copy->getReturnSymbol();
      replaceSetterArgWithFalse(copy, copy, ret);
      resolveFns(copy);
    }

    replaceSetterArgWithTrue(fn, fn);
  }

  insertFormalTemps(fn);

  resolveBlock(fn->body);

  if (tryFailure) {
    resolvedFns.put(fn, false);
    return;
  }

  if (fn->hasFlag(FLAG_TYPE_CONSTRUCTOR)) {
    ClassType* ct = toClassType(fn->retType);
    if (!ct)
      INT_FATAL(fn, "Constructor has no class type");
    setScalarPromotionType(ct);
    fixTypeNames(ct);
  }

  Symbol* ret = fn->getReturnSymbol();
  Type* retType = ret->type;

  Vec<Type*> retTypes;
  Vec<Symbol*> retParams;
  computeReturnTypeParamVectors(fn, ret, retTypes, retParams);

  if (retType == dtUnknown) {
    if (retTypes.n == 1)
      retType = retTypes.v[0];
    else if (retTypes.n > 1) {
      for (int i = 0; i < retTypes.n; i++) {
        bool best = true;
        for (int j = 0; j < retTypes.n; j++) {
          if (retTypes.v[i] != retTypes.v[j]) {
            bool requireScalarPromotion = false;
            if (!canDispatch(retTypes.v[j], retParams.v[j], retTypes.v[i], fn, &requireScalarPromotion))
              best = false;
            if (requireScalarPromotion)
              best = false;
          }
        }
        if (best) {
          retType = retTypes.v[i];
          break;
        }
      }
    }
  }



  ret->type = retType;
  if (!fn->iteratorInfo) {
    fn->retType = retType;
    if (retTypes.n == 0 && fn->retType == dtUnknown)
      fn->retType = ret->type = dtVoid;
    else if (retType == dtUnknown)
      USR_FATAL(fn, "unable to resolve return type");
  }

  //
  // insert casts as necessary
  //
  if (fn->retTag != RET_PARAM) {
    Vec<CallExpr*> casts;
    insertCasts(fn->body, fn, casts);
    forv_Vec(CallExpr, cast, casts) {
      resolveCall(cast);
      if (cast->isResolved())
        resolveFns(cast->isResolved());
    }
  }

  //
  // mark leaders for inlining
  //
  if (fn->hasFlag(FLAG_ITERATOR_FN)) {
    for_formals(formal, fn) {
      if (formal->type == gLeaderTag->type &&
          paramMap.get(formal) == gLeaderTag) {
        fn->addFlag(FLAG_INLINE_ITERATOR);
      }
    }
  }



  if (fn->hasFlag(FLAG_ITERATOR_FN) && !fn->iteratorInfo) {
    protoIteratorClass(fn);
  }

  if (fn->hasFlag(FLAG_TYPE_CONSTRUCTOR)) {
    forv_Vec(Type, parent, fn->retType->dispatchParents) {
      if (toClassType(parent) && parent != dtValue && parent != dtObject && parent->defaultTypeConstructor) {
        resolveFormals(parent->defaultTypeConstructor);
        if (resolvedFormals.set_in(parent->defaultTypeConstructor))
          resolveFns(parent->defaultTypeConstructor);
      }
    }
    if (ClassType* ct = toClassType(fn->retType)) {
      for_fields(field, ct) {
        if (ClassType* fct = toClassType(field->type)) {
          if (fct->defaultTypeConstructor) {
            resolveFormals(fct->defaultTypeConstructor);
            if (resolvedFormals.set_in(fct->defaultTypeConstructor))
              resolveFns(fct->defaultTypeConstructor);
          }
        }
      }
    }

    //
    // instantiate default constructor
    //
    if (fn->instantiatedFrom) {
      INT_ASSERT(!fn->retType->defaultConstructor);
      FnSymbol* instantiatedFrom = fn->instantiatedFrom;
      while (instantiatedFrom->instantiatedFrom)
        instantiatedFrom = instantiatedFrom->instantiatedFrom;
      CallExpr* call = new CallExpr(instantiatedFrom->retType->defaultConstructor);
      for_formals(formal, fn) {
        if (formal->type == dtMethodToken || formal == fn->_this) {
          call->insertAtTail(formal);
        } else if (paramMap.get(formal)) {
          call->insertAtTail(new NamedExpr(formal->name, new SymExpr(paramMap.get(formal))));
        } else {
          Symbol* field = fn->retType->getField(formal->name);
          if (instantiatedFrom->hasFlag(FLAG_TUPLE)) {
            call->insertAtTail(field);
          } else {
            call->insertAtTail(new NamedExpr(formal->name, new SymExpr(field)));
          }
        }
      }
      fn->insertBeforeReturn(call);
      resolveCall(call);
      fn->retType->defaultConstructor = call->isResolved();
      INT_ASSERT(fn->retType->defaultConstructor);
      //      resolveFns(fn->retType->defaultConstructor);
      call->remove();
    }


    //
    // resolve destructor
    //
    if (ClassType* ct = toClassType(fn->retType)) {
      if (!ct->destructor &&
          !ct->symbol->hasFlag(FLAG_REF)) {
        VarSymbol* tmp = newTemp(ct);
        CallExpr* call = new CallExpr("~chpl_destroy", gMethodToken, tmp);
        fn->insertAtHead(new CallExpr(call));
        fn->insertAtHead(new DefExpr(tmp));
        resolveCall(call);
        resolveFns(call->isResolved());
        ct->destructor = call->isResolved();
        call->remove();
        tmp->defPoint->remove();
      }
    }
  }

  //
  // mark privatized classes
  //
  if (fn->hasFlag(FLAG_PRIVATIZED_CLASS)) {
    if (fn->getReturnSymbol() == gTrue) {
      fn->getFormal(1)->type->symbol->addFlag(FLAG_PRIVATIZED_CLASS);
    }
  }
}


static bool
possible_signature_match(FnSymbol* fn, FnSymbol* gn) {
  if (fn->name != gn->name)
    return false;
  if (fn->numFormals() != gn->numFormals())
    return false;
  for (int i = 3; i <= fn->numFormals(); i++) {
    ArgSymbol* fa = fn->getFormal(i);
    ArgSymbol* ga = gn->getFormal(i);
    if (strcmp(fa->name, ga->name))
      return false;
  }
  return true;
}


static bool
signature_match(FnSymbol* fn, FnSymbol* gn) {
  if (fn->name != gn->name)
    return false;
  if (fn->numFormals() != gn->numFormals())
    return false;
  for (int i = 3; i <= fn->numFormals(); i++) {
    ArgSymbol* fa = fn->getFormal(i);
    ArgSymbol* ga = gn->getFormal(i);
    if (strcmp(fa->name, ga->name))
      return false;
    if (fa->type != ga->type)
      return false;
  }
  return true;
}


//
// add to vector icts all types instantiated from ct
//
static void
collectInstantiatedClassTypes(Vec<Type*>& icts, Type* ct) {
  forv_Vec(TypeSymbol, ts, gTypeSymbols) {
    if (ts->type->defaultTypeConstructor)
      if (!ts->hasFlag(FLAG_GENERIC) &&
          ts->type->defaultTypeConstructor->instantiatedFrom ==
          ct->defaultTypeConstructor)
        icts.add(ts->type);
  }
}


//
// return true if child overrides parent in dispatch table
//
static bool
isVirtualChild(FnSymbol* child, FnSymbol* parent) {
  if (Vec<FnSymbol*>* children = virtualChildrenMap.get(parent)) {
    forv_Vec(FnSymbol*, candidateChild, *children) {
      if (candidateChild == child)
        return true;
    }
  }
  return false;
}


static void
addToVirtualMaps(FnSymbol* pfn, ClassType* ct) {
  forv_Vec(FnSymbol, cfn, ct->methods) {
    if (cfn && !cfn->instantiatedFrom && possible_signature_match(pfn, cfn)) {
      Vec<Type*> types;
      if (ct->symbol->hasFlag(FLAG_GENERIC))
        collectInstantiatedClassTypes(types, ct);
      else
        types.add(ct);
      forv_Vec(Type, type, types) {
        SymbolMap subs;
        if (ct->symbol->hasFlag(FLAG_GENERIC))
          subs.put(cfn->getFormal(2), type->symbol);
        for (int i = 3; i <= cfn->numFormals(); i++) {
          ArgSymbol* arg = cfn->getFormal(i);
          if (arg->intent == INTENT_PARAM) {
            subs.put(arg, paramMap.get(pfn->getFormal(i)));
          } else if (arg->type->symbol->hasFlag(FLAG_GENERIC)) {
            subs.put(arg, pfn->getFormal(i)->type->symbol);
          }
        }
        FnSymbol* fn = cfn;
        if (subs.n) {
          fn = instantiate(fn, &subs, NULL);
          if (fn) {
            if (type->defaultTypeConstructor->instantiationPoint)
              fn->instantiationPoint = type->defaultTypeConstructor->instantiationPoint;
            else
              fn->instantiationPoint = toBlockStmt(type->defaultTypeConstructor->defPoint->parentExpr);
            INT_ASSERT(fn->instantiationPoint);
          }
        }
        if (fn) {
          resolveFormals(fn);
          if (signature_match(pfn, fn)) {
            resolveFns(fn);
            if (fn->retType->symbol->hasFlag(FLAG_ITERATOR_RECORD) &&
                pfn->retType->symbol->hasFlag(FLAG_ITERATOR_RECORD)) {
              if (!isSubType(fn->retType->defaultConstructor->iteratorInfo->getValue->retType,
                  pfn->retType->defaultConstructor->iteratorInfo->getValue->retType)) {
                USR_FATAL_CONT(pfn, "conflicting return type specified for '%s: %s'", toString(pfn),
                               pfn->retType->defaultConstructor->iteratorInfo->getValue->retType->symbol->name);
                USR_FATAL_CONT(fn, "  overridden by '%s: %s'", toString(fn),
                               fn->retType->defaultConstructor->iteratorInfo->getValue->retType->symbol->name);
                USR_STOP();
              } else {
                pfn->retType->dispatchChildren.add_exclusive(fn->retType);
                fn->retType->dispatchParents.add_exclusive(pfn->retType);
                Type* pic = pfn->retType->defaultConstructor->iteratorInfo->iclass;
                Type* ic = fn->retType->defaultConstructor->iteratorInfo->iclass;
                pic->dispatchChildren.add_exclusive(ic);
                ic->dispatchParents.add_exclusive(pic);
                continue; // do not add to virtualChildrenMap; handle in _getIterator
              }
            } else if (!isSubType(fn->retType, pfn->retType)) {
              USR_FATAL_CONT(pfn, "conflicting return type specified for '%s: %s'", toString(pfn), pfn->retType->symbol->name);
              USR_FATAL_CONT(fn, "  overridden by '%s: %s'", toString(fn), fn->retType->symbol->name);
              USR_STOP();
            }
            {
              Vec<FnSymbol*>* fns = virtualChildrenMap.get(pfn);
              if (!fns) fns = new Vec<FnSymbol*>();
              fns->add(fn);
              virtualChildrenMap.put(pfn, fns);
              fn->addFlag(FLAG_VIRTUAL);
              pfn->addFlag(FLAG_VIRTUAL);
            }
            {
              Vec<FnSymbol*>* fns = virtualRootsMap.get(fn);
              if (!fns) fns = new Vec<FnSymbol*>();
              bool added = false;

              //
              // check if parent or child already exists in vector
              //
              for (int i = 0; i < fns->n; i++) {
                //
                // if parent already exists, do not add child to vector
                //
                if (isVirtualChild(pfn, fns->v[i])) {
                  added = true;
                  break;
                }

                //
                // if child already exists, replace with parent
                //
                if (isVirtualChild(fns->v[i], pfn)) {
                    fns->v[i] = pfn;
                    added = true;
                    break;
                }
              }

              if (!added)
                fns->add(pfn);

              virtualRootsMap.put(fn, fns);
            }
          }
        }
      }
    }
  }
}


static void
addAllToVirtualMaps(FnSymbol* fn, ClassType* ct) {
  forv_Vec(Type, t, ct->dispatchChildren) {
    ClassType* ct = toClassType(t);
    if (ct->defaultTypeConstructor &&
        (ct->defaultTypeConstructor->hasFlag(FLAG_GENERIC) ||
         resolvedFns.get(ct->defaultTypeConstructor))) {
      addToVirtualMaps(fn, ct);
    }
    if (!ct->instantiatedFrom)
      addAllToVirtualMaps(fn, ct);
  }
}


static void
buildVirtualMaps() {
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (!fn->hasFlag(FLAG_WRAPPER) && resolvedFns.get(fn) && !fn->hasFlag(FLAG_NO_PARENS) && fn->retTag != RET_PARAM && fn->retTag != RET_TYPE) {
      if (fn->numFormals() > 1) {
        if (fn->getFormal(1)->type == dtMethodToken) {
          if (ClassType* pt = toClassType(fn->getFormal(2)->type)) {
            if (isClass(pt) && !pt->symbol->hasFlag(FLAG_GENERIC)) {
              addAllToVirtualMaps(fn, pt);
            }
          }
        }
      }
    }
  }
}


static void
addVirtualMethodTableEntry(Type* type, FnSymbol* fn, bool exclusive = false) {
  Vec<FnSymbol*>* fns = virtualMethodTable.get(type);
  if (!fns) fns = new Vec<FnSymbol*>();
  if (exclusive) {
    forv_Vec(FnSymbol, f, *fns) {
      if (f == fn)
        return;
    }
  }
  fns->add(fn);
  virtualMethodTable.put(type, fns);
}


void
parseExplainFlag(char* flag, int* line, ModuleSymbol** module) {
  *line = 0;
  if (strcmp(flag, "")) {
    char *token, *str1 = NULL, *str2 = NULL;
    token = strstr(flag, ":");
    if (token) {
      *token = '\0';
      str1 = token+1;
      token = strstr(str1, ":");
      if (token) {
        *token = '\0';
        str2 = token + 1;
      }
    }
    if (str1) {
      if (str2 || !atoi(str1)) {
        forv_Vec(ModuleSymbol, mod, allModules) {
          if (!strcmp(mod->name, str1)) {
            *module = mod;
            break;
          }
        }
        if (!*module)
          USR_FATAL("invalid module name '%s' passed to --explain-call flag", str1);
      } else
        *line = atoi(str1);
      if (str2)
        *line = atoi(str2);
    }
    if (*line == 0)
      *line = -1;
  }
}


static void
computeStandardModuleSet() {
  standardModuleSet.set_add(rootModule->block);
  standardModuleSet.set_add(theProgram->block);

  Vec<ModuleSymbol*> stack;
  stack.add(standardModule);

  while (ModuleSymbol* mod = stack.pop()) {
    if (mod->block->modUses) {
      for_actuals(expr, mod->block->modUses) {
        SymExpr* se = toSymExpr(expr);
        INT_ASSERT(se);
        ModuleSymbol* use = toModuleSymbol(se->var);
        INT_ASSERT(use);
        if (!standardModuleSet.set_in(use->block)) {
          stack.add(use);
          standardModuleSet.set_add(use->block);
        }
      }
    }
  }
}


void
resolve() {
  parseExplainFlag(fExplainCall, &explainCallLine, &explainCallModule);

  computeStandardModuleSet();

  // call _nilType nil so as to not confuse the user
  dtNil->symbol->name = gNil->name;

  bool changed = true;
  while (changed) {
    changed = false;
    forv_Vec(FnSymbol, fn, gFnSymbols) {
      changed = fn->tag_generic() || changed;
    }
  }

  //
  // make it so that arguments with types that have default values for
  // all generic arguments used those defaults
  //
  // markedGeneric is used to identify places where the user inserted
  // '?' (queries) to mark such a type as generic.
  //
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    bool unmark = fn->hasFlag(FLAG_GENERIC);
    for_formals(formal, fn) {
      if (formal->type->hasGenericDefaults) {
        if (!formal->markedGeneric &&
            formal != fn->_this &&
            !formal->hasFlag(FLAG_IS_MEME)) {
          formal->typeExpr = new BlockStmt(new CallExpr(formal->type->defaultTypeConstructor));
          insert_help(formal->typeExpr, NULL, formal);
          formal->type = dtUnknown;
        } else {
          unmark = false;
        }
      } else if (formal->type->symbol->hasFlag(FLAG_GENERIC) || formal->intent == INTENT_PARAM) {
        unmark = false;
      }
    }
    if (unmark) {
      fn->removeFlag(FLAG_GENERIC);
      INT_ASSERT(false);
    }
  }

  resolveFns(chpl_main);
  USR_STOP();

  if (fRuntime) {
    forv_Vec(FnSymbol, fn, gFnSymbols) {
      if (fn->hasFlag(FLAG_EXPORT)) {
        resolveFormals(fn);
        resolveFns(fn);
      }
    }
  }

  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->hasFlag(FLAG_SEPARATELY_TYPE_CHECKED)) {
      resolveFormals(fn);
      resolveFns(fn);
    }
  }

  // need to handle enumerated types better
  forv_Vec(TypeSymbol, type, gTypeSymbols) {
    if (EnumType* et = toEnumType(type->type)) {
      for_enums(def, et) {
        if (def->init) {
          resolve_type_expr(def->init);
        }
      }
    }
  }

  inDynamicDispatchResolution = true;
  int num_types;
  do {
    num_types = gTypeSymbols.n;
    {
      Vec<Vec<FnSymbol*>*> values;
      virtualChildrenMap.get_values(values);
      forv_Vec(Vec<FnSymbol*>, value, values) {
        delete value;
      }
    }
    virtualChildrenMap.clear();
    {
      Vec<Vec<FnSymbol*>*> values;
      virtualRootsMap.get_values(values);
      forv_Vec(Vec<FnSymbol*>, value, values) {
        delete value;
      }
    }
    virtualRootsMap.clear();
    buildVirtualMaps();
  } while (num_types != gTypeSymbols.n);

  for (int i = 0; i < virtualRootsMap.n; i++) {
    if (virtualRootsMap.v[i].key) {
      for (int j = 0; j < virtualRootsMap.v[i].value->n; j++) {
        FnSymbol* root = virtualRootsMap.v[i].value->v[j];
        addVirtualMethodTableEntry(root->_this->type, root, true);
      }
    }
  }

  Vec<Type*> ctq;
  ctq.add(dtObject);
  forv_Vec(Type, ct, ctq) {
    if (Vec<FnSymbol*>* parentFns = virtualMethodTable.get(ct)) {
      forv_Vec(FnSymbol, pfn, *parentFns) {
        Vec<Type*> childSet;
        if (Vec<FnSymbol*>* childFns = virtualChildrenMap.get(pfn)) {
          forv_Vec(FnSymbol, cfn, *childFns) {
            forv_Vec(Type, pt, cfn->_this->type->dispatchParents) {
              if (pt == ct) {
                if (!childSet.set_in(cfn->_this->type)) {
                  addVirtualMethodTableEntry(cfn->_this->type, cfn);
                  childSet.set_add(cfn->_this->type);
                }
                break;
              }
            }
          }
        }
        forv_Vec(Type, childType, ct->dispatchChildren) {
          if (!childSet.set_in(childType)) {
            addVirtualMethodTableEntry(childType, pfn);
          }
        }
      }
    }
    forv_Vec(Type, child, ct->dispatchChildren) {
      ctq.add(child);
    }
  }

  for (int i = 0; i < virtualMethodTable.n; i++) {
    if (virtualMethodTable.v[i].key) {
      virtualMethodTable.v[i].value->reverse();
      for (int j = 0; j < virtualMethodTable.v[i].value->n; j++) {
        virtualMethodMap.put(virtualMethodTable.v[i].value->v[j], j);
      }
    }
  }
  inDynamicDispatchResolution = false;

  if (fPrintDispatch) {
    printf("Dynamic dispatch table:\n");
    for (int i = 0; i < virtualMethodTable.n; i++) {
      if (Type* t = virtualMethodTable.v[i].key) {
        printf("  %s\n", toString(t));
        for (int j = 0; j < virtualMethodTable.v[i].value->n; j++) {
          printf("    %s\n", toString(virtualMethodTable.v[i].value->v[j]));
        }
      }
    }
  }

  forv_Vec(TypeSymbol, ts, gTypeSymbols) {
    if (ts->defPoint &&
        ts->defPoint->parentSymbol &&
        ts->hasFlag(FLAG_HAS_RUNTIME_TYPE) &&
        !ts->hasFlag(FLAG_GENERIC)) {
      VarSymbol* tmp = newTemp(ts->type);
      ts->type->defaultConstructor->insertBeforeReturn(new DefExpr(tmp));
      CallExpr* call = new CallExpr("chpl__convertValueToRuntimeType", tmp);
      ts->type->defaultConstructor->insertBeforeReturn(call);
      resolveCall(call);
      resolveFns(call->isResolved());
      valueToRuntimeTypeMap.put(ts->type, call->isResolved());
      call->remove();
      tmp->defPoint->remove();
    }
  }

  forv_Vec(TypeSymbol, ts, gTypeSymbols) {
    if ((isRecord(ts->type) ||
         ts->hasFlag(FLAG_SYNC)) &&
        !ts->hasFlag(FLAG_GENERIC) &&
        !ts->hasFlag(FLAG_SYNTACTIC_DISTRIBUTION)) {
      resolveAutoCopy(ts->type);
      resolveAutoDestroy(ts->type);
    }
  }

  //
  // resolve PRIM_INITs for records
  //
  forv_Vec(CallExpr, init, inits) {
    if (init->parentSymbol) {
      Type* type = init->get(1)->typeInfo();
      if (!type->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE)) {
        if (type->symbol->hasFlag(FLAG_REF))
          type = type->getValType();
        if (type->defaultValue) {
          INT_FATAL(init, "PRIM_INIT should have been replaced already");
        } else if (type->symbol->hasFlag(FLAG_ITERATOR_RECORD)) {
          // why??  --sjd
          init->replace(init->get(1)->remove());
        } else if (type->symbol->hasFlag(FLAG_DISTRIBUTION)) {
          Symbol* tmp = newTemp();
          init->getStmtExpr()->insertBefore(new DefExpr(tmp));
          CallExpr* classCall = new CallExpr(type->getField("_valueType")->type->defaultConstructor);
          CallExpr* move = new CallExpr(PRIM_MOVE, tmp, classCall);
          init->getStmtExpr()->insertBefore(move);
          resolveCall(classCall);
          resolveFns(classCall->isResolved());
          resolveCall(move);
          CallExpr* distCall = new CallExpr("chpl__buildDistValue", tmp);
          init->replace(distCall);
          resolveCall(distCall);
          resolveFns(distCall->isResolved());
        } else if (type->symbol->hasFlag(FLAG_EXTERN)) {
          init->replace(init->get(1)->remove());
        } else {
          INT_ASSERT(type->defaultConstructor);
          CallExpr* call = new CallExpr(type->defaultConstructor);
          init->replace(call);
          resolveCall(call);
          if (call->isResolved())
            resolveFns(call->isResolved());
        }
      }
    }
  }

  Vec<CallExpr*> dynamicDispatchCalls;
  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->parentSymbol && call->getStmtExpr())
      if (FnSymbol* fn = call->isResolved())
        if (virtualChildrenMap.get(fn))
          dynamicDispatchCalls.add(call);
  }
  forv_Vec(CallExpr, call, dynamicDispatchCalls) {
    SET_LINENO(call);
    FnSymbol* key = call->isResolved();
    Vec<FnSymbol*>* fns = virtualChildrenMap.get(key);

    //Check to see if any of the overridden methods reference outer variables.  If they do, then when we later change the
    //signature in flattenFunctions, the vtable style will break (function signatures will no longer match).  To avoid this
    //we switch to the if-block style in the case where outer variables are discovered.
    //Note: This is conservative, as we haven't finished resolving functions and calls yet, we check all possibilities. 
    
    bool referencesOuterVars = false;

    Vec<FnSymbol*> seen;
    referencesOuterVars = usesOuterVars(key, seen);

    if (!referencesOuterVars) {    
      for (int i = 0; i < fns->n; ++i) {
        seen.clear();
        if ( (referencesOuterVars = usesOuterVars(key, seen)) ) {
          break;
        }
      }
    }
    
    if ((fns->n + 1 > fConditionalDynamicDispatchLimit) && (!referencesOuterVars)) {
      //
      // change call of root method into virtual method call; replace
      // method token with function
      //
      VarSymbol* cid = newTemp(dtInt[INT_SIZE_32]);
      call->getStmtExpr()->insertBefore(new DefExpr(cid));
      call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, cid, new CallExpr(PRIM_GETCID, call->get(2)->copy())));
      call->get(1)->replace(call->baseExpr->remove());
      call->get(2)->insertBefore(new SymExpr(cid));
      call->primitive = primitives[PRIM_VMT_CALL];
    } else {
      forv_Vec(FnSymbol, fn, *fns) {
        Type* type = fn->getFormal(2)->type;
        CallExpr* subcall = call->copy();
        SymExpr* tmp = new SymExpr(gNil);
        BlockStmt* ifBlock = new BlockStmt();

        VarSymbol* cid = newTemp(dtBool);
        ifBlock->insertAtTail(new DefExpr(cid));
        ifBlock->insertAtTail(new CallExpr(PRIM_MOVE, cid,
                                new CallExpr(PRIM_TESTCID,
                                             call->get(2)->copy(),
                                             type->symbol)));
        VarSymbol* _ret = NULL;
        if (key->retType != dtVoid) {
          _ret = newTemp(key->retType);
          ifBlock->insertAtTail(new DefExpr(_ret));
        }
        BlockStmt* trueBlock = new BlockStmt();
        if (fn->retType == key->retType) {
          if (_ret)
            trueBlock->insertAtTail(new CallExpr(PRIM_MOVE, _ret, subcall));
          else
            trueBlock->insertAtTail(subcall);
        } else if (isSubType(fn->retType, key->retType)) {
          // Insert a cast to the overridden method's return type
          VarSymbol* castTemp = newTemp(fn->retType);
          trueBlock->insertAtTail(new DefExpr(castTemp));
          trueBlock->insertAtTail(new CallExpr(PRIM_MOVE, castTemp,
                                               subcall));
          INT_ASSERT(_ret);
          trueBlock->insertAtTail(new CallExpr(PRIM_MOVE, _ret,
                                    new CallExpr(PRIM_CAST,
                                                 key->retType->symbol,
                                                 castTemp)));
        } else
          INT_FATAL(key, "unexpected case");
        BlockStmt* falseBlock = NULL;
        if (_ret)
          falseBlock = new BlockStmt(new CallExpr(PRIM_MOVE, _ret, tmp));
        else
          falseBlock = new BlockStmt(tmp);
        ifBlock->insertAtTail(new CondStmt(
                                new SymExpr(cid),
                                trueBlock,
                                falseBlock));
        if (key->retType == dtUnknown)
          INT_FATAL(call, "bad parent virtual function return type");
        call->getStmtExpr()->insertBefore(ifBlock);
        if (_ret)
          call->replace(new SymExpr(_ret));
        else
          call->remove();
        tmp->replace(call);
        subcall->baseExpr->replace(new SymExpr(fn));
        if (SymExpr* se = toSymExpr(subcall->get(2))) {
          VarSymbol* tmp = newTemp(type);
          se->getStmtExpr()->insertBefore(new DefExpr(tmp));
          se->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_CAST, type->symbol, se->var)));
          se->replace(new SymExpr(tmp));
        } else if (CallExpr* ce = toCallExpr(subcall->get(2)))
          if (ce->isPrimitive(PRIM_CAST))
            ce->get(1)->replace(new SymExpr(type->symbol));
          else
            INT_FATAL(subcall, "unexpected");
        else
          INT_FATAL(subcall, "unexpected");
      }
    }
  }

  insertReturnTemps(); // must be done before pruneResolvedTree is called.
  pruneResolvedTree();

  freeCache(ordersCache);
  freeCache(defaultsCache);
  freeCache(genericsCache);
  freeCache(coercionsCache);
  freeCache(promotionsCache);

  Vec<VisibleFunctionBlock*> vfbs;
  visibleFunctionMap.get_values(vfbs);
  forv_Vec(VisibleFunctionBlock, vfb, vfbs) {
    Vec<Vec<FnSymbol*>*> vfns;
    vfb->visibleFunctions.get_values(vfns);
    forv_Vec(Vec<FnSymbol*>, vfn, vfns) {
      delete vfn;
    }
    delete vfb;
  }
  visibleFunctionMap.clear();
  visibilityBlockCache.clear();

  checkResolveRemovedPrims();

  resolved = true;
}


static Type*
buildRuntimeTypeInfo(FnSymbol* fn) {
  ClassType* ct = new ClassType(CLASS_RECORD);
  TypeSymbol* ts = new TypeSymbol(astr("_RuntimeTypeInfo"), ct);
  for_formals(formal, fn) {
    if (!formal->instantiatedParam) {
      VarSymbol* field = new VarSymbol(formal->name, formal->type);
      ct->fields.insertAtTail(new DefExpr(field));
      if (formal->hasFlag(FLAG_TYPE_VARIABLE))
        field->addFlag(FLAG_TYPE_VARIABLE);
    }
  }
  theProgram->block->insertAtTail(new DefExpr(ts));
  ct->symbol->addFlag(FLAG_RUNTIME_TYPE_VALUE);
  return ct;
}


static void insertReturnTemps() {
  //
  // Insert return temps for functions that return values if no
  // variable captures the result. If the value is a sync var or a
  // reference to a sync var, pass it through the _statementLevelSymbol
  // function to get the semantics of reading a sync var. If the value
  // is an iterator pass it through another overload of
  // _statementLevelSymbol to iterate through it for side effects.
  //
  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->parentSymbol) {
      if (FnSymbol* fn = call->isResolved()) {
        if (fn->retType != dtVoid) {
          CallExpr* parent = toCallExpr(call->parentExpr);
          if (!parent && !isDefExpr(call->parentExpr)) { // no use
            VarSymbol* tmp = newTemp(fn->retType);
            DefExpr* def = new DefExpr(tmp);
            call->insertBefore(def);
            if ((fn->retType->getValType() &&
                 fn->retType->getValType()->symbol->hasFlag(FLAG_SYNC)) ||
                fn->retType->symbol->hasFlag(FLAG_SYNC) ||
                fn->hasFlag(FLAG_ITERATOR_FN)) {
              CallExpr* sls = new CallExpr("_statementLevelSymbol", tmp);
              call->insertBefore(sls);
              reset_line_info(sls, call->lineno);
              resolveCall(sls);
              INT_ASSERT(sls->isResolved());
              resolveFns(sls->isResolved());
            }
            def->insertAfter(new CallExpr(PRIM_MOVE, tmp, call->remove()));
          }
        }
      }
    }
  }
}


//
// insert code to initialize a class or record
//
static void
initializeClass(Expr* stmt, Symbol* sym) {
  ClassType* ct = toClassType(sym->type);
  INT_ASSERT(ct);
  for_fields(field, ct) {
    if (!field->hasFlag(FLAG_SUPER_CLASS)) {
      if (field->type->defaultValue) {
        stmt->insertBefore(new CallExpr(PRIM_SET_MEMBER, sym, field, field->type->defaultValue));
      } else if (isRecord(field->type)) {
        VarSymbol* tmp = newTemp(field->type);
        stmt->insertBefore(new DefExpr(tmp));
        initializeClass(stmt, tmp);
        stmt->insertBefore(new CallExpr(PRIM_SET_MEMBER, sym, field, tmp));
      }
    }
  }
}



//
// pruneResolvedTree -- prunes and cleans the AST after all of the
// function calls and types have been resolved
//
static void
pruneResolvedTree() {
  // Remove unused functions
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->defPoint && fn->defPoint->parentSymbol) {
      if (!resolvedFns.get(fn) || fn->retTag == RET_PARAM)
        fn->defPoint->remove();
    }
  }

  // Remove unused types
  forv_Vec(TypeSymbol, type, gTypeSymbols) {
    if (type->defPoint && type->defPoint->parentSymbol)
      if (!type->hasFlag(FLAG_REF))
        if (ClassType* ct = toClassType(type->type))
          if (!resolvedFns.get(ct->defaultConstructor) &&
              !resolvedFns.get(ct->defaultTypeConstructor)) {
            if (ct->symbol->hasFlag(FLAG_OBJECT_CLASS))
              dtObject = NULL;
            ct->symbol->defPoint->remove();
          }
  }
  forv_Vec(TypeSymbol, type, gTypeSymbols) {
    if (type->defPoint && type->defPoint->parentSymbol) {
      if (type->hasFlag(FLAG_REF)) {
        if (ClassType* ct = toClassType(type->getValType())) {
          if (!resolvedFns.get(ct->defaultConstructor) &&
              !resolvedFns.get(ct->defaultTypeConstructor)) {
            if (ct->symbol->hasFlag(FLAG_OBJECT_CLASS))
              dtObject = NULL;
            type->defPoint->remove();
          }
        }
        if (type->type->defaultTypeConstructor->defPoint->parentSymbol)
          type->type->defaultTypeConstructor->defPoint->remove();
      }
    }
  }

  Vec<BaseAST*> asts;
  collect_asts_postorder(rootModule, asts);
  forv_Vec(BaseAST, ast, asts) {
    Expr* expr = toExpr(ast);
    if (!expr || !expr->parentSymbol)
      continue;
    if (DefExpr* def = toDefExpr(ast)) {
      // Remove unused global variables
      if (toVarSymbol(def->sym))
        if (toModuleSymbol(def->parentSymbol))
          if (def->sym->type == dtUnknown)
            def->remove();
    } else if (CallExpr* call = toCallExpr(ast)) {
      if (call->isPrimitive(PRIM_NOOP)) {
        // Remove Noops
        call->remove();
      } else if (call->isPrimitive(PRIM_TYPEOF)) {
        // Remove move(x, PRIM_TYPEOF(y)) calls -- useless after this
        CallExpr* parentCall = toCallExpr(call->parentExpr);
        if (parentCall && parentCall->isPrimitive(PRIM_MOVE) && 
            parentCall->get(2) == call) {
          parentCall->remove();
        } else {
          // Replace PRIM_TYPEOF with argument
          call->replace(call->get(1)->remove());
        }
      } else if (call->isPrimitive(PRIM_CAST)) {
        if (call->get(1)->typeInfo() == call->get(2)->typeInfo())
          call->replace(call->get(2)->remove());
      } else if (call->isPrimitive(PRIM_SET_MEMBER) ||
                 call->isPrimitive(PRIM_GET_MEMBER) ||
                 call->isPrimitive(PRIM_GET_MEMBER_VALUE)) {
        // Remove member accesses of types
        // Replace string literals with field symbols in member primitives
        Type* baseType = call->get(1)->typeInfo();
        if (!call->parentSymbol->hasFlag(FLAG_REF) &&
            baseType->symbol->hasFlag(FLAG_REF))
          baseType = baseType->getValType();
        const char* memberName = get_string(call->get(2));
        Symbol* sym = baseType->getField(memberName);
        if ((sym->hasFlag(FLAG_TYPE_VARIABLE) && !sym->type->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE)) ||
            !strcmp(sym->name, "_promotionType") ||
            sym->isParameter())
          call->getStmtExpr()->remove();
        else
          call->get(2)->replace(new SymExpr(sym));
      } else if (call->isPrimitive(PRIM_MOVE)) {
        // Remove types to enable --baseline
        SymExpr* sym = toSymExpr(call->get(2));
        if (sym && isTypeSymbol(sym->var))
          call->remove();
      } else if (FnSymbol* fn = call->isResolved()) {
        // Remove method and leader token actuals
        for (int i = fn->numFormals(); i >= 1; i--) {
          ArgSymbol* formal = fn->getFormal(i);
          if (formal->type == dtMethodToken ||
              formal->instantiatedParam ||
              (formal->hasFlag(FLAG_TYPE_VARIABLE) &&
               !formal->type->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE))) {
            if (!fn->hasFlag(FLAG_EXTERN)) {
              call->get(i)->remove();
            } else {
              // extern function with a type parameter
              INT_ASSERT(toSymExpr(call->get(1)));
              TypeSymbol *ts = toSymExpr(call->get(1))->var->type->symbol;
              // Remove the tmp and just pass the type through directly
              call->get(i)->replace(new SymExpr(ts));
            }
          }
        }
      }
    } else if (NamedExpr* named = toNamedExpr(ast)) {
      // Remove names of named actuals
      Expr* actual = named->actual;
      actual->remove();
      named->replace(actual);
    } else if (BlockStmt* block = toBlockStmt(ast)) {
      // Remove type blocks--code that exists only to determine types
      if (block->blockTag == BLOCK_TYPE)
        block->remove();
    }
  }

  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->defPoint && fn->defPoint->parentSymbol) {
      if (fn->hasFlag(FLAG_HAS_RUNTIME_TYPE)) {
        INT_ASSERT(fn->retType->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE));
        Type* runtimeType = buildRuntimeTypeInfo(fn);
        runtimeTypeMap.put(fn->retType, runtimeType);

        FnSymbol* runtimeTypeToValueFn = fn->copy();
        runtimeTypeToValueFn->name = astr("chpl__convertRuntimeTypeToValue");
        runtimeTypeToValueFn->cname = runtimeTypeToValueFn->name;
        runtimeTypeToValueFn->removeFlag(FLAG_HAS_RUNTIME_TYPE);
        runtimeTypeToValueFn->getReturnSymbol()->removeFlag(FLAG_TYPE_VARIABLE);
        runtimeTypeToValueFn->retTag = RET_VALUE;
        fn->defPoint->insertBefore(new DefExpr(runtimeTypeToValueFn));
        runtimeTypeToValueMap.put(runtimeType, runtimeTypeToValueFn);

        fn->retType = runtimeType;
        fn->getReturnSymbol()->type = runtimeType;
        BlockStmt* block = new BlockStmt();
        VarSymbol* var = newTemp(fn->retType);
        block->insertAtTail(new DefExpr(var));
        for_formals(formal, fn) {
          if (!formal->instantiatedParam) {
            Symbol* field = runtimeType->getField(formal->name);
            if (!formal->hasFlag(FLAG_TYPE_VARIABLE) || field->type->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE))
              block->insertAtTail(new CallExpr(PRIM_SET_MEMBER, var, field, formal));
          }
        }
        block->insertAtTail(new CallExpr(PRIM_RETURN, var));
        fn->body->replace(block);
      }
    }
  }

  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->defPoint && fn->defPoint->parentSymbol) {
      Vec<SymExpr*> symExprs;
      for_formals(formal, fn) {
        // Remove formal default values
        if (formal->defaultExpr)
          formal->defaultExpr->remove();
        // Remove formal type expressions
        if (formal->typeExpr)
          formal->typeExpr->remove();
        // Remove method and leader token formals
        if (formal->type == dtMethodToken || formal->instantiatedParam)
          formal->defPoint->remove();
        if (formal->hasFlag(FLAG_TYPE_VARIABLE) &&
            (!formal->type->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE) &&
             !fn->hasFlag(FLAG_EXTERN))) {
          formal->defPoint->remove();
          VarSymbol* tmp = newTemp(formal->type);
          fn->insertAtHead(new DefExpr(tmp));
          if (symExprs.n == 0)
            collectSymExprs(fn->body, symExprs);
          forv_Vec(SymExpr, se, symExprs) {
            if (se->var == formal) {
              if (CallExpr* call = toCallExpr(se->parentExpr))
                if (call->isPrimitive(PRIM_GET_REF))
                  se->getStmtExpr()->remove();
              se->var = tmp;
            }
          }
        }
        if (formal->hasFlag(FLAG_TYPE_VARIABLE) &&
            formal->type->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE)) {
          if (FnSymbol* fn = valueToRuntimeTypeMap.get(formal->type)) {
            Type* rt = (fn->retType->symbol->hasFlag(FLAG_RUNTIME_TYPE_VALUE)) ?
                        fn->retType : runtimeTypeMap.get(fn->retType);
            INT_ASSERT(rt);
            formal->type =  rt;
            formal->removeFlag(FLAG_TYPE_VARIABLE);
          }
        }
      }
      if (fn->where)
        fn->where->remove();
      if (fn->retTag == RET_TYPE) {
        VarSymbol* ret = toVarSymbol(fn->getReturnSymbol());
        if (ret && ret->type->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE)) {
          if (FnSymbol* rtfn = valueToRuntimeTypeMap.get(ret->type)) {
            Type* rt = (rtfn->retType->symbol->hasFlag(FLAG_RUNTIME_TYPE_VALUE)) ?
                        rtfn->retType : runtimeTypeMap.get(rtfn->retType);
            INT_ASSERT(rt);
            ret->type = rt;
            fn->retType = ret->type;
            fn->retTag = RET_VALUE;
          }
        }
      }
    }
  }

  asts.clear();
  collect_asts_postorder(rootModule, asts);

  forv_Vec(BaseAST, ast, asts) {
    if (DefExpr* def = toDefExpr(ast)) {
      if (isVarSymbol(def->sym) &&
          def->sym->hasFlag(FLAG_TYPE_VARIABLE) &&
          def->sym->type->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE)) {
        Type* rt = runtimeTypeMap.get(def->sym->type);
        INT_ASSERT(rt);
        def->sym->type = rt;
        def->sym->removeFlag(FLAG_TYPE_VARIABLE);
      }
    }
  }

  forv_Vec(BaseAST, ast, asts) {
    if (CallExpr* call = toCallExpr(ast)) {
      if (call->parentSymbol && call->isPrimitive(PRIM_INIT)) {
        SymExpr* se = toSymExpr(call->get(1));
        Type* rt =se->var->type;
        if (rt->symbol->hasFlag(FLAG_RUNTIME_TYPE_VALUE)) {
          SET_LINENO(call);
          FnSymbol* runtimeTypeToValueFn = runtimeTypeToValueMap.get(rt);
          INT_ASSERT(runtimeTypeToValueFn);
          CallExpr* runtimeTypeToValueCall = new CallExpr(runtimeTypeToValueFn);
          for_formals(formal, runtimeTypeToValueFn) {
            Symbol* field = rt->getField(formal->name);
            INT_ASSERT(field);
            VarSymbol* tmp = newTemp(field->type);
            call->getStmtExpr()->insertBefore(new DefExpr(tmp));
            call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp,
                                                           new CallExpr(PRIM_GET_MEMBER_VALUE, se->var, field)));
            if (formal->hasFlag(FLAG_TYPE_VARIABLE))
              tmp->addFlag(FLAG_TYPE_VARIABLE);
            runtimeTypeToValueCall->insertAtTail(tmp);
          }
          VarSymbol* tmp = newTemp(runtimeTypeToValueFn->retType);
          call->getStmtExpr()->insertBefore(new DefExpr(tmp));
          call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, runtimeTypeToValueCall));
          INT_ASSERT(autoCopyMap.get(tmp->type));
          call->replace(new CallExpr(autoCopyMap.get(tmp->type), tmp));
        } else if (rt->symbol->hasFlag(FLAG_HAS_RUNTIME_TYPE)) {
          //
          // This is probably related to a comment that used to handle
          // this case elsewhere:
          //
          // special handling of tuple constructor to avoid
          // initialization of array based on an array type symbol
          // rather than a runtime array type
          //
          // this code added during the introduction of the new
          // keyword; it should be removed when possible
          //
          call->getStmtExpr()->remove();
        } else {
          INT_FATAL(call, "PRIM_INIT should have already been handled");
        }
      }
    } else if (SymExpr* se = toSymExpr(ast)) {

      // remove dead type expressions
      if (se->getStmtExpr() == se)
        if (se->var->hasFlag(FLAG_TYPE_VARIABLE))
          se->remove();

    }
  }

  // Remove type fields, parameter fields, and _promotionType field
  forv_Vec(TypeSymbol, type, gTypeSymbols) {
    if (type->defPoint && type->defPoint->parentSymbol) {
      if (ClassType* ct = toClassType(type->type)) {
        for_fields(field, ct) {
          if (field->hasFlag(FLAG_TYPE_VARIABLE) ||
              field->isParameter() ||
              !strcmp(field->name, "_promotionType"))
            field->defPoint->remove();
        }
      }
    }
  }

  // Remove nilType formals and actuals
  Vec<ArgSymbol*> nilFormalSet;
  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->parentSymbol && call->isResolved()) {
      for_formals_actuals(formal, actual, call) {
        if (formal->type == dtNil) {
          actual->remove();
          nilFormalSet.set_add(formal);
        }
      }
    }
  }
  forv_Vec(ArgSymbol, arg, nilFormalSet) {
    if (arg) {
      FnSymbol* fn = toFnSymbol(arg->defPoint->parentSymbol);
      INT_ASSERT(fn);
      arg->defPoint->remove();
      Vec<SymExpr*> symExprs;
      collectSymExprs(fn, symExprs);
      forv_Vec(SymExpr, se, symExprs) {
        if (se->var == arg)
          se->var = gNil;
      }
    }
  }

  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->parentSymbol && call->isResolved()) {
      //
      // Insert reference temps for function arguments that expect them.
      //
      for_formals_actuals(formal, actual, call) {
        if (formal->type == actual->typeInfo()->refType) {
          SET_LINENO(call);
          VarSymbol* tmp = newTemp(formal->type);
          call->getStmtExpr()->insertBefore(new DefExpr(tmp));
          actual->replace(new SymExpr(tmp));
          call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_SET_REF, actual)));
        }
      }
    } else if (call->isPrimitive(PRIM_INIT_FIELDS)) {
      initializeClass(call, toSymExpr(call->get(1))->var);
      call->remove();
    }
  }
}


static bool
is_array_type(Type* type) {
  forv_Vec(Type, t, type->dispatchParents) {
    if (t->symbol->hasFlag(FLAG_BASE_ARRAY))
      return true;
    else if (is_array_type(t))
      return true;
  }
  return false;
}


static void
fixTypeNames(ClassType* ct)
{
  const char default_domain_name[] = "DefaultRectangularDom";

  if (is_array_type(ct))
  {
    const char* domain_type = ct->getField("dom")->type->symbol->name;
    const char* elt_type = ct->getField("eltType")->type->symbol->name;
    ct->symbol->name = astr("[", domain_type, "] ", elt_type);
  }
  if (ct->instantiatedFrom &&
      !strcmp(ct->instantiatedFrom->symbol->name, default_domain_name)) {
    ct->symbol->name = astr("domain", ct->symbol->name+strlen(default_domain_name));
  }
  if (ct->symbol->hasFlag(FLAG_ARRAY) || ct->symbol->hasFlag(FLAG_DOMAIN)) {
    ct->symbol->name = ct->getField("_valueType")->type->symbol->name;
  }
}


static void
setScalarPromotionType(ClassType* ct) {
  for_fields(field, ct) {
    if (!strcmp(field->name, "_promotionType"))
      ct->scalarPromotionType = field->type;
  }
}
