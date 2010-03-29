#include "expr.h"
#include "iterator.h"
#include "primitive.h"
#include "type.h"

static Type*
returnInfoBool(CallExpr* call) {
  return dtBool;
}

static Type*
returnInfoVoid(CallExpr* call) {
  return dtVoid;
}

static Type*
returnInfoFile(CallExpr* call) {
  return dtFile;
}

static Type*
returnInfoTimer(CallExpr* call) {
  return dtTimer;
}

static Type*
returnInfoString(CallExpr* call) {
  return dtString;
}

static Type*
returnInfoInt32(CallExpr* call) {
  return dtInt[INT_SIZE_32];
}

static Type*
returnInfoInt64(CallExpr* call) {
  return dtInt[INT_SIZE_64];
}

static Type*
returnInfoUInt32(CallExpr* call) { // unexecuted none/gasnet on 4/25/08
  return dtUInt[INT_SIZE_32];
}

static Type*
returnInfoUInt64(CallExpr* call) {
  return dtUInt[INT_SIZE_64];
}

static Type*
returnInfoReal64(CallExpr* call) {
  return dtReal[FLOAT_SIZE_64];
}

static Type*
returnInfoTaskID(CallExpr* call) {
  return dtTaskID;
}

static Type*
returnInfoComplexField(CallExpr* call) {  // for get real/imag primitives
  Type *t = call->get(1)->getValType();
  if (t == dtComplex[COMPLEX_SIZE_64]) {
    return dtReal[FLOAT_SIZE_32]->refType;
  } else if (t == dtComplex[COMPLEX_SIZE_128]) {
    return dtReal[FLOAT_SIZE_64]->refType;
  } else {
    INT_FATAL( call, "unsupported complex size");
  }
  return dtUnknown;
}

static Type*
returnInfoFirst(CallExpr* call) {
  return call->get(1)->typeInfo();
}

static Type*
returnInfoFirstDeref(CallExpr* call) {
  return call->get(1)->getValType();
}

static Type*
returnIteratorType(CallExpr* call) {
  Type* ict = call->get(1)->typeInfo();
  INT_ASSERT(ict->symbol->hasFlag(FLAG_ITERATOR_CLASS));
  return ict->defaultConstructor->getReturnSymbol()->type;
}

static Type*
returnInfoCast(CallExpr* call) {
  Type* t1 = call->get(1)->typeInfo();
  Type* t2 = call->get(2)->typeInfo();
  if (t2->symbol->hasFlag(FLAG_WIDE_CLASS))
    if (wideClassMap.get(t1))
      t1 = wideClassMap.get(t1);
  if (t2->symbol->hasFlag(FLAG_WIDE))
    if (wideRefMap.get(t1))
      t1 = wideRefMap.get(t1);
  return t1;
}

static Type*
returnInfoVal(CallExpr* call) {
  ClassType* ct = toClassType(call->get(1)->typeInfo());
  if (!ct || !ct->symbol->hasFlag(FLAG_REF))
    INT_FATAL(call, "attempt to get value type of non-reference type");
  return ct->getField(1)->type;
}

static Type*
returnInfoRef(CallExpr* call) {
  Type* t = call->get(1)->typeInfo();
  if (!t->refType)
    INT_FATAL(call, "invalid attempt to get reference type");
  return t->refType;
}

// NEEDS TO BE FINISHED WHEN PRIMITIVES ARE REDONE
static Type*
returnInfoNumericUp(CallExpr* call) {
  Type* t1 = call->get(1)->typeInfo();
  Type* t2 = call->get(2)->typeInfo();
  if (is_int_type(t1) && is_real_type(t2))
    return t2;
  if (is_real_type(t1) && is_int_type(t2))
    return t1;
  if (is_int_type(t1) && is_bool_type(t2))
    return t1;
  if (is_bool_type(t1) && is_int_type(t2))
    return t2;
  return t1;
}

static Type*
returnInfoArrayIndexValue(CallExpr* call) {
  SymExpr* sym = toSymExpr(call->get(1));
  INT_ASSERT(sym);
  Type* type = sym->var->type;
  if (type->symbol->hasFlag(FLAG_WIDE_CLASS))
    type = type->getField("addr")->type;
  if (!type->substitutions.n)
    INT_FATAL(call, "bad primitive");
  // Is this conditional necessary?  Can just assume condition is true?
  if (type->symbol->hasFlag(FLAG_DATA_CLASS)) {
    return toTypeSymbol(getDataClassType(type->symbol))->type;
  }
  else {
    return toTypeSymbol(type->substitutions.v[0].value)->type;
  }
}

static Type*
returnInfoArrayIndex(CallExpr* call) {
  return returnInfoArrayIndexValue(call)->refType;
}

static Type*
returnInfoChplAlloc(CallExpr* call) {
  SymExpr* sym = toSymExpr(call->get(1));
  INT_ASSERT(sym);
  Type* type = sym->var->type;
  if (type->symbol->hasFlag(FLAG_WIDE_CLASS))
    type = type->getField("addr")->type;
  return type;
}

static Type*
returnInfoGetMember(CallExpr* call) {
  SymExpr* sym1 = toSymExpr(call->get(1));
  if (!sym1)
    INT_FATAL(call, "bad member primitive");
  ClassType* ct = toClassType(sym1->var->type);
  if (ct->symbol->hasFlag(FLAG_REF))
    ct = toClassType(ct->getValType());
  if (!ct)
    INT_FATAL(call, "bad member primitive");
  SymExpr* sym = toSymExpr(call->get(2));
  if (!sym)
    INT_FATAL(call, "bad member primitive");
  VarSymbol* var = toVarSymbol(sym->var);
  if (!var)
    INT_FATAL(call, "bad member primitive");
  if (var->immediate) {
    const char* name = var->immediate->v_string;
    for_fields(field, ct) {
      if (!strcmp(field->name, name))
        return field->type;
    }
  } else
    return var->type;
  INT_FATAL(call, "bad member primitive");
  return NULL;
}

static Type*
returnInfoGetTupleMember(CallExpr* call) {
  ClassType* ct = toClassType(call->get(1)->getValType());
  INT_ASSERT(ct && ct->symbol->hasFlag(FLAG_STAR_TUPLE));
  return ct->getField("x1")->type;
}

static Type*
returnInfoGetTupleMemberRef(CallExpr* call) {
  Type* type = returnInfoGetTupleMember(call);
  return (type->refType) ? type->refType : type;
}

static Type*
returnInfoGetMemberRef(CallExpr* call) {
  ClassType* ct = toClassType(call->get(1)->getValType());
  INT_ASSERT(ct);
  SymExpr* se = toSymExpr(call->get(2));
  INT_ASSERT(se);
  VarSymbol* var = toVarSymbol(se->var);
  INT_ASSERT(var);
  if (var->immediate) {
    const char* name = var->immediate->v_string;
    for_fields(field, ct) {
      if (!strcmp(field->name, name))
        return field->type->refType ? field->type->refType : field->type;
    }
  } else
    return var->type->refType ? var->type->refType : var->type;
  INT_FATAL(call, "bad member primitive");
  return NULL;
}

static Type*
returnInfoEndCount(CallExpr* call) {
  static Type* endCountType = NULL;
  if (endCountType == NULL) {
    forv_Vec(TypeSymbol, ts, gTypeSymbols) {
      if (!strcmp(ts->name, "_EndCount")) {
        endCountType = ts->type;
        break;
      }
    }
  }
  return endCountType;
}

static Type*
returnInfoTaskList(CallExpr* call) {
  return dtTaskList;
}

static Type*
returnInfoVirtualMethodCall(CallExpr* call) {
  SymExpr* se = toSymExpr(call->get(1));
  INT_ASSERT(se);
  FnSymbol* fn = toFnSymbol(se->var);
  INT_ASSERT(fn);
  return fn->retType;
}

HashMap<const char *, StringHashFns, PrimitiveOp *> primitives_map;

PrimitiveOp* primitives[NUM_KNOWN_PRIMS];

PrimitiveOp::PrimitiveOp(PrimitiveTag atag,
                         const char *aname,
                         Type *(*areturnInfo)(CallExpr*)) :
  tag(atag),
  name(aname),
  returnInfo(areturnInfo),
  isEssential(false),
  passLineno(false),
  isAtomicSafe(false)
{
  primitives_map.put(name, this);
}

static void
prim_def(PrimitiveTag tag, const char* name, Type *(*returnInfo)(CallExpr*),
         bool isEssential = false, bool passLineno = false,
	 bool isAtomicSafe = false) {
  primitives[tag] = new PrimitiveOp(tag, name, returnInfo);
  primitives[tag]->isEssential = isEssential;
  primitives[tag]->passLineno = passLineno;
  primitives[tag]->isAtomicSafe = isAtomicSafe;
}

static void
prim_def(const char* name, Type *(*returnInfo)(CallExpr*),
         bool isEssential = false, bool passLineno = false,
	 bool isAtomicSafe = false) {
  PrimitiveOp* prim = new PrimitiveOp(PRIM_UNKNOWN, name, returnInfo);
  prim->isEssential = isEssential;
  prim->passLineno = passLineno;
  prim->isAtomicSafe = isAtomicSafe;
}


void
initPrimitive() {
  primitives[PRIM_UNKNOWN] = NULL;

  prim_def(PRIM_ACTUALS_LIST, "actuals list", returnInfoVoid);
  prim_def(PRIM_NOOP, "noop", returnInfoVoid);
  prim_def(PRIM_MOVE, "move", returnInfoVoid, false, true);
  prim_def(PRIM_INIT, "init", returnInfoFirstDeref);
  prim_def(PRIM_REF2STR, "ref to string", returnInfoString);
  prim_def(PRIM_RETURN, "return", returnInfoFirst, true);
  prim_def(PRIM_YIELD, "yield", returnInfoFirst, true);
  prim_def(PRIM_UNARY_MINUS, "u-", returnInfoFirst);
  prim_def(PRIM_UNARY_PLUS, "u+", returnInfoFirst);
  prim_def(PRIM_UNARY_NOT, "u~", returnInfoFirst);
  prim_def(PRIM_UNARY_LNOT, "!", returnInfoBool);
  prim_def(PRIM_ADD, "+", returnInfoNumericUp);
  prim_def(PRIM_SUBTRACT, "-", returnInfoNumericUp);
  prim_def(PRIM_MULT, "*", returnInfoNumericUp);
  prim_def(PRIM_DIV, "/", returnInfoNumericUp, true); // div by zero is visible
  prim_def(PRIM_MOD, "%", returnInfoFirst); // mod by zero?
  prim_def(PRIM_LSH, "<<", returnInfoFirst);
  prim_def(PRIM_RSH, ">>", returnInfoFirst);
  prim_def(PRIM_EQUAL, "==", returnInfoBool);
  prim_def(PRIM_NOTEQUAL, "!=", returnInfoBool);
  prim_def(PRIM_LESSOREQUAL, "<=", returnInfoBool);
  prim_def(PRIM_GREATEROREQUAL, ">=", returnInfoBool);
  prim_def(PRIM_LESS, "<", returnInfoBool);
  prim_def(PRIM_GREATER, ">", returnInfoBool);
  prim_def(PRIM_AND, "&", returnInfoFirst);
  prim_def(PRIM_OR, "|", returnInfoFirst);
  prim_def(PRIM_XOR, "^", returnInfoFirst);
  prim_def(PRIM_POW, "**", returnInfoNumericUp);

  prim_def(PRIM_MIN, "_min", returnInfoFirst);
  prim_def(PRIM_MAX, "_max", returnInfoFirst);
  prim_def(PRIM_PROD_ID, "_prod_id", returnInfoFirst);
  prim_def(PRIM_LAND_ID, "_land_id", returnInfoBool);
  prim_def(PRIM_LOR_ID, "_lor_id", returnInfoBool);
  prim_def(PRIM_LXOR_ID, "_lxor_id", returnInfoBool);
  prim_def(PRIM_BAND_ID, "_band_id", returnInfoFirst);
  prim_def(PRIM_BOR_ID, "_bor_id", returnInfoFirst);
  prim_def(PRIM_BXOR_ID, "_bxor_id", returnInfoFirst);

  prim_def(PRIM_SETCID, "setcid", returnInfoVoid, true, true);
  prim_def(PRIM_TESTCID, "testcid", returnInfoBool, false, true);
  prim_def(PRIM_GETCID, "getcid", returnInfoBool, false, true);
  prim_def(PRIM_UNION_SETID, "set_union_id", returnInfoVoid, true, true);
  prim_def(PRIM_UNION_GETID, "get_union_id", returnInfoInt64, false, true);
  prim_def(PRIM_GET_MEMBER, ".", returnInfoGetMemberRef);
  prim_def(PRIM_GET_MEMBER_VALUE, ".v", returnInfoGetMember, false, true);
  prim_def(PRIM_SET_MEMBER, ".=", returnInfoVoid, true, true);
  prim_def(PRIM_CHECK_NIL, "_check_nil", returnInfoVoid, true, true);
  prim_def(PRIM_NEW, "new", returnInfoFirst);
  prim_def(PRIM_GET_REAL, "complex_get_real", returnInfoComplexField);
  prim_def(PRIM_GET_IMAG, "complex_get_imag", returnInfoComplexField);
  prim_def(PRIM_QUERY, "query", returnInfoVoid);

  prim_def(PRIM_INIT_REF, "init ref", returnInfoVoid, true);
  prim_def(PRIM_SET_REF, "set ref", returnInfoRef);
  prim_def(PRIM_GET_REF, "get ref", returnInfoVal, false, true);

  // local block primitives
  prim_def(PRIM_LOCAL_CHECK, "local_check", returnInfoVoid, true, true);

  // operations on sync/single vars
  prim_def(PRIM_SYNC_INIT, "init_sync_aux", returnInfoVoid, true);
  prim_def(PRIM_SYNC_DESTROY, "destroy_sync_aux", returnInfoVoid, true);
  prim_def(PRIM_SYNC_LOCK, "sync_lock", returnInfoVoid, true);
  prim_def(PRIM_SYNC_UNLOCK, "sync_unlock", returnInfoVoid, true);
  prim_def(PRIM_SYNC_WAIT_FULL, "sync_wait_full_and_lock", returnInfoVoid, true, true);
  prim_def(PRIM_SYNC_WAIT_EMPTY, "sync_wait_empty_and_lock", returnInfoVoid, true, true);
  prim_def(PRIM_SYNC_SIGNAL_FULL, "sync_mark_and_signal_full", returnInfoVoid, true);
  prim_def(PRIM_SYNC_SIGNAL_EMPTY, "sync_mark_and_signal_empty", returnInfoVoid, true);
  prim_def(PRIM_SINGLE_INIT, "init_single_aux", returnInfoVoid, true);
  prim_def(PRIM_SINGLE_DESTROY, "destroy_single_aux", returnInfoVoid, true);
  prim_def(PRIM_SINGLE_LOCK, "single_lock", returnInfoVoid, true);
  prim_def(PRIM_SINGLE_UNLOCK, "single_unlock", returnInfoVoid, true);
  prim_def(PRIM_SINGLE_WAIT_FULL, "single_wait_full", returnInfoVoid, true, true);
  prim_def(PRIM_SINGLE_SIGNAL_FULL, "single_mark_and_signal_full", returnInfoVoid, true);

  // sync/single var support
  prim_def(PRIM_WRITEEF, "write_EF", returnInfoVoid, true);
  prim_def(PRIM_WRITEFF, "write_FF", returnInfoVoid, true);
  prim_def(PRIM_WRITEXF, "write_XF", returnInfoVoid, true);
  prim_def(PRIM_SYNC_RESET, "sync_reset", returnInfoVoid, true);
  prim_def(PRIM_READFE, "read_FE", returnInfoFirst, true);
  prim_def(PRIM_READFF, "read_FF", returnInfoFirst, true);
  prim_def(PRIM_READXX, "read_XX", returnInfoFirst, true);
  prim_def(PRIM_SYNC_ISFULL, "sync_is_full", returnInfoBool, true);
  prim_def(PRIM_SINGLE_WRITEEF, "single_write_EF", returnInfoVoid, true);
  prim_def(PRIM_SINGLE_RESET, "single_reset", returnInfoVoid, true);
  prim_def(PRIM_SINGLE_READFF, "single_read_FF", returnInfoFirst, true);
  prim_def(PRIM_SINGLE_READXX, "single_read_XX", returnInfoFirst, true);
  prim_def(PRIM_SINGLE_ISFULL, "single_is_full", returnInfoBool, true);

  prim_def(PRIM_GET_END_COUNT, "get end count", returnInfoEndCount);
  prim_def(PRIM_SET_END_COUNT, "set end count", returnInfoVoid, true);

  prim_def(PRIM_INIT_TASK_LIST, "init to NULL", returnInfoTaskList);
  prim_def(PRIM_PROCESS_TASK_LIST, "process task list", returnInfoVoid, true);
  prim_def(PRIM_EXECUTE_TASKS_IN_LIST, "execute tasks in list", returnInfoVoid, true);
  prim_def(PRIM_FREE_TASK_LIST, "free task list", returnInfoVoid, true);

  // task primitives
  prim_def(PRIM_TASK_ID, "task_id", returnInfoTaskID);
  prim_def(PRIM_TASK_SLEEP, "task sleep", returnInfoVoid, true);
  prim_def(PRIM_GET_SERIAL, "task_get_serial", returnInfoBool);
  prim_def(PRIM_SET_SERIAL, "task_set_serial", returnInfoVoid, true);

  prim_def(PRIM_CHPL_ALLOC, "chpl_alloc", returnInfoChplAlloc, true, true);
  prim_def(PRIM_CHPL_ALLOC_PERMIT_ZERO, "chpl_alloc_permit_zero",
           returnInfoChplAlloc, true, true);
  prim_def(PRIM_CHPL_FREE, "chpl_free", returnInfoVoid, true, true);
  prim_def(PRIM_INIT_FIELDS, "chpl_init_record", returnInfoVoid, true);
  prim_def(PRIM_PTR_EQUAL, "ptr_eq", returnInfoBool);
  prim_def(PRIM_PTR_NOTEQUAL, "ptr_neq", returnInfoBool);
  prim_def(PRIM_ISSUBTYPE, "is_subtype", returnInfoBool);
  prim_def(PRIM_CAST, "cast", returnInfoCast, false, true);
  prim_def(PRIM_DYNAMIC_CAST, "dynamic_cast", returnInfoCast, false, true);
  prim_def(PRIM_TYPEOF, "typeof", returnInfoFirstDeref);
  prim_def(PRIM_GET_ITERATOR_RETURN, "get iterator return", returnIteratorType);
  prim_def(PRIM_USE, "use", returnInfoVoid, true);
  prim_def(PRIM_USED_MODULES_LIST, "used modules list", returnInfoVoid);
  prim_def(PRIM_TUPLE_EXPAND, "expand_tuple", returnInfoVoid);
  prim_def(PRIM_TUPLE_AND_EXPAND, "and_expand_tuple", returnInfoVoid);

  prim_def(PRIM_ARRAY_ALLOC, "array_alloc", returnInfoVoid, true, true);
  prim_def(PRIM_ARRAY_FREE, "array_free", returnInfoVoid, true, true);
  prim_def(PRIM_ARRAY_FREE_ELTS, "array_free_elts", returnInfoVoid, true);
  prim_def(PRIM_ARRAY_GET, "array_get", returnInfoArrayIndex, false, true);
  prim_def(PRIM_ARRAY_GET_VALUE, "array_get_value", returnInfoArrayIndexValue, false, true);

  prim_def(PRIM_GPU_GET_ARRAY, "get_gpu_array", returnInfoArrayIndex, false, true);
  prim_def(PRIM_GPU_GET_VALUE, "get_gpu_value", returnInfoArrayIndex, false, true);
  prim_def(PRIM_GPU_GET_VAL, "get_gpu_val", returnInfoArrayIndex, false, true);
  prim_def(PRIM_GPU_ALLOC, "gpu_alloc", returnInfoVoid, true, true);
  prim_def(PRIM_COPY_HOST_GPU, "copy_host_to_gpu", returnInfoVoid, true, false);
  prim_def(PRIM_COPY_GPU_HOST, "copy_gpu_to_host", returnInfoVoid, true, false);
  prim_def(PRIM_GPU_FREE, "gpu_free", returnInfoVoid, true, true);
  prim_def(PRIM_ON_GPU, "chpl_on_gpu", returnInfoInt32);

  // PRIM_ARRAY_SET is unused by compiler, runtime, modules
  prim_def(PRIM_ARRAY_SET, "array_set", returnInfoVoid, true, true);
  prim_def(PRIM_ARRAY_SET_FIRST, "array_set_first", returnInfoVoid, true, true);

  prim_def(PRIM_ERROR, "error", returnInfoVoid, true);
  prim_def(PRIM_WARNING, "warning", returnInfoVoid, true);
  prim_def(PRIM_WHEN, "when case expressions", returnInfoVoid);
  prim_def(PRIM_TYPE_TO_STRING, "typeToString", returnInfoString);

  prim_def(PRIM_BLOCK_PARAM_LOOP, "param loop", returnInfoVoid);
  prim_def(PRIM_BLOCK_WHILEDO_LOOP, "while...do loop", returnInfoVoid);
  prim_def(PRIM_BLOCK_DOWHILE_LOOP, "do...while loop", returnInfoVoid);
  prim_def(PRIM_BLOCK_FOR_LOOP, "for loop", returnInfoVoid);
  prim_def(PRIM_BLOCK_BEGIN, "begin block", returnInfoVoid);
  prim_def(PRIM_BLOCK_COBEGIN, "cobegin block", returnInfoVoid);
  prim_def(PRIM_BLOCK_COFORALL, "coforall loop", returnInfoVoid);
  prim_def(PRIM_BLOCK_ON, "on block", returnInfoVoid);
  prim_def(PRIM_BLOCK_ON_NB, "non-blocking on block", returnInfoVoid);
  prim_def(PRIM_BLOCK_LOCAL, "local block", returnInfoVoid);

  prim_def(PRIM_BLOCK_ATOMIC, "atomic block", returnInfoVoid, 
	   false, false, true);
  prim_def(PRIM_TX_BEGIN, "tx begin", returnInfoVoid, true, true, true);
  prim_def(PRIM_TX_COMMIT, "tx commit", returnInfoVoid, true, true, true);
  prim_def(PRIM_TX_ARRAY_SET, "tx array set", returnInfoVoid, 
	   true, true, true);
  prim_def(PRIM_TX_ARRAY_ALLOC, "tx array alloc", returnInfoVoid, 
	   true, true, true);
  prim_def(PRIM_TX_ARRAY_FREE, "tx array free", returnInfoVoid,
	   true, true, true);
  prim_def(PRIM_TX_GET_LOCALEID, "tx get locale", returnInfoInt32, 
	   false, true, true);
  prim_def(PRIM_TX_LOAD_LOCALEID, "tx load locale is", returnInfoInt32, 
	   false, true, true);
  prim_def(PRIM_TX_GET_REF, "tx get ref", returnInfoVal, 
	   false, true, true);
  prim_def(PRIM_TX_LOAD_REF, "tx load ref", returnInfoVal, 
	   false, true, true);
  prim_def(PRIM_TX_GET_MEMBER_VALUE, "tx get member value", 
	   returnInfoGetMember, false, true, true);
  prim_def(PRIM_TX_LOAD_MEMBER_VALUE, "tx load member value", 
	   returnInfoGetMember, false, true, true);
  prim_def(PRIM_TX_LOAD_SVEC_MEMBER_VALUE, "tx load svec member value", 
	   returnInfoGetTupleMember, false, true, true);
  prim_def(PRIM_TX_ARRAY_GET, "tx array get", returnInfoArrayIndex, 
	   false, true, true);
  prim_def(PRIM_TX_ARRAY_LOAD, "tx array load", returnInfoArrayIndex, 
	   false, true, true);
  prim_def(PRIM_TX_ARRAY_GET_VALUE, "tx array get value", 
	   returnInfoArrayIndexValue, false, true, true);
  prim_def(PRIM_TX_ARRAY_LOAD_VALUE, "tx array load value", 
 	   returnInfoArrayIndexValue, false, true, true);
  prim_def(PRIM_TX_GET_TEST_CID, "tx get and test cid", returnInfoBool, 
	   false, true, true);
  prim_def(PRIM_TX_LOAD_TEST_CID, "tx load and test cid", returnInfoBool, 
	   false, true, true);
  prim_def(PRIM_TX_PUT, "tx put", returnInfoVoid, true, true, true);
  prim_def(PRIM_TX_STORE_REF, "tx store ref", returnInfoVoid, 
	   true, true, true);
  prim_def(PRIM_TX_STORE, "tx store", returnInfoVoid, true, true, true);
  prim_def(PRIM_TX_SET_CID, "tx set cid", returnInfoVoid, true, true, true);
  prim_def(PRIM_TX_STORE_CID, "tx store cid", returnInfoVoid, 
	   true, true, true);
  prim_def(PRIM_TX_SET_SVEC_MEMBER, "tx set svec member", returnInfoVoid, 
	   true, true, true);
  prim_def(PRIM_TX_STORE_SVEC_MEMBER, "tx store svec member", 
	   returnInfoVoid, true, true, true);
  prim_def(PRIM_TX_SET_MEMBER, "tx set member", returnInfoVoid, 
	   true, true, true);
  prim_def(PRIM_TX_STORE_MEMBER, "tx store member", returnInfoVoid, 
	   true, true, true);
  prim_def(PRIM_TX_CHPL_ALLOC, "tx chpl alloc", returnInfoChplAlloc, 
	   true, true, true);
  prim_def(PRIM_TX_CHPL_ALLOC_PERMIT_ZERO, "tx chpl alloc permit zero", 
	   returnInfoChplAlloc, true, true, true);
  prim_def(PRIM_TX_CHPL_FREE, "chpl_stm_tx_free", returnInfoVoid, 
	   true, true, true);
  prim_def(PRIM_TX_RT_ERROR, "chpl_stm_error", returnInfoVoid, 
	   true, true, true);
  prim_def(PRIM_BLOCK_ATOMIC_IGNORE, "atomic block ignore", returnInfoVoid);
 
  prim_def(PRIM_TO_LEADER, "to leader", returnInfoVoid);
  prim_def(PRIM_TO_FOLLOWER, "to follower", returnInfoVoid);

  prim_def(PRIM_DELETE, "delete class instance", returnInfoVoid);

  prim_def(PRIM_GC_CC_INIT, "_chpl_gc_init", returnInfoVoid);
  prim_def(PRIM_GC_ADD_ROOT, "_addRoot", returnInfoVoid);
  prim_def(PRIM_GC_ADD_NULL_ROOT, "_addNullRoot", returnInfoVoid);
  prim_def(PRIM_GC_DELETE_ROOT, "_deleteRoot", returnInfoVoid);
  prim_def(PRIM_GC_CLEANUP, "_chpl_gc_cleanup", returnInfoVoid);

  prim_def(PRIM_IS_ENUM, "isEnumType", returnInfoBool);
  prim_def(PRIM_IS_TUPLE, "isTupleType", returnInfoBool);
  prim_def(PRIM_CALL_DESTRUCTOR, "call destructor", returnInfoVoid, true);

  prim_def(PRIM_LOGICAL_FOLDER, "_paramFoldLogical", returnInfoBool);

  prim_def(PRIM_NUM_LOCALES, "chpl_comm_default_num_locales", returnInfoInt32);
  prim_def(PRIM_GET_LOCALEID, "_get_locale", returnInfoInt32, false, true);
  prim_def(PRIM_LOCALE_ID, "chpl_localeID", returnInfoInt32);
  prim_def(PRIM_ON_LOCALE_NUM, "chpl_on_locale_num", returnInfoInt32);

  prim_def(PRIM_ALLOC_GVR, "allocchpl_globals_registry", returnInfoVoid);
  prim_def(PRIM_HEAP_REGISTER_GLOBAL_VAR, "_heap_register_global_var", returnInfoVoid, true, true);
  prim_def(PRIM_HEAP_BROADCAST_GLOBAL_VARS, "_heap_broadcast_global_vars", returnInfoVoid, true, true);
  prim_def(PRIM_PRIVATE_BROADCAST, "_private_broadcast", returnInfoVoid, true, true);

  prim_def(PRIM_INT_ERROR, "_internal_error", returnInfoVoid, true);

  prim_def("_config_has_value", returnInfoBool);
  prim_def("_config_get_value", returnInfoString);

  prim_def("fopen", returnInfoFile, true);
  prim_def("fclose", returnInfoInt32, true);
  prim_def("fprintf", returnInfoInt32, true);
  prim_def("fflush", returnInfoInt32, true);
  prim_def("_fscan_literal", returnInfoBool, true, true);
  prim_def("_fscan_string", returnInfoString, true, true);
  prim_def("_fscan_int32", returnInfoInt32, true, true);
  prim_def("_fscan_uint32", returnInfoUInt32, true, true);
  prim_def("_fscan_real64", returnInfoReal64, true, true);
  prim_def("_readToEndOfLine", returnInfoVoid, true);
  prim_def("_format", returnInfoString);
  prim_def("chpl_string_compare", returnInfoInt32, true);
  prim_def("string_contains", returnInfoBool, true);
  prim_def("string_concat", returnInfoString, true, true, true);
  prim_def("string_length", returnInfoInt32);
  prim_def("ascii", returnInfoInt32);
  prim_def("string_index", returnInfoString, true, true);
  prim_def(PRIM_STRING_COPY, "string_copy", returnInfoString, false, true, true);
  prim_def("string_select", returnInfoString, true, true);
  prim_def("string_strided_select", returnInfoString, true, true);
  prim_def("sleep", returnInfoVoid, true);
  prim_def("real2int", returnInfoInt64);
  prim_def("object2int", returnInfoInt64);
  prim_def("chpl_exit_any", returnInfoVoid, true);

  prim_def("complex_set_real", returnInfoVoid, true);
  prim_def("complex_set_imag", returnInfoVoid, true);

  prim_def("get_stdin", returnInfoFile);
  prim_def("get_stdout", returnInfoFile);
  prim_def("get_stderr", returnInfoFile);
  prim_def("get_nullfile", returnInfoFile);
  prim_def(PRIM_GET_ERRNO, "get_errno", returnInfoString);

  prim_def("_init_timer", returnInfoVoid, true);
  prim_def("_now_timer", returnInfoTimer, true);
  prim_def("_seconds_timer", returnInfoReal64, true);
  prim_def("_microseconds_timer", returnInfoReal64, true);
  prim_def("_now_year", returnInfoInt32, true);
  prim_def("_now_month", returnInfoInt32, true);
  prim_def("_now_day", returnInfoInt32, true);
  prim_def("_now_dow", returnInfoInt32, true);
  prim_def("_now_time", returnInfoReal64, true);

  prim_def("chpl_coresPerLocale", returnInfoInt32);
  prim_def("chpl_localeName", returnInfoString);
  prim_def("chpl_maxThreads", returnInfoInt32);
  prim_def("chpl_maxThreadsLimit", returnInfoInt32);
  prim_def(PRIM_CHPL_NUMTHREADS, "chpl_numThreads", returnInfoUInt32);
  prim_def(PRIM_CHPL_NUMIDLETHREADS, "chpl_numIdleThreads", returnInfoUInt32);
  prim_def(PRIM_CHPL_NUMQUEUEDTASKS, "chpl_numQueuedTasks", returnInfoUInt32);
  prim_def(PRIM_CHPL_NUMRUNNINGTASKS, "chpl_numRunningTasks", returnInfoUInt32);
  prim_def(PRIM_CHPL_NUMBLOCKEDTASKS, "chpl_numBlockedTasks", returnInfoInt32);

  prim_def("chpl_printMemTable", returnInfoVoid, true, true);
  prim_def("chpl_printMemStat", returnInfoVoid, true, true);
  prim_def("chpl_memoryUsed", returnInfoUInt64, false, true);
  prim_def("chpl_setMemFlags", returnInfoVoid, true);

  prim_def(PRIM_RT_ERROR, "chpl_error", returnInfoVoid, true, true);
  prim_def(PRIM_RT_WARNING, "chpl_warning", returnInfoVoid, true, true);

  prim_def(PRIM_NEW_PRIV_CLASS, "chpl_newPrivatizedClass", returnInfoVoid, true);
  prim_def(PRIM_NUM_PRIV_CLASSES, "chpl_numPrivatizedClasses", returnInfoInt32);
  prim_def(PRIM_GET_PRIV_CLASS, "chpl_getPrivatizedClass",  returnInfoFirst);

  prim_def("chpl_startVerboseComm", returnInfoVoid, true);
  prim_def("chpl_stopVerboseComm", returnInfoVoid, true);
  prim_def("chpl_startVerboseCommHere", returnInfoVoid, true);
  prim_def("chpl_stopVerboseCommHere", returnInfoVoid, true);

  prim_def("chpl_startCommDiagnostics", returnInfoVoid, true);
  prim_def("chpl_stopCommDiagnostics", returnInfoVoid, true);
  prim_def("chpl_startCommDiagnosticsHere", returnInfoVoid, true);
  prim_def("chpl_stopCommDiagnosticsHere", returnInfoVoid, true);

  prim_def("chpl_numCommGets", returnInfoInt32);
  prim_def("chpl_numCommPuts", returnInfoInt32);
  prim_def("chpl_numCommForks", returnInfoInt32);
  prim_def("chpl_numCommNBForks", returnInfoInt32);
  
  prim_def(PRIM_NEXT_UINT32, "_next_uint32", returnInfoUInt32);
  prim_def(PRIM_GET_USER_LINE, "_get_user_line", returnInfoInt32, true, true);
  prim_def(PRIM_GET_USER_FILE, "_get_user_file", returnInfoString, true, true);

  prim_def(PRIM_COUNT_NUM_REALMS, "get num realms", returnInfoInt32);

  prim_def(PRIM_FTABLE_CALL, "call ftable function", returnInfoVoid, true);

  prim_def(PRIM_IS_STAR_TUPLE_TYPE, "is star tuple type", returnInfoBool);
  prim_def(PRIM_SET_SVEC_MEMBER, "set svec member", returnInfoVoid, true, true);
  prim_def(PRIM_GET_SVEC_MEMBER, "get svec member", returnInfoGetTupleMemberRef);
  prim_def(PRIM_GET_SVEC_MEMBER_VALUE, "get svec member value", returnInfoGetTupleMember, false, true);

  prim_def(PRIM_VMT_CALL, "virtual method call", returnInfoVirtualMethodCall, true, true);
}

Map<const char*, VarSymbol*> memDescsMap;
Map<VarSymbol*, const char*> memDescsNameMap;
Vec<const char*> memDescsVec;

VarSymbol* newMemDesc(const char* str) {
  static int memDescInt = 0;
  const char* s = astr(str);
  if (VarSymbol* v = memDescsMap.get(s))
    return v;
  VarSymbol* memDescVar = new_IntSymbol(memDescInt++, INT_SIZE_16);
  memDescsMap.put(s, memDescVar);
  memDescsVec.add(s);
  memDescsNameMap.put(memDescVar, s);
  return memDescVar;
}
