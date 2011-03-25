#include <cstring>
#include "astutil.h"
#include "expr.h"
#include "misc.h"
#include "passes.h"
#include "stmt.h"
#include "stringutil.h"
#include "type.h"


Expr::Expr(AstTag astTag) :
  BaseAST(astTag),
  prev(NULL),
  next(NULL),
  list(NULL),
  parentExpr(NULL),
  parentSymbol(NULL)
{}


Expr* Expr::getStmtExpr() {
  for (Expr* expr = this; expr; expr = expr->parentExpr) {
    if (IS_STMT(expr) || isBlockStmt(expr->parentExpr))
      return expr;
  }
  return NULL;
}


void Expr::verify() {
  if (prev || next)
    if (!list)
      INT_FATAL(this, "Expr is in list but does not point at it");
  if (!parentSymbol)
    INT_FATAL(this, "Expr::parentSymbol is NULL");
  if (parentExpr && parentExpr->parentSymbol != parentSymbol)
    INT_FATAL(this, "Bad Expr::parentSymbol");
}


bool Expr::inTree(void) {
  if (parentSymbol)
    return parentSymbol->inTree();
  else
    return false;
}


Type* Expr::typeInfo(void) {
  INT_FATAL(this, "Illegal call to Expr::typeInfo()");
  return NULL;
}


static void
callReplaceChild(Expr* expr, Expr* new_ast) {
  if (expr->parentExpr) {
    expr->parentExpr->replaceChild(expr, new_ast);
  } else {
    expr->parentSymbol->replaceChild(expr, new_ast);
  }
}


Expr* Expr::remove(void) {
  if (!this)
    return this;
  if (list) {
    if (next)
      next->prev = prev;
    else
      list->tail = prev;
    if (prev)
      prev->next = next;
    else
      list->head = next;
    list->length--;
    next = NULL;
    prev = NULL;
    list = NULL;
  } else {
    callReplaceChild(this, NULL);
  }
  if (parentSymbol)
    remove_help(this);
  return this;
}


void Expr::replace(Expr* new_ast) {
  if (new_ast->parentSymbol)
    INT_FATAL(new_ast, "Argument is already in AST in Expr::replace");
  if (new_ast->list)
    INT_FATAL(new_ast, "Argument is in a list in Expr::replace");
  if (list) {
    new_ast->next = next;
    new_ast->prev = prev;
    new_ast->list = list;
    if (next)
      next->prev = new_ast;
    else
      list->tail = new_ast;
    if (prev)
      prev->next = new_ast;
    else
      list->head = new_ast;
    next = NULL;
    prev = NULL;
    list = NULL;
  } else {
    callReplaceChild(this, new_ast);
  }

  Symbol* myParentSymbol = parentSymbol;
  Expr* myParentExpr = parentExpr;
  remove_help(this);
  insert_help(new_ast, myParentExpr, myParentSymbol);
}


void Expr::insertBefore(Expr* new_ast) {
  if (new_ast->parentSymbol)
    INT_FATAL(new_ast, "Argument is already in AST in Expr::insertBefore");
  if (!list)
    INT_FATAL(this, "Cannot call insertBefore on Expr not in a list");
  if (new_ast->list)
    INT_FATAL(new_ast, "Argument is in a list in Expr::insertBefore");
  new_ast->prev = prev;
  new_ast->next = this;
  new_ast->list = list;
  if (prev)
    prev->next = new_ast;
  else
    list->head = new_ast;
  prev = new_ast;
  if (parentSymbol)
    sibling_insert_help(this, new_ast);
  list->length++;
}


void Expr::insertAfter(Expr* new_ast) {
  if (new_ast->parentSymbol)
    INT_FATAL(new_ast, "Argument is already in AST in Expr::insertAfter");
  if (!list)
    INT_FATAL(this, "Cannot call insertAfter on Expr not in a list");
  if (new_ast->list)
    INT_FATAL(new_ast, "Argument is in a list in Expr::insertAfter");
  new_ast->prev = this;
  new_ast->next = next;
  new_ast->list = list;
  if (next)
    next->prev = new_ast;
  else
    list->tail = new_ast;
  next = new_ast;
  if (parentSymbol)
    sibling_insert_help(this, new_ast);
  list->length++;
}


SymExpr::SymExpr(Symbol* init_var) :
  Expr(E_SymExpr),
  var(init_var)
{
  if (!init_var)
    INT_FATAL(this, "Bad call to SymExpr");
  gSymExprs.add(this);
}


void 
SymExpr::replaceChild(Expr* old_ast, Expr* new_ast) {
  INT_FATAL(this, "Unexpected case in SymExpr::replaceChild");
}


void
SymExpr::verify() {
  Expr::verify();
  if (astTag != E_SymExpr)
    INT_FATAL(this, "Bad SymExpr::astTag");
  if (!var)
    INT_FATAL(this, "SymExpr::var is NULL");
  if (var && var->defPoint && !var->defPoint->parentSymbol)
    INT_FATAL(this, "SymExpr::var::defPoint is not in AST");
}


SymExpr*
SymExpr::copyInner(SymbolMap* map) {
  return new SymExpr(var);
}


Type* SymExpr::typeInfo(void) {
  return var->type;
}


void SymExpr::codegen(FILE* outfile) {
  if (getStmtExpr() && getStmtExpr() == this)
    codegenStmt(outfile, this);
  var->codegen(outfile);
  if (getStmtExpr() && getStmtExpr() == this)
    fprintf(outfile, ";\n");
}


UnresolvedSymExpr::UnresolvedSymExpr(const char* iunresolved) :
  Expr(E_UnresolvedSymExpr),
  unresolved(astr(iunresolved))
{
  if (!iunresolved)
    INT_FATAL(this, "bad call to UnresolvedSymExpr");
  gUnresolvedSymExprs.add(this);
}


void 
UnresolvedSymExpr::replaceChild(Expr* old_ast, Expr* new_ast) {
  INT_FATAL(this, "unexpected case in UnresolvedSymExpr::replaceChild");
}


void
UnresolvedSymExpr::verify() {
  Expr::verify();
  if (astTag != E_UnresolvedSymExpr)
    INT_FATAL(this, "bad UnresolvedSymExpr::astTag");
  if (!unresolved)
    INT_FATAL(this, "UnresolvedSymExpr::unresolved is NULL");
}


UnresolvedSymExpr*
UnresolvedSymExpr::copyInner(SymbolMap* map) {
  return new UnresolvedSymExpr(unresolved);
}


Type* UnresolvedSymExpr::typeInfo(void) {
  return dtUnknown;
}


void UnresolvedSymExpr::codegen(FILE* outfile) {
  INT_FATAL(this, "UnresolvedSymExpr::codegen called");
  fprintf(outfile, "%s /* unresolved symbol */", unresolved);
}


DefExpr::DefExpr(Symbol* initSym, BaseAST* initInit, BaseAST* initExprType) :
  Expr(E_DefExpr),
  sym(initSym),
  init(NULL),
  exprType(NULL)
{
  if (sym)
    sym->defPoint = this;

  if (Expr* a = toExpr(initInit))
    init = a;
  else if (Symbol* a = toSymbol(initInit))
    init = new SymExpr(a);
  else if (initInit)
    INT_FATAL(this, "DefExpr initialized with bad init ast");

  if (Expr* a = toExpr(initExprType))
    exprType = a;
  else if (Symbol* a = toSymbol(initExprType))
    exprType = new SymExpr(a);
  else if (initExprType)
    INT_FATAL(this, "DefExpr initialized with bad exprType ast");

  if (init && init->parentSymbol)
    INT_FATAL(this, "DefExpr initialized with init already in tree");

  if (exprType && exprType->parentSymbol)
    INT_FATAL(this, "DefExpr initialized with exprType already in tree");

  if (isArgSymbol(sym) && (exprType || init))
    INT_FATAL(this, "DefExpr of ArgSymbol cannot have either exprType or init");

  gDefExprs.add(this);
}


void DefExpr::verify() {
  Expr::verify();
  if (astTag != E_DefExpr) {
    INT_FATAL(this, "Bad DefExpr::astTag");
  }
  if (!sym) {
    INT_FATAL(this, "DefExpr has no sym");
  }
  if (toFnSymbol(sym) && (exprType || init))
    INT_FATAL(this, "Bad FnSymbol::defPoint");
  if (toArgSymbol(sym) && (exprType || init))
    INT_FATAL(this, "Bad ArgSymbol::defPoint");
  if (init && init->parentExpr != this)
    INT_FATAL(this, "Bad DefExpr::init::parentExpr");
  if (exprType && exprType->parentExpr != this)
    INT_FATAL(this, "Bad DefExpr::exprType::parentExpr");
}


DefExpr*
DefExpr::copyInner(SymbolMap* map) {
  return new DefExpr(COPY_INT(sym), COPY_INT(init), COPY_INT(exprType));
}


void DefExpr::replaceChild(Expr* old_ast, Expr* new_ast) {
  if (old_ast == init) {
    init = new_ast;
  } else if (old_ast == exprType) {
    exprType = new_ast;
  } else {
    INT_FATAL(this, "Unexpected case in DefExpr::replaceChild");
  }
}


Type* DefExpr::typeInfo(void) {
  INT_FATAL(this, "Illegal call to DefExpr::typeInfo()");
  return NULL;
}


void DefExpr::codegen(FILE* outfile) {
  if (toLabelSymbol(sym))
    fprintf(outfile, "%s:;\n", sym->cname);
}


static void
codegen_member(FILE* outfile, Expr *base, BaseAST *member) {
  ClassType* ct = toClassType(base->typeInfo());
  INT_ASSERT(ct);
  if (SymExpr* mem = toSymExpr(member)) {
    if (mem->var->hasFlag(FLAG_SUPER_CLASS)) {
      fprintf(outfile, "&(");
    }
  }
  if (ct->symbol->hasFlag(FLAG_REF)) {
    ct = toClassType(ct->getValType());
    fprintf(outfile, "(*");
    base->codegen(outfile);
    fprintf(outfile, ")");
  } else
    base->codegen(outfile);
  if (isClass(ct))
    fprintf(outfile, "->");
  else
    fprintf(outfile, ".");
  if (isUnion(ct))
    fprintf(outfile, "_u.");
  member->codegen(outfile);
  if (SymExpr* mem = toSymExpr(member)) {
    if (mem->var->hasFlag(FLAG_SUPER_CLASS)) {
      fprintf(outfile, ")");
    }
  }
}

static void
codegenExprMinusOne(FILE* outfile, Expr* expr) {
  long i;
  if (get_int(expr, &i)) {
    fprintf(outfile, "%ld", i-1);
  } else {
    expr->codegen(outfile);
    fprintf(outfile, "-1");
  }
}


static void
codegenTupleMember(FILE* outfile, Expr* base, Expr* member) {
  if (base->typeInfo()->symbol->hasFlag(FLAG_REF))
    fprintf(outfile, "(*");
  base->codegen(outfile);
  if (base->typeInfo()->symbol->hasFlag(FLAG_REF))
    fprintf(outfile, ")");
  fprintf(outfile, "[");
  codegenExprMinusOne(outfile, member);
  fprintf(outfile, "]");
}


static void callExprHelper(CallExpr* call, BaseAST* arg) {
  if (!arg)
    return;
  if (toSymbol(arg) || toExpr(arg))
    call->insertAtTail(arg);
  else
    INT_FATAL(call, "Bad argList in CallExpr constructor");
}


CallExpr::CallExpr(BaseAST* base, BaseAST* arg1, BaseAST* arg2,
                   BaseAST* arg3, BaseAST* arg4) :
  Expr(E_CallExpr),
  baseExpr(NULL),
  argList(),
  primitive(NULL),
  partialTag(false),
  methodTag(false),
  square(false)
{
  if (Symbol* b = toSymbol(base)) {
    baseExpr = new SymExpr(b);
  } else if (Expr* b = toExpr(base)) {
    baseExpr = b;
  } else {
    INT_FATAL(this, "Bad baseExpr in CallExpr constructor");
  }
  callExprHelper(this, arg1);
  callExprHelper(this, arg2);
  callExprHelper(this, arg3);
  callExprHelper(this, arg4);
  argList.parent = this;
  gCallExprs.add(this);
}


CallExpr::CallExpr(PrimitiveOp *prim, BaseAST* arg1, BaseAST* arg2, BaseAST* arg3, BaseAST* arg4) :
  Expr(E_CallExpr),
  baseExpr(NULL),
  argList(),
  primitive(prim),
  partialTag(false),
  methodTag(false),
  square(false)
{
  callExprHelper(this, arg1);
  callExprHelper(this, arg2);
  callExprHelper(this, arg3);
  callExprHelper(this, arg4);
  argList.parent = this;
  gCallExprs.add(this);
}

CallExpr::CallExpr(PrimitiveTag prim, BaseAST* arg1, BaseAST* arg2, BaseAST* arg3, BaseAST* arg4) :
  Expr(E_CallExpr),
  baseExpr(NULL),
  argList(),
  primitive(primitives[prim]),
  partialTag(false),
  methodTag(false),
  square(false)
{
  callExprHelper(this, arg1);
  callExprHelper(this, arg2);
  callExprHelper(this, arg3);
  callExprHelper(this, arg4);
  argList.parent = this;
  gCallExprs.add(this);
}


CallExpr::CallExpr(const char* name, BaseAST* arg1, BaseAST* arg2,
                   BaseAST* arg3, BaseAST* arg4) :
  Expr(E_CallExpr),
  baseExpr(new UnresolvedSymExpr(name)),
  argList(),
  primitive(NULL),
  partialTag(false),
  methodTag(false),
  square(false)
{
  callExprHelper(this, arg1);
  callExprHelper(this, arg2);
  callExprHelper(this, arg3);
  callExprHelper(this, arg4);
  argList.parent = this;
  gCallExprs.add(this);
}


CallExpr::~CallExpr() { }


void CallExpr::verify() {
  Expr::verify();
  if (astTag != E_CallExpr) {
    INT_FATAL(this, "Bad CallExpr::astTag");
  }
  if (argList.parent != this)
    INT_FATAL(this, "Bad AList::parent in CallExpr");
  if (normalized && isPrimitive(PRIM_RETURN)) {
    FnSymbol* fn = toFnSymbol(parentSymbol);
    SymExpr* sym = toSymExpr(get(1));
    if (!fn)
      INT_FATAL(this, "Return is not in a function.");
    if (fn->body->body.last() != this)
      INT_FATAL(this, "Return is in middle of function.");
    if (!sym)
      INT_FATAL(this, "Return does not return a symbol.");
  }
  for_actuals(actual, this) {
    if (actual->parentExpr != this)
      INT_FATAL(this, "Bad CallExpr::argList::parentExpr");
  }
  if (resolved) {
    if (FnSymbol* fn = isResolved()) {
      if (!fn->hasFlag(FLAG_EXTERN)) {
        for_formals_actuals(formal, actual, this) {
          if (formal->type != actual->typeInfo() && actual->typeInfo() != dtNil)
            INT_FATAL(this, "actual formal type mismatch");
        }
      }
    }
  }
}


CallExpr*
CallExpr::copyInner(SymbolMap* map) {
  CallExpr *_this = 0;
  if (primitive)
    _this = new CallExpr(primitive);
  else
    _this = new CallExpr(COPY_INT(baseExpr));
  for_actuals(expr, this)
    _this->insertAtTail(COPY_INT(expr));
  _this->primitive = primitive;;
  _this->partialTag = partialTag;
  _this->methodTag = methodTag;
  _this->square = square;
  return _this;
}


void CallExpr::replaceChild(Expr* old_ast, Expr* new_ast) {
  if (old_ast == baseExpr) {
    baseExpr = new_ast;
  } else {
    INT_FATAL(this, "Unexpected case in CallExpr::replaceChild");
  }
}


void
CallExpr::insertAtHead(BaseAST* ast) {
  if (Symbol* a = toSymbol(ast))
    argList.insertAtHead(new SymExpr(a));
  else
    argList.insertAtHead(toExpr(ast));
}


void
CallExpr::insertAtTail(BaseAST* ast) {
  if (Symbol* a = toSymbol(ast))
    argList.insertAtTail(new SymExpr(a));
  else
    argList.insertAtTail(toExpr(ast));
}


FnSymbol* CallExpr::isResolved(void) {
  SymExpr* base = toSymExpr(baseExpr);
  return base ? toFnSymbol(base->var) : NULL;
}


bool CallExpr::isNamed(const char* name) {
  if (SymExpr* base = toSymExpr(baseExpr))
    if (!strcmp(base->var->name, name))
      return true;
  if (UnresolvedSymExpr* base = toUnresolvedSymExpr(baseExpr))
    if (!strcmp(base->unresolved, name))
      return true;
  return false;
}


int CallExpr::numActuals() {
  return argList.length;
}


Expr* CallExpr::get(int index) {
  return argList.get(index);
}


FnSymbol* CallExpr::findFnSymbol(void) {
  FnSymbol* fn = NULL;
  if (SymExpr* variable = toSymExpr(baseExpr))
    fn = toFnSymbol(variable->var);
  if (!fn)
    INT_FATAL(this, "Cannot find FnSymbol in CallExpr");
  return fn;
}


Type* CallExpr::typeInfo(void) {
  if (primitive)
    return primitive->returnInfo(this);
  else if (isResolved())
    return isResolved()->retType;
  else
    return dtUnknown;
}


static void codegenWideDynamicCastCheck(FILE* outfile, Type* type) {
  fprintf(outfile, "chpl_macro_tmp == chpl__cid_%s", type->symbol->cname);
  forv_Vec(Type, child, type->dispatchChildren) {
    fprintf(outfile, " || ");
    codegenWideDynamicCastCheck(outfile, child);
  }
}


static void codegenDynamicCastCheck(FILE* outfile, Type* type, Expr* value) {
  fprintf(outfile, "((");
  dtObject->typeInfo()->codegen(outfile);
  fprintf(outfile, ")");
  value->codegen(outfile);
  fprintf(outfile, ")->chpl__cid == %s%s", "chpl__cid_", type->symbol->cname);
  forv_Vec(Type, child, type->dispatchChildren) {
    fprintf(outfile, " || ");
    codegenDynamicCastCheck(outfile, child, value);
  }
}


static void
codegenNullAssignments(FILE* outfile, const char* cname, ClassType* ct, int skip=0) {
  if (!skip && isClass(ct))
    fprintf(outfile, "%s = NULL;\n", cname);
  else {
    for_fields(field, ct) {
      if (ClassType* fct = toClassType(field->type)) {
        char buffer[1024];
        strcpy(buffer, cname);
        if (skip)
          strcat(buffer, "->");
        else
          strcat(buffer, ".");
        strcat(buffer, field->cname);
        codegenNullAssignments(outfile, buffer, fct, 0);
      }
    }
  }
}


static void
codegenBasicPrimitive(FILE* outfile, CallExpr* call) {
  fprintf(outfile, "%s(", call->primitive->name);
  bool first_actual;
  first_actual = true;
  for_actuals(actual, call) {
    if (first_actual)
      first_actual = false;
    else
      fprintf(outfile, ", ");
    if (actual->typeInfo()->symbol->hasFlag(FLAG_REF) ||
        actual->typeInfo()->symbol->hasFlag(FLAG_WIDE))
      fprintf(outfile, "*");
    actual->codegen(outfile);
    if (actual->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) ||
        actual->typeInfo()->symbol->hasFlag(FLAG_WIDE))
      fprintf(outfile, ".addr");
  }
  fprintf(outfile, ")");
}


static void
gen(FILE* outfile, const char* format, ...) {
  va_list vl;

  va_start(vl, format);
  for (int i = 0; format[i] != '\0'; i++) {
    if (format[i] == '%') {
      i++;
      switch (format[i]) {
      case 'A': {
        BaseAST* ast = va_arg(vl, BaseAST*);
        ast->codegen(outfile);
        break;
      }
      case 'd': {
        int i = va_arg(vl, int);
        fprintf(outfile, "%d", i);
        break;
      }
      case 's': {
        char *str = va_arg(vl, char*);
        fprintf(outfile, "%s", str);
        break;
      }
      case '%': {
        fputc('%', outfile);
        break;
      }
      default:
        INT_FATAL("unknown gen format");
      }
    } else {
      fputc(format[i], outfile);
    }
  }
  va_end(vl);
}


static bool
isTupleOfTuple(Expr *e) {
  return (e->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE) &&
          toDefExpr(toClassType(e->typeInfo())->fields.head)->sym->type->symbol->hasFlag(FLAG_TUPLE));
}

void CallExpr::codegen(FILE* outfile) {
  if (getStmtExpr() && getStmtExpr() == this)
    codegenStmt(outfile, this);

  if (primitive) {
    switch (primitive->tag) {
    case PRIM_UNKNOWN:
      codegenBasicPrimitive(outfile, this);
      break;
    case PRIM_CALL_GPU:
      gen(outfile, "/* Calling GPU kernel */");
      break;
    case PRIM_ARRAY_SET:
    case PRIM_ARRAY_SET_FIRST:
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        Type* classType = get(1)->typeInfo()->getField("addr")->type;
        Type* elementType = getDataClassType(classType->symbol)->type;
        Type* wideElementType = wideRefMap.get(elementType->refType);
        if (elementType->symbol->hasFlag(FLAG_STAR_TUPLE))
          gen(outfile, "CHPL_COMM_WIDE_ARRAY_SET_VALUE_SVEC");
        else
          gen(outfile, "CHPL_COMM_WIDE_ARRAY_SET_VALUE");
        gen(outfile, "(%A, %A, %A, %A, _data, %A, ",
            wideElementType, get(1), get(2), classType, elementType);
        genTypeStructureIndex(outfile, elementType->symbol);
        gen(outfile, ", %A, %A, %A)", get(3), get(4), get(5));
      } else if (getDataClassType(get(1)->typeInfo()->symbol)->type->symbol->hasFlag(FLAG_STAR_TUPLE))
        gen(outfile, "_ARRAY_SET_SVEC(%A, %A, %A)", get(1), get(2), get(3));
      else
        gen(outfile, "_ARRAY_SET(%A, %A, %A)", get(1), get(2), get(3));
      break;
    case PRIM_ARRAY_ALLOC:
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        gen(outfile, "_WIDE_ARRAY_ALLOC(%A, %A, %A, %A, %A)", get(1),
            getDataClassType(get(1)->typeInfo()->getField("addr")->type->symbol),
            get(3), get(4), get(5));
      } else {
        gen(outfile, "_ARRAY_ALLOC(%A, %A, %A, %A, %A)", get(1),
            getDataClassType(get(1)->typeInfo()->symbol),
            get(3), get(4), get(5));
      }
      break;
    case PRIM_GPU_ALLOC:
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        gen(outfile, "_GPU_ALLOC(%A, %A, %A)", get(1),
            getDataClassType(get(1)->typeInfo()->getField("addr")->type->symbol),
            get(3));
      } else {
        fprintf(outfile, "_GPU_ALLOC(");
        get(1)->codegen(outfile);
        fprintf(outfile,", _");
        get(1)->typeInfo()->symbol->codegen(outfile);
        fprintf(outfile,", ");
        get(2)->codegen(outfile);
        fprintf(outfile,", ");
        get(3)->typeInfo()->symbol->codegen(outfile);
        fprintf(outfile,")");
      }
      break;
    case PRIM_COPY_HOST_GPU:
      gen(outfile, "_GPU_COPY_HOST_GPU(%A, %A, %A, %A)",
          get(1), get(2), get(3), get(4));
      break;
    case PRIM_COPY_GPU_HOST:
      gen(outfile, "_GPU_COPY_GPU_HOST(%A, %A, %A, %A)",
          get(1), get(2), get(3), get(4));
      break;
    case PRIM_GPU_FREE:
      if (fNoMemoryFrees)
        break;
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        gen(outfile, "_GPU_FREE(%A)", get(1));
      else
        gen(outfile, "_GPU_FREE(%A)", get(1));
      break;
    case PRIM_ARRAY_FREE:
      if (fNoMemoryFrees)
        break;
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        gen(outfile, "_WIDE_ARRAY_FREE(%A, %A, %A)", get(1), get(2), get(3));
      else
        gen(outfile, "_ARRAY_FREE(%A, %A, %A)", get(1), get(2), get(3));
      break;
    case PRIM_ARRAY_FREE_ELTS:
      if (fNoMemoryFrees)
        break;
      gen(outfile, "_ARRAY_FREE_ELTS(%A, %A, %A)", get(1), get(2), get(3));
      break;
    case PRIM_NOOP:
      break;
    case PRIM_MOVE:
      if (get(1)->typeInfo() == dtVoid) {
        get(2)->codegen(outfile);
        break;
      }
      if (CallExpr* call = toCallExpr(get(2))) {
        if (call->isPrimitive(PRIM_GET_LOCALEID)) {
          if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
            if (call->get(1)->getValType()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
              // get locale field of wide class via wide reference
              gen(outfile, "CHPL_COMM_WIDE_GET_LOCALE(%A, %A, %A, ",
                  get(1), call->get(1),
                  dtLocale->getField("chpl_id")->typeInfo());
              genTypeStructureIndex(outfile,
                                    dtLocale->getField("chpl_id")->typeInfo()->symbol);
              gen(outfile, ", %A, %A)", call->get(2), call->get(3));
            } else {
              gen(outfile, "%A = (%A).locale", get(1), call->get(1));
            }
          } else if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            gen(outfile, "%A = (%A).locale", get(1), call->get(1));
          } else {
            gen(outfile, "%A = chpl_localeID", get(1));
          }
          break;
        }
        if (call->isPrimitive(PRIM_GET_REF)) {
          if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE) ||
              call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            Type* valueType;
            if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE))
              valueType = call->get(1)->getValType();
            else
              valueType = call->get(1)->typeInfo()->getField("addr")->type;
            INT_ASSERT(valueType == get(1)->typeInfo());
            if (valueType == dtString)
              fprintf(outfile, "CHPL_COMM_WIDE_GET_STRING");
            else if (valueType->symbol->hasFlag(FLAG_STAR_TUPLE))
              fprintf(outfile, "CHPL_COMM_WIDE_GET_SVEC");
            else
              fprintf(outfile, "CHPL_COMM_WIDE_GET");
            gen(outfile, "(%A, %A, ", get(1), call->get(1));
            if (valueType != dtString) {
              gen(outfile, "%A, ", valueType);
            }
            genTypeStructureIndex(outfile, valueType->symbol);
            if (valueType != dtString) {
              gen(outfile, ", 1 /*length*/");
            }
            gen(outfile, ", %A, %A)",
                call->get(2), call->get(3));
          } else if (get(1)->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE) ||
                     get(1)->typeInfo()->symbol->hasFlag(FLAG_FIXED_STRING)) {
            if (!fNoTupleCopyOpt &&
                (toClassType(get(1)->typeInfo())->fields.length <=
                 tuple_copy_limit) &&
                !isTupleOfTuple(get(1))) {
              int i = 0;
              for_fields(field, toClassType(get(1)->typeInfo())) {
                if (i != 0) {
                  fprintf(outfile, ";\n");
                }
                gen(outfile, "(%A)[", get(1));
                fprintf(outfile, "%d] =", i);
                gen(outfile, " (*(%A))[", call->get(1));
                fprintf(outfile, "%d]", i);
                i++;
              }
            } else {
              gen(outfile, "CHPL_ASSIGN_SVEC(%A, *(%A))",
                  get(1), call->get(1));
            }
          } else if (call->get(1)->typeInfo() == dtString) {
            // this should be illegal when wide strings are fixed
            gen(outfile, "%A = %A", get(1), call->get(1));
          } else {
            gen(outfile, "%A = *(%A)", get(1), call->get(1));
          }
          break;
        }
        if (call->isPrimitive(PRIM_GET_MEMBER_VALUE)) {
          if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            Type* classType = call->get(1)->typeInfo()->getField("addr")->type;
            SymExpr* se = toSymExpr(call->get(2));
            INT_ASSERT(se);
            if (se->var->hasFlag(FLAG_SUPER_CLASS)) {
              gen(outfile, "CHPL_WIDE_CLASS_GET_SUPER(%A, %A, %A)",
                  get(1)->typeInfo()->getField("addr")->type,
                  get(1), call->get(1));
            } else {
              Type* fieldType = call->get(2)->typeInfo();
              if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
                fprintf(outfile, "CHPL_COMM_WIDE_GET_FIELD_VALUE_SVEC");
              else
                fprintf(outfile, "CHPL_COMM_WIDE_GET_FIELD_VALUE");
              gen(outfile, "(%A, %A, %A, %A, %A, ",
                  get(1), call->get(1), classType, call->get(2), fieldType);
              genTypeStructureIndex(outfile, fieldType->symbol);
              gen(outfile, ", %A, %A)",
                  call->get(3), call->get(4));
            }
          } else if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
            Type* valueType = call->get(1)->getValType();
            Type* fieldType = call->get(2)->typeInfo();
            if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
              fprintf(outfile, "CHPL_COMM_WIDE_GET_FIELD_VALUE_SVEC");
            else
              fprintf(outfile, "CHPL_COMM_WIDE_GET_FIELD_VALUE");
            gen(outfile, "(%A, %A, %A*, ", get(1), call->get(1), valueType);
            if (isUnion(valueType))
              fprintf(outfile, "_u.");
            gen(outfile, "%A, %A, ", call->get(2), fieldType);
            genTypeStructureIndex(outfile, fieldType->symbol);
            gen(outfile, ", %A, %A)", call->get(3), call->get(4));
          } else if (get(2)->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE)) {
            if (!fNoTupleCopyOpt &&
                (toClassType(get(2)->typeInfo())->fields.length <=
                 tuple_copy_limit) &&
                !isTupleOfTuple(get(2))) {
              int i = 0;
              for_fields(field, toClassType(get(2)->typeInfo())) {
                if (i != 0) {
                  fprintf(outfile, ";\n");
                }
                gen(outfile, "(%A)[", get(1));
                fprintf(outfile, "%d] = (", i);
                codegen_member(outfile, call->get(1), call->get(2));
                fprintf(outfile, ")[%d]", i);
                i++;
              }
            } else {
              gen(outfile, "CHPL_ASSIGN_SVEC(%A, ", get(1));
              codegen_member(outfile, call->get(1), call->get(2));
              gen(outfile, ")");
            }
          } else {
            SymExpr* se = toSymExpr(call->get(2));
            INT_ASSERT(se);
            if (se->var->hasFlag(FLAG_SUPER_CLASS) &&
                get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
              gen(outfile, "CHPL_WIDEN(%A, ", get(1));
              codegen_member(outfile, call->get(1), call->get(2));
              gen(outfile, ")");
            } else {
              gen(outfile, "%A = (", get(1));
              codegen_member(outfile, call->get(1), call->get(2));
              gen(outfile, ")");
            }
          }
          break;
        }
        if (call->isPrimitive(PRIM_GET_MEMBER)) {
          if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            Type* classType = call->get(1)->typeInfo()->getField("addr")->type;
            gen(outfile, "CHPL_WIDE_GET_FIELD(%A, %A, %A, %A)",
                get(1), call->get(1), classType, call->get(2));
            break;
          } else if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
            Type* valueType = call->get(1)->getValType();
            gen(outfile, "CHPL_WIDE_GET_FIELD(%A, %A, %A*, ",
                get(1), call->get(1), valueType);
            if (isUnion(valueType))
              fprintf(outfile, "_u.");
            gen(outfile, "%A)", call->get(2));
            break;
          } else if (get(2)->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE)) {
            if (!fNoTupleCopyOpt &&
                (toClassType(get(2)->typeInfo())->fields.length <=
                 tuple_copy_limit) &&
                !isTupleOfTuple(get(2))) {
              int i = 0;
              for_fields(field, toClassType(get(2)->typeInfo())) {
                if (i != 0) {
                  fprintf(outfile, ";\n");
                }
                gen(outfile, "(%A)[", get(1));
                fprintf(outfile, "%d] = (", i);
                codegen_member(outfile, call->get(1), call->get(2));
                fprintf(outfile, ")[%d]", i);
                i++;
              }
            } else {
              gen(outfile, "CHPL_ASSIGN_SVEC(%A, ", get(1));
              codegen_member(outfile, call->get(1), call->get(2));
              fprintf(outfile, ")");
            }
            break;
          }
        }
        if (call->isPrimitive(PRIM_GET_SVEC_MEMBER)) {
          if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
            gen(outfile, "CHPL_WIDE_GET_TUPLE_COMPONENT(%A, %A, ",
                get(1), call->get(1));
            codegenExprMinusOne(outfile, call->get(2));
            fprintf(outfile, ")");
            break;
          }
        }
        if (call->isPrimitive(PRIM_GET_SVEC_MEMBER_VALUE)) {
          if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
            Type* valueType = call->get(1)->getValType();
            Type* fieldType = valueType->getField("x1")->type;
            if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
              fprintf(outfile, "CHPL_COMM_WIDE_GET_TUPLE_COMPONENT_VALUE_SVEC");
            else
              fprintf(outfile, "CHPL_COMM_WIDE_GET_TUPLE_COMPONENT_VALUE");
            gen(outfile, "(%A, %A, ", get(1), call->get(1));
            codegenExprMinusOne(outfile, call->get(2));
            gen(outfile, ", %A, ", fieldType);
            genTypeStructureIndex(outfile, fieldType->symbol);
            gen(outfile, ", %A, %A)", call->get(3), call->get(4));
          } else {
            Type* tupleType = call->get(1)->getValType();
            bool useMemCpy =
              tupleType->getField("x1")->type->symbol->hasFlag(FLAG_STAR_TUPLE);
            if (useMemCpy)
              fprintf(outfile, "CHPL_ASSIGN_SVEC(");
            get(1)->codegen(outfile);
            if (useMemCpy)
              fprintf(outfile, ", ");
            else
              fprintf(outfile, " = ");
            codegenTupleMember(outfile, call->get(1), call->get(2));
            if (useMemCpy)
              fprintf(outfile, ")");
          }
          break;
        }
        if (call->isPrimitive(PRIM_ARRAY_GET)) {
          if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            Type* classType = call->get(1)->typeInfo()->getField("addr")->type;
            Type* eltType = get(1)->typeInfo()->getField("addr")->type;
            gen(outfile, "CHPL_COMM_WIDE_ARRAY_GET(%A, %A, %A, %A, _data, %A, ",
                get(1), call->get(1), call->get(2), classType, eltType);
            genTypeStructureIndex(outfile, eltType->symbol);
            gen(outfile, ", %A, %A)", call->get(3), call->get(4));
          } else {
            gen(outfile, "%A = _ARRAY_GET(%A, %A)",
                get(1), call->get(1), call->get(2));
          }
          break;
        }
        if (call->isPrimitive(PRIM_ARRAY_GET_VALUE)) {
          if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            fprintf(outfile, "CHPL_COMM_WIDE_ARRAY_GET_VALUE(");
            Type* tt = wideRefMap.get(getDataClassType(call->get(1)->typeInfo()->getField("addr")->type->symbol)->type->refType);
            gen(outfile, "%A, %A, %A, %A, ",
                tt->symbol, get(1), call->get(1), call->get(2));
            TypeSymbol* ts0 = call->get(1)->typeInfo()->getField("addr")->type->symbol;
            gen(outfile, "%A, _data, ", ts0);
            TypeSymbol* ts = getDataClassType(call->get(1)->typeInfo()->getField("addr")->type->symbol);
            TypeSymbol* ts2 = tt->getField("addr")->type->symbol;
            gen(outfile, "%A, ", ts2);
            genTypeStructureIndex(outfile, ts2);
            gen(outfile, ", %A, ", ts);
            genTypeStructureIndex(outfile, ts);
            gen(outfile, ", %A, %A)", call->get(3), call->get(4));
          } else {
            gen(outfile, "%A = _ARRAY_GET_VALUE(%A, %A)",
                get(1), call->get(1), call->get(2));
          }
          break;
        }
        if (call->isPrimitive(PRIM_GPU_GET_VALUE)) {
          gen(outfile, "%A = _GPU_GET_VALUE(%A, %A)",
              get(1), call->get(1), call->get(2));
          break;
        }
        if (call->isPrimitive(PRIM_GPU_GET_VAL)) {
          gen(outfile, "%A = _GPU_GET_VAL(%A, %A)",
              get(1), call->get(1), call->get(2));
          break;
        }
        if (call->isPrimitive(PRIM_GPU_GET_ARRAY)) {
          gen(outfile, "%A = _GPU_GET_ARRAY(%A)", get(1), call->get(1));
          break;
        }
        if (call->isPrimitive(PRIM_UNION_GETID)) {
          if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
            gen(outfile,
                "CHPL_COMM_WIDE_GET_FIELD_VALUE(%A, %A, %A*, _uid, %A, ",
                get(1), call->get(1), call->get(1)->getValType(),
                get(1)->typeInfo());
            genTypeStructureIndex(outfile, get(1)->typeInfo()->symbol);
            gen(outfile, ", %A, %A)", call->get(2), call->get(3));
            break;
          }
        }
        if (call->isPrimitive(PRIM_TESTCID)) {
          if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            gen(outfile,
                "CHPL_COMM_WIDE_CLASS_TEST_CID(%A, %A, chpl__cid_%A, %A, ",
                get(1), call->get(1), call->get(2)->typeInfo(),
                dtObject->typeInfo());
            gen(outfile, "chpl__class_id, %s, %A, %A)",
                fHeterogeneous ? "CHPL_TYPE_enum" : "-1",
              call->get(3), call->get(4));
            break;
          }
        }
        if (call->isPrimitive(PRIM_GETCID)) {
          if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            gen(outfile, "CHPL_COMM_WIDE_CLASS_GET_CID(%A, %A, %A, ",
                get(1), call->get(1), dtObject->typeInfo());
            gen(outfile, "chpl__class_id, %s, %A, %A)",
                fHeterogeneous ? "CHPL_TYPE_enum" : "-1",
                call->get(2), call->get(3));
            break;
          }
        }
        if (call->isPrimitive(PRIM_CAST)) {
          if (call->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) ||
              call->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
            gen(outfile, "CHPL_WIDE_CAST(%A, %A, %A)",
                get(1), call->get(1)->typeInfo(), call->get(2));
            break;
          }
        }
        if (call->isPrimitive(PRIM_DYNAMIC_CAST)) {
          if (call->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            gen(outfile, "CHPL_COMM_WIDE_DYNAMIC_CAST(%A, %A, (",
                get(1), call->typeInfo()->getField("addr")->type);
            codegenWideDynamicCastCheck(outfile, call->typeInfo()->getField("addr")->type);
            gen(outfile, "), %A, %A, %A)",
                call->get(2), call->get(3), call->get(4));
            break;
          }
        }
        if (call->isPrimitive(PRIM_GET_PRIV_CLASS)) {
          if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            gen(outfile, "CHPL_WIDE_GET_PRIVATIZED_CLASS(%A, %A)", get(1), call->get(2));
          } else {
            gen(outfile, "%A = chpl_getPrivatizedClass(%A)", get(1), call->get(2));
          }
          break;
        }
      }
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
          !get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        if (get(2)->typeInfo() != dtString)
          gen(outfile, "CHPL_WIDEN(%A, %A)", get(1), get(2));
        else
          gen(outfile, "CHPL_WIDEN_STRING(%A, %A)", get(1), get(2));
        break;
      }
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE) &&
          get(2)->typeInfo()->symbol->hasFlag(FLAG_REF)) {
        gen(outfile, "CHPL_WIDEN(%A, %A)", get(1), get(2));
        break;
      }
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE) &&
          !get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE) &&
          !get(2)->typeInfo()->symbol->hasFlag(FLAG_REF)) {
        Type* valueType = get(2)->typeInfo();
        if (valueType->symbol->hasFlag(FLAG_STAR_TUPLE))
          fprintf(outfile, "CHPL_COMM_WIDE_PUT_SVEC");
        else
          fprintf(outfile, "CHPL_COMM_WIDE_PUT");
        gen(outfile, "(%A, ", valueType);
        genTypeStructureIndex(outfile, valueType->symbol);
        gen(outfile, ", 1 /*length*/, %A, %A, %A, %A)",
            get(1), get(2), get(3), get(4));
        break;
      }
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_REF) &&
          get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
        gen(outfile, "CHPL_NARROW(%A, %A)", get(1), get(2));
        break;
      }
      if (!get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
          !get(1)->typeInfo()->symbol->hasFlag(FLAG_REF) &&
          get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        gen(outfile, "CHPL_NARROW(%A, %A)", get(1), get(2));
        break;
      }
      if ((get(2)->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE)) || (get(2)->typeInfo()->symbol->hasFlag(FLAG_FIXED_STRING))) {
        if (!fNoTupleCopyOpt &&
            (toClassType(get(2)->typeInfo())->fields.length <=
             tuple_copy_limit) &&
            !isTupleOfTuple(get(2))) {
          int i = 0;
          for_fields(field, toClassType(get(2)->typeInfo())) {
            if (i != 0) {
              fprintf(outfile, ";\n");
            }
            if (get(1)->typeInfo()->symbol->hasFlag(FLAG_REF))
              fprintf(outfile, "(*");
            gen(outfile, "(%A)", get(1));
            if (get(1)->typeInfo()->symbol->hasFlag(FLAG_REF))
              fprintf(outfile, ")");
            fprintf(outfile, "[%d] = ", i);
            gen(outfile, "(%A)", get(2));
            fprintf(outfile, "[%d]", i);
            i++;
          }
        } else {
          if (get(1)->typeInfo()->symbol->hasFlag(FLAG_REF))
            gen(outfile, "CHPL_ASSIGN_SVEC(*%A, %A)", get(1), get(2));
          else
            gen(outfile, "CHPL_ASSIGN_SVEC(%A, %A)", get(1), get(2));
        }
        break;
      }
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_REF) &&
          !get(2)->typeInfo()->symbol->hasFlag(FLAG_REF))
        gen(outfile, "(*%A) = %A", get(1), get(2));
      else
        gen(outfile, "%A = %A", get(1), get(2));
      break;
    case PRIM_GET_REF:
    case PRIM_GET_SVEC_MEMBER_VALUE:
    case PRIM_GET_MEMBER_VALUE:
    case PRIM_GET_LOCALEID:
    case PRIM_GET_PRIV_CLASS:
    case PRIM_ARRAY_GET:
    case PRIM_ARRAY_GET_VALUE:
    case PRIM_GPU_GET_VALUE:
    case PRIM_GPU_GET_VAL:
    case PRIM_GPU_GET_ARRAY:
      // generated during generation of PRIM_MOVE
      break;
    case PRIM_SET_REF:
      gen(outfile, "&(%A)", get(1));
      break;
    case PRIM_REF2STR:
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE) ||
          get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        gen(outfile, "chpl_wideRefToString(&(%A))", get(1));
      } else {
        gen(outfile, "chpl_refToString(%A)", get(1));
      }
      break;
    case PRIM_RETURN:
      if (typeInfo() == dtVoid)
        gen(outfile, "return");
      else
        gen(outfile, "return %A", get(1));
      break;
    case PRIM_UNARY_MINUS:
      gen(outfile, "(- %A)", get(1));
      break;
    case PRIM_UNARY_PLUS:
      gen(outfile, "(+ %A)", get(1));
      break;
    case PRIM_UNARY_NOT:
      gen(outfile, "(~ %A)", get(1));
      break;
    case PRIM_UNARY_LNOT:
      gen(outfile, "(! %A)", get(1));
      break;
    case PRIM_ADD:
      gen(outfile, "(%A + %A)", get(1), get(2));
      break;
    case PRIM_SUBTRACT:
      gen(outfile, "(%A - %A)", get(1), get(2));
      break;
    case PRIM_MULT:
      gen(outfile, "(%A * %A)", get(1), get(2));
      break;
    case PRIM_DIV:
      gen(outfile, "(%A / %A)", get(1), get(2));
      break;
    case PRIM_MOD:
      gen(outfile, "(%A %% %A)", get(1), get(2));
      break;
    case PRIM_LSH:
      gen(outfile, "(%A << %A)", get(1), get(2));
      break;
    case PRIM_RSH:
      gen(outfile, "(%A >> %A)", get(1), get(2));
      break;
    case PRIM_PTR_EQUAL:
    case PRIM_EQUAL:
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
          get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        gen(outfile, "CHPL_WIDE_TEST_EQ(%A, %A)", get(1), get(2));
      } else if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
          get(2)->typeInfo() == dtNil) {
        gen(outfile, "%A.addr == nil", get(1));
      } else if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
                 get(1)->typeInfo() == dtNil) {
        gen(outfile, "nil == %A.addr", get(2));
      } else {
        gen(outfile, "(%A == %A)", get(1), get(2));
      }
      break;
    case PRIM_PTR_NOTEQUAL:
    case PRIM_NOTEQUAL:
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
          get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        gen(outfile, "CHPL_WIDE_TEST_NEQ(%A, %A)", get(1), get(2));
      } else if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
          get(2)->typeInfo() == dtNil) {
        gen(outfile, "%A.addr != nil", get(1));
      } else if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
                 get(1)->typeInfo() == dtNil) {
        gen(outfile, "nil != %A.addr", get(2));
      } else {
        gen(outfile, "(%A != %A)", get(1), get(2));
      }
      break;
    case PRIM_LESSOREQUAL:
      gen(outfile, "(%A <= %A)", get(1), get(2));
      break;
    case PRIM_GREATEROREQUAL:
      gen(outfile, "(%A >= %A)", get(1), get(2));
      break;
    case PRIM_LESS:
      gen(outfile, "(%A < %A)", get(1), get(2));
      break;
    case PRIM_GREATER:
      gen(outfile, "(%A > %A)", get(1), get(2));
      break;
    case PRIM_AND:
      gen(outfile, "(%A & %A)", get(1), get(2));
      break;
    case PRIM_OR:
      gen(outfile, "(%A | %A)", get(1), get(2));
      break;
    case PRIM_XOR:
      gen(outfile, "(%A ^ %A)", get(1), get(2));
      break;
    case PRIM_POW:
      gen(outfile, "pow(%A, %A)", get(1), get(2));
      break;
    case PRIM_MIN: 
      {
        Type *t = get(1)->typeInfo();
        if (is_arithmetic_type( t)) {
          if (is_int_type( t)) {
            fprintf( outfile, "MIN_INT%d", get_width( t));
          } else if (is_uint_type( t)) {
            fprintf( outfile, "MIN_UINT%d", get_width( t));
          } else if (is_real_type( t)) {
            fprintf( outfile, "MIN_FLOAT%d", get_width( t));
          } else if (is_imag_type( t)) {
            fprintf( outfile, "MIN_IMAG%d", get_width( t));
          } else if (is_complex_type( t)) {
            fprintf( outfile, "_chpl_complex%d( MIN_FLOAT%d, MIN_FLOAT%d)", 
                     get_width( t), get_width( t)/2, get_width( t)/2);
          } else {
            INT_FATAL( t, "cannot do min on complex types");
          }
        } else {
          INT_FATAL( t, "not arithmetic type");
        }
        break;
      }
    case PRIM_MAX: 
      {
        Type *t = get(1)->typeInfo();
        if (is_arithmetic_type( t)) {
          if (is_int_type( t)) {
            fprintf( outfile, "MAX_INT%d", get_width( t));
          } else if (is_uint_type( t)) {
            fprintf( outfile, "MAX_UINT%d", get_width( t));
          } else if (is_real_type( t)) {
            fprintf( outfile, "MAX_FLOAT%d", get_width( t));
          } else if (is_imag_type( t)) {
            fprintf( outfile, "MAX_IMAG%d", get_width( t));
          } else if (is_complex_type( t)) {
            fprintf( outfile, "_chpl_complex%d( MAX_FLOAT%d, MAX_FLOAT%d)", 
                     get_width( t), get_width( t)/2, get_width( t)/2);
          } else {
            INT_FATAL( t, "cannot do max on complex types");
          }
        } else {
          INT_FATAL( t, "not arithmetic type");
        }
        break;
      }
    case PRIM_PROD_ID: 
      {
        Type *t = get(1)->typeInfo();
        if (is_arithmetic_type( t)) {
          if (is_int_type( t) || is_uint_type( t)) {
            fprintf( outfile, "INT%d(1)", get_width( t));
          } else if (is_real_type( t)) {
            fprintf( outfile, "1.0");
          } else {
            fprintf( outfile, "_chpl_complex%d( 1.0, 0.0)", get_width( t)); 
          }
        } else {
          INT_FATAL( t, "not arithmetic type");
        }
        break;
      }
    case PRIM_LAND_ID: 
      {
        Type *t = get(1)->typeInfo();
        if (is_arithmetic_type( t) || (is_bool_type(t))) {
          if (is_bool_type(t)) {
            fprintf( outfile, "true");
          } else if (is_int_type( t) || is_uint_type( t)) {
            fprintf( outfile, "1");
          } else if (is_real_type( t)) {
            fprintf( outfile, "1.0");
          } else {
            fprintf( outfile, "_chpl_complex%d(1.0, 1.0)", get_width( t));
          }
        } else {
          INT_FATAL( t, "cannot codegen logic identity for");
        }
        break;
      }
    case PRIM_LOR_ID: 
    case PRIM_LXOR_ID: 
      {
        Type *t = get(1)->typeInfo();
        if (is_arithmetic_type( t) || (is_bool_type(t))) {
          if (is_bool_type(t)) {
            fprintf( outfile, "false");
          } else if (is_int_type( t) || is_uint_type( t)) {
            fprintf( outfile, "0");
          } else if (is_real_type( t)) {
            fprintf( outfile, "0.0");
          } else {
            fprintf( outfile, "_chpl_complex%d(0.0, 0.0)", get_width( t));
          }
        } else {
          INT_FATAL( t, "cannot codegen logic identity for");
        }
        break;
      }
    case PRIM_BAND_ID: 
      {
        Type *t = get(1)->typeInfo();
        if (is_arithmetic_type( t)) {
          if (is_int_type( t) || is_uint_type( t) || is_real_type( t) ||
              is_imag_type(t)) {
            fprintf( outfile, "MAX_UINT%d", get_width( t));
          } else {   // must be (is_complex_type( t))
            // WAW: needs fixing?
            fprintf( outfile, "_chpl_complex%d( MAX_UINT%d, MAX_UINT%d)", 
                     get_width( t), get_width( t)/2, get_width( t)/2);
          }
        } else if (is_bool_type(t)) {
          fprintf(outfile, "true");
        } else {
          INT_FATAL( t, "not arithmetic type");
        }
        break;
      }
    case PRIM_BOR_ID: 
    case PRIM_BXOR_ID: 
      {
        Type *t = get(1)->typeInfo();
        if (is_arithmetic_type( t)) {
          if (is_int_type( t) || is_uint_type( t)) {
            fprintf( outfile, "0");
          } else if (is_real_type( t) || is_imag_type(t)) {
            // sjd: why are we doing this on floats???
            fprintf( outfile, "0.0");
          } else {   // must be (is_complex_type( t))
            // WAW: needs fixing?
            fprintf( outfile, "_chpl_complex%d( 0.0, 0.0)", get_width( t));
          }
        } else if (is_bool_type(t)) {
          fprintf(outfile, "false");
        } else {
          INT_FATAL( t, "not arithmetic type");
        }
        break;
      }
    case PRIM_SETCID:
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        gen(outfile, "CHPL_COMM_WIDE_SET_FIELD_VALUE(chpl__class_id, %s",
            fHeterogeneous ? "CHPL_TYPE_enum" : "-1");
        gen(outfile, ", %A, chpl__cid_%A, %A, chpl__cid, %A, %A)",
            get(1), get(1)->typeInfo()->getField("addr")->type/*->symbol*/,
            dtObject->typeInfo(), get(2), get(3));
      } else {
        fprintf(outfile, "((");
        dtObject->typeInfo()->codegen(outfile);
        fprintf(outfile, ")");
        get(1)->codegen(outfile);
        fprintf(outfile, ")");
        fprintf(outfile, "->chpl__cid = %s%s", "chpl__cid_", get(1)->typeInfo()->symbol->cname);
      }
      break;
    case PRIM_GETCID:
      INT_ASSERT(get(1)->typeInfo() != dtNil);
      fprintf(outfile, "(((");
      dtObject->typeInfo()->codegen(outfile);
      fprintf(outfile, ")");
      get(1)->codegen(outfile);
      fprintf(outfile, ")->chpl__cid)");
      break;
    case PRIM_TESTCID:
      INT_ASSERT(get(1)->typeInfo() != dtNil);
      fprintf(outfile, "(((");
      dtObject->typeInfo()->codegen(outfile);
      fprintf(outfile, ")");
      get(1)->codegen(outfile);
      fprintf(outfile, ")");
      fprintf(outfile, "->chpl__cid == %s%s", "chpl__cid_", get(2)->typeInfo()->symbol->cname);
      fprintf(outfile, ")");
      break;
    case PRIM_UNION_SETID:
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
        gen(outfile, "CHPL_COMM_WIDE_SET_FIELD_VALUE(int64_t, ");
        genTypeStructureIndex(outfile, get(1)->typeInfo()->symbol);
        gen(outfile, ", %A, %A", get(1), get(2));
        fprintf(outfile, ", %s*, _uid, ", get(1)->getValType()->symbol->cname);
        get(3)->codegen(outfile);
        fprintf(outfile, ", ");
        get(4)->codegen(outfile);
        fprintf(outfile, ")");
      } else {
        get(1)->codegen(outfile);
        if (get(1)->typeInfo()->symbol->hasFlag(FLAG_REF))
          fprintf(outfile, "->");
        else
          fprintf(outfile, ".");
        fprintf(outfile, "_uid = ");
        get(2)->codegen(outfile);
      }
      break;
    case PRIM_UNION_GETID:
      fprintf(outfile, "(");
      get(1)->codegen(outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_REF))
        fprintf(outfile, "->");
      else
        fprintf(outfile, ".");
      fprintf(outfile, "_uid)");
      break;
    case PRIM_SET_SVEC_MEMBER: {
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
        Type* valueType = get(1)->getValType();
        Type* fieldType = valueType->getField("x1")->type;
        if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
          gen(outfile, "CHPL_COMM_WIDE_SET_TUPLE_COMPONENT_VALUE_SVEC");
        else
          gen(outfile, "CHPL_COMM_WIDE_SET_TUPLE_COMPONENT_VALUE");
        gen(outfile, "(%A, ", fieldType);
        genTypeStructureIndex(outfile, fieldType->symbol);
        gen(outfile, ", %A, %A, %A, ", get(1), get(3), valueType);
        codegenExprMinusOne(outfile, get(2));
        gen(outfile, ", %A, %A)", get(4), get(5));
      } else {
        if (get(3)->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE)) {
          if (!fNoTupleCopyOpt &&
              (toClassType(get(3)->typeInfo())->fields.length <=
               tuple_copy_limit) &&
              !isTupleOfTuple(get(3))) {
            int i = 0;
            for_fields(field, toClassType(get(3)->typeInfo())) {
              if (i != 0) {
                fprintf(outfile, ";\n");
              }
              fprintf(outfile, "(");
              codegenTupleMember(outfile, get(1), get(2));
              fprintf(outfile, ")[%d] = (", i);
              get(3)->codegen(outfile);
              fprintf(outfile, ")[%d]", i);
              i++;
            }
          } else {
            gen(outfile, "CHPL_ASSIGN_SVEC(");
            codegenTupleMember(outfile, get(1), get(2));
            fprintf(outfile, ", ");
            get(3)->codegen(outfile);
            fprintf(outfile, ")");
          }
        } else {
          codegenTupleMember(outfile, get(1), get(2));
          fprintf(outfile, " = ");
          get(3)->codegen(outfile);
        }
      }
      break;
    }
    case PRIM_GET_MEMBER:
    {
      if (!get(2)->typeInfo()->symbol->hasFlag(FLAG_REF))
        fprintf(outfile, "(&(");
      codegen_member(outfile, get(1), get(2));
      if (!get(2)->typeInfo()->symbol->hasFlag(FLAG_REF))
        fprintf(outfile, "))");
      break;
    }
    case PRIM_GET_SVEC_MEMBER: {
      Type* tupleType = get(1)->getValType();
      if (!tupleType->getField("x1")->type->symbol->hasFlag(FLAG_REF))
        fprintf(outfile, "(&(");
      codegenTupleMember(outfile, get(1), get(2));
      if (!tupleType->getField("x1")->type->symbol->hasFlag(FLAG_REF))
        fprintf(outfile, "))");
      break;
    }
    case PRIM_SET_MEMBER:
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        Type* classType = get(1)->typeInfo()->getField("addr")->type;
        Type* fieldType = get(2)->typeInfo();
        if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
          fprintf(outfile, "CHPL_COMM_WIDE_SET_FIELD_VALUE_SVEC");
        else
          fprintf(outfile, "CHPL_COMM_WIDE_SET_FIELD_VALUE");
        gen(outfile, "(%A, ", fieldType);
        genTypeStructureIndex(outfile, fieldType->symbol);
        gen(outfile, ", %A, %A, %A, %A, %A, %A)",
            get(1), get(3), classType, get(2), get(4), get(5));
      } else if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
        Type* valueType = get(1)->getValType();
        Type* fieldType = get(2)->typeInfo();
        if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
          fprintf(outfile, "CHPL_COMM_WIDE_SET_FIELD_VALUE_SVEC");
        else
          fprintf(outfile, "CHPL_COMM_WIDE_SET_FIELD_VALUE");
        gen(outfile, "(%A, ", fieldType);
        genTypeStructureIndex(outfile, fieldType->symbol);
        gen(outfile, ", %A, %A, %A*, ", get(1), get(3), valueType);
        if (isUnion(valueType))
          gen(outfile, "_u.");
        gen(outfile, "%A, %A, %A)", get(2), get(4), get(5));
      } else if (get(2)->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE)) {
        if (!fNoTupleCopyOpt &&
            (toClassType(get(2)->typeInfo())->fields.length <=
             tuple_copy_limit) &&
            !isTupleOfTuple(get(2))) {
          int i = 0;
          for_fields(field, toClassType(get(2)->typeInfo())) {
            if (i != 0) {
              fprintf(outfile, ";\n");
            }
            fprintf(outfile, "(");
            codegen_member(outfile, get(1), get(2));
            fprintf(outfile, ")[%d] = ", i);
            gen(outfile, "(%A)", get(3));
            fprintf(outfile, "[%d]", i);
            i++;
          }
        } else {
          fprintf(outfile, "CHPL_ASSIGN_SVEC(");
          codegen_member(outfile, get(1), get(2));
          gen(outfile, ", %A)", get(3));
        }
      } else {
        codegen_member(outfile, get(1), get(2));
        gen(outfile, " = %A", get(3));
      }
      break;
    case PRIM_CHECK_NIL:
      fprintf(outfile, "_CHECK_NIL(");
      get(1)->codegen(outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf(outfile, ", ");
      get(2)->codegen(outfile);
      fprintf(outfile, ", ");
      get(3)->codegen(outfile);
      fprintf(outfile, ")");
      break;
    case PRIM_LOCAL_CHECK:
      fprintf(outfile, "CHPL_TEST_LOCAL(");
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_REF))
        fprintf(outfile, "*");
      gen(outfile, "%A, %A, %A, ", get(1), get(2), get(3));
      INT_ASSERT(get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE) ||
                 get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS));
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
          get(1)->typeInfo()->getField("addr")->typeInfo()->symbol->hasFlag(FLAG_EXTERN)) {
        gen(outfile, "\"cannot pass non-local extern class to extern procedure\")");
      } else {
        gen(outfile, "\"cannot access remote data in local block\")");
      }
      break;
    case PRIM_SYNC_INIT:
    case PRIM_SYNC_DESTROY:
      fprintf( outfile, primitive->tag == PRIM_SYNC_INIT ?
               "chpl_sync_initAux(&((" : "chpl_sync_destroyAux(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->sync_aux))");
      break;
    case PRIM_SYNC_LOCK:
      fprintf( outfile, "chpl_sync_lock(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->sync_aux))");
      break;
    case PRIM_SYNC_UNLOCK:
      fprintf(outfile, "chpl_sync_unlock(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf(outfile, ")->sync_aux))");
      break;
    case PRIM_SYNC_WAIT_FULL:
      fprintf( outfile, "chpl_sync_waitFullAndLock(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->sync_aux), ");
      get(2)->codegen(outfile);
      fprintf( outfile, ", ");
      get(3)->codegen( outfile);
      fprintf( outfile, ")");
      break;
    case PRIM_SYNC_WAIT_EMPTY:
      fprintf( outfile, "chpl_sync_waitEmptyAndLock(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->sync_aux), ");
      get(2)->codegen(outfile);
      fprintf( outfile, ", ");
      get(3)->codegen( outfile);
      fprintf( outfile, ")");
      break;
    case PRIM_SYNC_SIGNAL_FULL:
      fprintf(outfile, "chpl_sync_markAndSignalFull(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf(outfile, ")->sync_aux))");
      break;
    case PRIM_SYNC_SIGNAL_EMPTY:
      fprintf( outfile, "chpl_sync_markAndSignalEmpty(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->sync_aux))");
      break;
    case PRIM_SINGLE_INIT:
    case PRIM_SINGLE_DESTROY:
      fprintf(outfile, primitive->tag == PRIM_SINGLE_INIT ?
              "chpl_single_initAux(&((" : "chpl_single_destroyAux(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->single_aux))");
      break;
    case PRIM_SINGLE_LOCK:
      fprintf( outfile, "chpl_single_lock(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->single_aux))");
      break;
    case PRIM_SINGLE_UNLOCK:
      fprintf( outfile, "chpl_single_unlock(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->single_aux))");
      break;
    case PRIM_SINGLE_WAIT_FULL:
      fprintf( outfile, "chpl_single_waitFullAndLock(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->single_aux), ");
      get(2)->codegen(outfile);
      fprintf( outfile, ", ");
      get(3)->codegen( outfile);
      fprintf( outfile, ")");
      break;
    case PRIM_SINGLE_SIGNAL_FULL:
      fprintf(outfile, "chpl_single_markAndSignalFull(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf(outfile, ")->single_aux))");
      break;
    case PRIM_WRITEEF:
      fprintf( outfile, "chpl_write_EF((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, "), ");
      get(2)->codegen( outfile);
      fprintf( outfile, ")");
      break;
    case PRIM_WRITEFF:
      fprintf( outfile, "chpl_write_FF((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, "), ");
      get(2)->codegen( outfile);
      fprintf( outfile, ")");
      break;
    case PRIM_WRITEXF:
      fprintf( outfile, "chpl_write_XF((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, "), ");
      get(2)->codegen( outfile);
      fprintf( outfile, ")");
      break;
    case PRIM_SYNC_RESET:
      fprintf( outfile, "chpl_sync_reset((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, "))");
      break;
    case PRIM_READFE:
      fprintf( outfile, "chpl_read_FE((");
      get(2)->codegen( outfile);
      if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, "))");
      break;
    case PRIM_READFF:
      fprintf( outfile, "chpl_read_FF((");
      get(2)->codegen( outfile);
      if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, "))");
      break;
    case PRIM_READXX:
      fprintf( outfile, "chpl_read_XX((");
      get(2)->codegen( outfile);
      if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, "))");
      break;
    case PRIM_SYNC_ISFULL:
      fprintf( outfile, "chpl_sync_isFull(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->value), &((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->sync_aux), ");
      get(2)->codegen( outfile);
      fprintf( outfile, ")");
      break;
    case PRIM_SINGLE_WRITEEF:
      fprintf( outfile, "chpl_single_write_EF((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, "), ");
      get(2)->codegen( outfile);
      fprintf( outfile, ")");
      break;
    case PRIM_SINGLE_RESET:
      fprintf( outfile, "chpl_single_reset((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, "))");
      break;
    case PRIM_SINGLE_READFF:
      fprintf( outfile, "chpl_single_read_FF((");
      get(2)->codegen( outfile);
      if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, "))");
      break;
    case PRIM_SINGLE_READXX:
      fprintf( outfile, "chpl_single_read_XX((");
      get(2)->codegen( outfile);
      if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, "))");
      break;
    case PRIM_SINGLE_ISFULL:
      fprintf( outfile, "chpl_single_isFull(&((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->value), &((");
      get(1)->codegen( outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf( outfile, ")->single_aux), ");
      get(2)->codegen( outfile);
      fprintf( outfile, ")");
      break;
    case PRIM_PROCESS_TASK_LIST:
      fputs( "chpl_task_processTaskList(", outfile);
      get(1)->codegen( outfile);
      {
        ClassType *endCountType = toClassType(toSymExpr(get(1))->typeInfo());
        if (endCountType->symbol->hasFlag(FLAG_WIDE_CLASS)) {
          fputc('.', outfile);
          endCountType->getField("addr")->codegen(outfile);
          endCountType = toClassType(endCountType->getField("addr")->typeInfo());
        }
        fputs("->", outfile);
        endCountType->getField("taskList")->codegen(outfile);
      }
      fputc( ')', outfile);
      break;
    case PRIM_EXECUTE_TASKS_IN_LIST:
      fputs( "chpl_task_executeTasksInList(", outfile);
      get(1)->codegen( outfile);
      fputc( ')', outfile);
      break;
    case PRIM_FREE_TASK_LIST:
      if (fNoMemoryFrees)
        break;
      fputs( "chpl_task_freeTaskList(", outfile);
      get(1)->codegen( outfile);
      fputc( ')', outfile);
      break;
    case PRIM_TASK_ID:
      fprintf(outfile, "chpl_task_getId()");
      break;
    case PRIM_TASK_SLEEP:
      fputs( "chpl_task_sleep(", outfile);
      get(1)->codegen( outfile);
      fputc( ')', outfile);
      break;
    case PRIM_GET_SERIAL:
      fprintf(outfile, "chpl_task_getSerial()");
      break;
    case PRIM_SET_SERIAL:
      gen(outfile, "chpl_task_setSerial(%A)", get(1));
      break;
    case PRIM_CHPL_ALLOC:
    case PRIM_CHPL_ALLOC_PERMIT_ZERO: {
      bool is_struct = false;

      // if Chapel class or record
      if (TypeSymbol *t = toTypeSymbol(typeInfo()->symbol)) {
        if (toClassType(t->type)) {
          is_struct = true;
        }
      }

      // pointer cast
      fprintf( outfile, "(");
      typeInfo()->symbol->codegen( outfile);
      if (!is_struct) {
        fprintf( outfile, "*");
      } 
      fprintf( outfile, ")");

      // target: void* chpl_alloc(size_t size, char* description);
      fprintf(outfile, "%s(sizeof(",
              (primitive->tag == PRIM_CHPL_ALLOC ?
               "chpl_alloc" : 
               "CHPL_ALLOC_PERMIT_ZERO"));
      if (is_struct) fprintf( outfile, "_");          // need struct of class
      typeInfo()->symbol->codegen( outfile);
      fprintf( outfile, "), ");
      if (fRuntime) {
        fprintf( outfile, "CHPL_RT_MD_CHAPEL_CODE");
      } else {
        get(2)->codegen( outfile);
        fprintf( outfile, " + CHPL_RT_MD_NUM");
      }
      fprintf( outfile, ", ");
      get(3)->codegen( outfile);
      fprintf( outfile, ", ");
      get(4)->codegen( outfile);
      fprintf( outfile, ")");
      break;
    }
    case PRIM_CHPL_FREE: {
      if (fNoMemoryFrees)
        break;
      INT_ASSERT(numActuals() == 3);
      fprintf(outfile, "%s((void*)", this->primitive->name);
      bool first_actual = true;
      for_actuals(actual, this) {
        if (!first_actual)
          fputs(", ", outfile);
        if (actual->typeInfo()->symbol->hasFlag(FLAG_REF) ||
            actual->typeInfo()->symbol->hasFlag(FLAG_WIDE))
          fputc('*', outfile);
        actual->codegen(outfile);
        if (actual->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) ||
            actual->typeInfo()->symbol->hasFlag(FLAG_WIDE))
          fputs(".addr", outfile);
        if (first_actual) {
          first_actual = false;
        }
      }
      fputs("); ", outfile);
      get(1)->codegen(outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) ||
          get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE))
        fputs(".addr", outfile);
      fputs(" = NULL", outfile);
      break;
    }
    case PRIM_CAST: {
      if (typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        INT_FATAL(this, "wide class cast is not normal");
      Type* dst = get(1)->typeInfo();
      Type* src = get(2)->typeInfo();
      if (dst == src) {
        get(2)->codegen(outfile);
      } else if ((is_int_type(dst) || is_uint_type(dst)) && src == dtTaskID) {
          fprintf(outfile, "((");
          typeInfo()->codegen(outfile);
          fprintf(outfile, ") (intptr_t) (");
          get(2)->codegen(outfile);
          fprintf(outfile, "))");
      } else if (dst == dtString || src == dtString) {
        fprintf(outfile, *dst->symbol->cname == '_' ? "%s_to%s(" : "%s_to_%s(",
                src->symbol->cname, dst->symbol->cname);
        get(2)->codegen(outfile);
        if (src == dtString) {
          fprintf(outfile, ", ");
          get(3)->codegen(outfile);
          fprintf(outfile, ", ");
          get(4)->codegen(outfile);
        }
        fprintf(outfile, ")");
      } else {
        if (isRecord(typeInfo()) || isUnion(typeInfo())) {
          fprintf(outfile, "(*((");
          typeInfo()->codegen(outfile);
          fprintf(outfile, "*)(&(");
          get(2)->codegen(outfile);
          fprintf(outfile, "))))");
        } else {
          fprintf(outfile, "((");
          typeInfo()->codegen(outfile);
          fprintf(outfile, ")(");
          get(2)->codegen(outfile);
          fprintf(outfile, "))");
        }
      }
      break;
    }
    case PRIM_DYNAMIC_CAST:
      if (typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        INT_FATAL(this, "wide class dynamic cast is not normal");
      fprintf(outfile, "((");
      codegenDynamicCastCheck(outfile, typeInfo(), get(2));
      fprintf(outfile, ") ? ((");
      typeInfo()->codegen(outfile);
      fprintf(outfile, ")(");
      get(2)->codegen(outfile);
      fprintf(outfile, ")) : NULL)");
      break;
    case PRIM_GC_CC_INIT:
      fprintf(outfile, "_chpl_gc_init(");
      get(1)->codegen(outfile);
      fprintf(outfile, ")");
      break;
    case PRIM_GC_ADD_ROOT:
      fprintf(outfile, "_addRoot(&");
      get(1)->codegen(outfile);
      fprintf(outfile, ")");
      break;
    case PRIM_GC_ADD_NULL_ROOT:
      fprintf(outfile, "_addNullRoot(&");
      get(1)->codegen(outfile);
      fprintf(outfile, ")");
      break;
    case PRIM_GC_DELETE_ROOT:
      fprintf(outfile, "_deleteRoot(");
      get(1)->codegen(outfile);
      fprintf(outfile, ")");
      break;
    case PRIM_GC_CLEANUP:
      fprintf(outfile, "_chpl_gc_cleanup()");
      break;
    case PRIM_LOCALE_ID:
      fprintf(outfile, "chpl_localeID");
      break;
    case PRIM_NUM_LOCALES:
      fprintf(outfile, "chpl_comm_default_num_locales()");
      break;
    case PRIM_ALLOC_GVR:
      fprintf(outfile, "chpl_comm_alloc_registry(%d)", numGlobalsOnHeap);
      break;
    case PRIM_HEAP_REGISTER_GLOBAL_VAR:
      gen(outfile, "CHPL_HEAP_REGISTER_GLOBAL_VAR(%A, %A)", get(1), get(2));
      break;
    case PRIM_HEAP_BROADCAST_GLOBAL_VARS:
      gen(outfile, "CHPL_COMM_BROADCAST_GLOBAL_VARS(%A)", get(1));
      break;
    case PRIM_PRIVATE_BROADCAST:
      gen(outfile, "chpl_comm_broadcast_private(%A, sizeof(%A), ",
          get(1), get(2)->typeInfo());
      genTypeStructureIndex(outfile, get(2)->typeInfo()->symbol);
      fprintf(outfile, ")");
      break;
    case PRIM_INT_ERROR:
      fprintf(outfile, "chpl_internal_error(\"compiler generated error\")");
      break;
    case PRIM_STRING_COPY:
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        fputs("chpl_wide_string_copy(", outfile);
        bool first_actual;
        first_actual = true;
        for_actuals(actual, this) {
          if (first_actual) {
            fputc('&', outfile);
            actual->codegen(outfile);
            first_actual = false;
          } else {
            fputs(", ", outfile);
            if (actual->typeInfo()->symbol->hasFlag(FLAG_REF) ||
                actual->typeInfo()->symbol->hasFlag(FLAG_WIDE))
              fputc('*', outfile);
            actual->codegen(outfile);
            if (actual->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) ||
                actual->typeInfo()->symbol->hasFlag(FLAG_WIDE))
              fputs(".addr", outfile);
          }
        }
        fputc(')', outfile);
      } else
        codegenBasicPrimitive(outfile, this);
      break;
    case PRIM_chpl_numThreads:
      fprintf(outfile, "chpl_task_getNumThreads()");
      break;
    case PRIM_chpl_numIdleThreads:
      fprintf(outfile, "chpl_task_getNumIdleThreads()");
      break;
    case PRIM_chpl_numQueuedTasks:
      fprintf(outfile, "chpl_task_getNumQueuedTasks()");
      break;
    case PRIM_chpl_numRunningTasks:
      fprintf(outfile, "chpl_task_getNumRunningTasks()");
      break;
    case PRIM_chpl_numBlockedTasks:
      fprintf(outfile, "chpl_task_getNumBlockedTasks()");
      break;
    case PRIM_RT_ERROR:
    case PRIM_RT_WARNING:
      codegenBasicPrimitive(outfile, this);
      break;
    case PRIM_NEW_PRIV_CLASS:
      fprintf(outfile, "chpl_newPrivatizedClass(");
      get(1)->codegen(outfile);
      if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        fprintf(outfile, ".addr");
      fprintf(outfile, ")");
      break;
    case PRIM_NUM_PRIV_CLASSES:
      fprintf(outfile, "chpl_numPrivatizedClasses()");
      break;
    case PRIM_WARNING:
      // warning issued, continue codegen
      break;
    case PRIM_TX_BEGIN:
      gen(outfile, "CHPL_STM_TX_BEGIN(%A, %A)", get(1), get(2));
      break;
    case PRIM_TX_COMMIT:
      gen(outfile, "CHPL_STM_TX_COMMIT(%A)", get(1));
      break;
    case PRIM_TX_ARRAY_SET: {
      if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        Type* classType = get(2)->typeInfo()->getField("addr")->type;
        Type* elementType = getDataClassType(classType->symbol)->type;
        Type* wideElementType = wideRefMap.get(elementType->refType);
        if (elementType->symbol->hasFlag(FLAG_STAR_TUPLE))
          gen(outfile, "CHPL_STM_COMM_WIDE_ARRAY_SET_VALUE_SVEC");
        else
	  gen(outfile, "CHPL_STM_COMM_WIDE_ARRAY_SET_VALUE");
        gen(outfile, "(%A, %A, %A, %A, %A, _data, %A, %A, %A, %A)",
            get(1), wideElementType, get(2), get(3), classType,
            elementType, get(4), get(5), get(6));
      } else {
        Type* type = get(4)->typeInfo();
        registerTypeToStructurallyCodegen(type->symbol);
        gen(outfile, "CHPL_STM_ARRAY_STORE_VALUE");
        gen(outfile, "(%A, %A, %A, %A, %A, %A, %A)",
            get(1), get(2), get(3), get(4), type, get(5), get(6));
      }
      break;
    }
    case PRIM_TX_ARRAY_ALLOC: {
      if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        gen(outfile, "CHPL_STM_WIDE_ARRAY_ALLOC(%A, %A, %A, %A, %A, %A)", 
            get(1), get(2), getDataClassType(get(2)->typeInfo()->getField("addr")->type->symbol), get(4), get(5), get(6));
      } else {
        gen(outfile, "CHPL_STM_ARRAY_ALLOC(%A, %A, %A, %A, %A, %A)", 
            get(1), get(2), getDataClassType(get(2)->typeInfo()->symbol), get(4), get(5), get(6));
      }
      break;
    }
    case PRIM_TX_ARRAY_FREE:
      if (fNoMemoryFrees)
        break;
      if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        gen(outfile, "CHPL_STM_WIDE_ARRAY_FREE(%A, %A, %A, %A)",
            get(1), get(2), get(3), get(4));
      else
        gen(outfile, "CHPL_STM_ARRAY_FREE(%A, %A, %A, %A)",
            get(1), get(2), get(3), get(4));
      break;      
    case PRIM_TX_GET_LOCALEID:
      gen(outfile, "CHPL_STM_COMM_WIDE_GET_LOCALE");
      gen(outfile, "(%A, %A, %A, %A, %A, %A)", get(1), get(2), get(3), 
	  dtLocale->getField("chpl_id")->typeInfo(), get(4), get(5));      
      break;
    case PRIM_TX_LOAD_LOCALEID:
      gen(outfile, "CHPL_STM_LOAD_LOCALE");
      gen(outfile, "(%A, %A, %A, %A, %A, %A)", get(1), get(2), get(3),
          dtLocale->getField("chpl_id")->typeInfo(), get(4), get(5));      
      break;
    case PRIM_TX_GET_REF: {
      Type* type = get(2)->typeInfo();
      registerTypeToStructurallyCodegen(type->symbol);
      gen(outfile, "CHPL_STM_COMM_WIDE_GET(%A, %A, %A, %A, %A, %A)",
          get(1), get(2), get(3), type, get(4), get(5));
      break;
    }
    case PRIM_TX_LOAD_REF: {
      Type* type = get(2)->typeInfo();
      registerTypeToStructurallyCodegen(type->symbol);
      gen(outfile, "CHPL_STM_LOAD_REF(%A, %A, %A, %A, %A, %A)",
          get(1), get(2), get(3), type, get(4), get(5));
      break;    
    }
    case PRIM_TX_LOAD: {
      Type* type = get(2)->typeInfo();
      registerTypeToStructurallyCodegen(type->symbol);
      gen(outfile, "CHPL_STM_LOAD(%A, %A, %A, %A, %A, %A)",
          get(1), get(2), get(3), type, get(4), get(5));
      break;    
    }
    case PRIM_TX_GET_MEMBER_VALUE: {
      Type* fieldType = get(2)->typeInfo();
      registerTypeToStructurallyCodegen(fieldType->symbol);
      if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
        fprintf(outfile, "CHPL_STM_COMM_WIDE_GET_FIELD_VALUE_SVEC");
      else
        fprintf(outfile, "CHPL_STM_COMM_WIDE_GET_FIELD_VALUE");
      if (get(3)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        Type* classType = get(3)->typeInfo()->getField("addr")->type;
        registerTypeToStructurallyCodegen(classType->symbol);
        gen(outfile, "(%A, %A, %A, %A, ", 
            get(1), get(2), get(3), classType);
      } else if (get(3)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
        Type* valueType = get(3)->getValType();
        registerTypeToStructurallyCodegen(valueType->symbol);
        gen(outfile, "(%A, %A, %A, %A*, ",
            get(1), get(2), get(3), valueType);
        if (isUnion(valueType))
          fprintf(outfile, "_u.");
      }
      gen(outfile, "%A, %A, %A, %A)", get(4), fieldType, get(5), get(6));
      break;
    }
    case PRIM_TX_LOAD_MEMBER_VALUE: {
      Type* fieldType = get(2)->typeInfo();      
      registerTypeToStructurallyCodegen(fieldType->symbol);
      fprintf(outfile, "CHPL_STM_LOAD_FIELD_VALUE");
      gen(outfile, "(%A, %A, (", get(1), get(2));
      codegen_member(outfile, get(3), get(4));
      gen(outfile, "), %A, %A, %A)", fieldType, get(5), get(6));      
      break;
    }
    case PRIM_TX_LOAD_MEMBER_VALUE_SVEC: {
      Type* fieldType = get(2)->typeInfo();      
      registerTypeToStructurallyCodegen(fieldType->symbol);
      fprintf(outfile, "CHPL_STM_LOAD_FIELD_VALUE_SVEC");
      gen(outfile, "(%A, %A, (", get(1), get(2));
      codegen_member(outfile, get(3), get(4));
      gen(outfile, "), %A, %A, %A)", fieldType, get(5), get(6));      
      break;
    }
    case PRIM_TX_GET_SVEC_MEMBER_VALUE: {
      Type* valueType = get(3)->getValType();
      Type* fieldType = valueType->getField("x1")->type;
      registerTypeToStructurallyCodegen(fieldType->symbol);
      if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
        fprintf(outfile, "CHPL_STM_COMM_WIDE_GET_TUPLE_COMPONENT_VALUE_SVEC");
      else
        fprintf(outfile, "CHPL_STM_COMM_WIDE_GET_TUPLE_COMPONENT_VALUE");
      gen(outfile, "(%A, %A, %A, ", get(1), get(2), get(3));
      codegenExprMinusOne(outfile, get(4));
      gen(outfile, ", %A, %A, %A)",
          fieldType, get(5), get(6));
      break;
    }
    case PRIM_TX_LOAD_SVEC_MEMBER_VALUE: {
      Type* type = get(2)->typeInfo();
      registerTypeToStructurallyCodegen(type->symbol);
      fprintf(outfile, "CHPL_STM_LOAD_TUPLE_COMPONENT_VALUE");
      gen(outfile, "(%A, %A, ", get(1), get(2));
      codegenTupleMember(outfile, get(3), get(4));
      gen(outfile, ", %A, %A, %A)", type, get(5), get(6));
      break;    
    }
    case PRIM_TX_LOAD_SVEC_MEMBER_VALUE_SVEC: {
      Type* type = get(2)->typeInfo();
      registerTypeToStructurallyCodegen(type->symbol);
      fprintf(outfile, "CHPL_STM_LOAD_TUPLE_COMPONENT_VALUE_SVEC");
      gen(outfile, "(%A, %A, ", get(1), get(2));
      codegenTupleMember(outfile, get(3), get(4));
      gen(outfile, ", %A, %A, %A)", type, get(5), get(6));
      break;    
    }
    case PRIM_TX_ARRAY_GET: {
      Type* type = get(2)->typeInfo()->getField("addr")->type;
      registerTypeToStructurallyCodegen(type->symbol);
      Type* stype = get(3)->typeInfo()->getField("addr")->type;
      registerTypeToStructurallyCodegen(stype->symbol);
      gen(outfile, "CHPL_STM_COMM_WIDE_ARRAY_GET");
      gen(outfile, "(%A, %A, %A, %A, %A, _data, %A, %A, %A)",
          get(1), get(2), get(3), get(4), stype, type, get(5), get(6));
      break;
    }
    case PRIM_TX_ARRAY_LOAD: {
      Type* type = get(2)->typeInfo();
      registerTypeToStructurallyCodegen(type->symbol);
      gen(outfile, "CHPL_STM_ARRAY_LOAD(%A, %A, %A, %A, %A, %A, %A)",
          get(1), get(2), get(3), get(4), type, get(5), get(6));
      break;
    }
    case PRIM_TX_ARRAY_GET_VALUE: {    
      Type* tt = wideRefMap.get(getDataClassType(get(3)->typeInfo()->getField("addr")->type->symbol)->type->refType);
      TypeSymbol* ts0 = get(3)->typeInfo()->getField("addr")->type->symbol;
      TypeSymbol* ts = getDataClassType(get(3)->typeInfo()->getField("addr")->type->symbol);
      TypeSymbol* ts2 = tt->getField("addr")->type->symbol;
      registerTypeToStructurallyCodegen(ts0);
      registerTypeToStructurallyCodegen(ts);
      registerTypeToStructurallyCodegen(ts2);
      gen(outfile, "CHPL_STM_COMM_WIDE_ARRAY_GET_VALUE");
      gen(outfile, "(%A, %A, %A, %A, %A, %A, _data, %A, %A, %A, %A)", 
          get(1), get(2), tt, get(3), get(4), ts0, ts2, ts, get(5), get(6)); 
      break;
    }
     case PRIM_TX_ARRAY_LOAD_VALUE: {
       Type* type = get(2)->typeInfo();
       registerTypeToStructurallyCodegen(type->symbol);
       gen(outfile, "CHPL_STM_ARRAY_LOAD_VALUE(%A, %A, %A, %A, %A, %A, %A)",
           get(1), get(2), get(3), get(4), type, get(5), get(6));    
       break; 
     }
    case PRIM_TX_GET_TEST_CID:
      gen(outfile, "CHPL_STM_COMM_WIDE_CLASS_TEST_CID");
      gen(outfile, "(%A, %A, %A, chpl__cid_%A, object, chpl__cid, %A, %A)", 
          get(1), get(2), get(3), get(4)->typeInfo(), get(5), get(6));    
      break;
    case PRIM_TX_LOAD_TEST_CID:
      INT_ASSERT(get(3)->typeInfo() != dtNil);
      gen(outfile, "CHPL_STM_CLASS_TEST_CID(%A, %A, ", get(1), get(2));
      fprintf(outfile, "((object)");
      get(3)->codegen(outfile);
      fprintf(outfile, ")->chpl__cid, ");
      fprintf(outfile, "%s%s, ", 
              "chpl__cid_", get(4)->typeInfo()->symbol->cname);
      gen(outfile, "%A, %A)", get(5), get(6));
      break;
    case PRIM_TX_PUT: {
      Type* type = get(3)->typeInfo();
      registerTypeToStructurallyCodegen(type->symbol);
      if (type->symbol->hasFlag(FLAG_STAR_TUPLE)) 
        gen(outfile, "CHPL_STM_COMM_WIDE_PUT_SVEC");
      else 
        gen(outfile, "CHPL_STM_COMM_WIDE_PUT");
      gen(outfile, "(%A, %A, %A, %A, %A, %A)",
          get(1), get(2), get(3), type, get(4), get(5));
      break;
    }
    case PRIM_TX_STORE_REF: {
      Type* type = get(3)->typeInfo();
      registerTypeToStructurallyCodegen(type->symbol);
      if (get(3)->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE)) 
        gen(outfile, "CHPL_STM_STORE_REF_SVEC");
      else
        gen(outfile, "CHPL_STM_STORE_REF");
      gen(outfile, "(%A, %A, %A, %A, %A, %A)",
          get(1), get(2), get(3), type, get(4), get(5));
      break;
    }
    case PRIM_TX_STORE: {
      Type* type = get(3)->typeInfo();
      registerTypeToStructurallyCodegen(type->symbol);
      if (type->symbol->hasFlag(FLAG_STAR_TUPLE)) 
        gen(outfile, "CHPL_STM_STORE_SVEC"); 
      else 
        gen(outfile, "CHPL_STM_STORE");
      gen(outfile, "(%A, %A, %A, %A, %A, %A)",
          get(1), get(2), get(3), type, get(4), get(5));
      break;
    }
    case PRIM_TX_SET_CID:
      gen(outfile, "CHPL_STM_COMM_WIDE_SET_FIELD_VALUE");
      gen(outfile, "(%A, %A, ", get(1), get(2));
      fprintf(outfile, "chpl__cid_%s, object, chpl__cid, chpl__class_id, ",
              get(2)->typeInfo()->getField("addr")->type->symbol->cname);
      gen(outfile, "%A, %A)", get(3), get(4));
      break;
    case PRIM_TX_STORE_CID:
      gen(outfile, "CHPL_STM_STORE");
      gen(outfile, "(%A, ((object)%A)->chpl__cid, ", get(1), get(2));
      fprintf(outfile, "chpl__cid_%s, chpl__class_id, ",
              get(2)->typeInfo()->symbol->cname);
      gen(outfile, "%A, %A)", get(3), get(4));
      break;
    case PRIM_TX_SET_SVEC_MEMBER: {
      Type* valueType = get(2)->getValType();
      Type* fieldType = valueType->getField("x1")->type;
      registerTypeToStructurallyCodegen(fieldType->symbol);
      if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
        gen(outfile, "CHPL_STM_COMM_WIDE_SET_TUPLE_COMPONENT_VALUE_SVEC");
      else
        gen(outfile, "CHPL_STM_COMM_WIDE_SET_TUPLE_COMPONENT_VALUE");
      gen(outfile, "(%A, %A, %A, %A, %A, ", 
          get(1), fieldType, get(2), get(4), valueType);
      codegenExprMinusOne(outfile, get(3));
      gen(outfile, ", %A, %A)", get(5), get(6));
      break;
    }   
    case PRIM_TX_STORE_SVEC_MEMBER: {
      Type* fieldType = get(4)->typeInfo();
      registerTypeToStructurallyCodegen(fieldType->symbol);
      gen(outfile, "/* store svec member */\n");
      if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE)) 
        gen(outfile, "CHPL_STM_STORE_SVEC(%A, ", get(1));
      else 
        gen(outfile, "CHPL_STM_STORE(%A, ", get(1));
      codegenTupleMember(outfile, get(2), get(3));
      gen(outfile, ", %A, %A, %A, %A)", get(4), fieldType, get(5), get(6));
      break;
    }
    case PRIM_TX_SET_MEMBER: {
      if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        Type* classType = get(2)->typeInfo()->getField("addr")->type;
        Type* fieldType = get(3)->typeInfo();
        registerTypeToStructurallyCodegen(fieldType->symbol);
        if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
          gen(outfile, "CHPL_STM_COMM_WIDE_SET_FIELD_VALUE_SVEC");
        else
          gen(outfile, "CHPL_STM_COMM_WIDE_SET_FIELD_VALUE");
        gen(outfile, "(%A, %A, %A, %A, %A, %A, %A, %A)",
            get(1), fieldType, get(2), get(4), classType, 
            get(3), get(5), get(6));
      } else if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
        Type* valueType = get(2)->getValType();
        Type* fieldType = get(3)->typeInfo();
        registerTypeToStructurallyCodegen(fieldType->symbol);
        INT_ASSERT(!isUnion(valueType));
        if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
          gen(outfile, "CHPL_STM_COMM_WIDE_SET_FIELD_VALUE_SVEC");
        else
          gen(outfile, "CHPL_STM_COMM_WIDE_SET_FIELD_VALUE");
        gen(outfile, "(%A, %A, %A, %A, %A*, %A, %A, %A)",
            get(1), fieldType, get(2), get(4), valueType, 
            get(3), get(5), get(6));
      }
      break;
    }
    case PRIM_TX_STORE_MEMBER: {
      Type* fieldType = get(4)->typeInfo();
      registerTypeToStructurallyCodegen(fieldType->symbol);
      gen(outfile, "/* store member */\n");
      if (fieldType->symbol->hasFlag(FLAG_STAR_TUPLE))
        gen(outfile, "CHPL_STM_STORE_SVEC(%A, ", get(1));
      else
        gen(outfile, "CHPL_STM_STORE(%A, ", get(1));
      codegen_member(outfile, get(2), get(3));
      gen(outfile, ", %A, %A, %A, %A)", get(4), fieldType, get(5), get(6));
      break;
    }
    case PRIM_TX_CHPL_ALLOC: 
    case PRIM_TX_CHPL_ALLOC_PERMIT_ZERO: {
      bool is_struct = false;
      INT_ASSERT(!fRuntime);

      // if Chapel class or record
      if (TypeSymbol *t = toTypeSymbol(typeInfo()->symbol)) {
        if (toClassType(t->type)) {
          is_struct = true;
        }
      }

      // pointer cast
      fprintf( outfile, "(");
      typeInfo()->symbol->codegen(outfile);
      if (!is_struct) {
        fprintf( outfile, "*");
      } 
      fprintf( outfile, ")");
      
      // target: void* chpl_stm_alloc(tx, size, description, ln, fn);
      fprintf(outfile, "%s(",
              (primitive->tag == PRIM_TX_CHPL_ALLOC ?
               "chpl_stm_alloc" : 
               "CHPL_STM_ALLOC_PERMIT_ZERO"));
      gen(outfile, "%A, sizeof(", get(2));    // insert tx descriptor
      if (is_struct) fprintf(outfile, "_");   // need struct of class
      typeInfo()->symbol->codegen(outfile);
      fprintf( outfile, "), ");

      get(3)->codegen( outfile);
      fprintf(outfile, " + CHPL_RT_MD_NUM");
      gen(outfile, ", %A, %A)", get(4), get(5));
      break;
    }
    case PRIM_TX_CHPL_FREE:
      INT_ASSERT(numActuals() == 4);
      fprintf(outfile, "%s", this->primitive->name);
      gen(outfile, "(%A, (void*)", get(1));
      if (get(2)->typeInfo()->symbol->hasFlag(FLAG_REF) ||
          get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE))
        fputc('*', outfile);
      get(2)->codegen(outfile);
      if (get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) ||
          get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE))
        fputs(".addr", outfile);     
      gen(outfile, ", %A, %A)", get(3), get(4));
      break;
    case PRIM_COUNT_NUM_REALMS:
      INT_FATAL(this, "count num realms primitive should no longer be in AST");
      break;
    case PRIM_FTABLE_CALL:
      //
      // indirect function call via a function pointer
      //
      fprintf(outfile, "((void(*)(");
      get(2)->typeInfo()->codegen(outfile);
      if (isRecord(get(2)->typeInfo()))
        fprintf(outfile, "*");
      fprintf(outfile, ",");
      get(3)->typeInfo()->codegen(outfile);
      if (isRecord(get(3)->typeInfo()))
        fprintf(outfile, "*");
      fprintf(outfile, "))*chpl_ftable[");
      get(1)->codegen(outfile);
      fprintf(outfile, "])(");
      if (isRecord(get(2)->typeInfo()))
        fprintf(outfile, "&");
      get(2)->codegen(outfile);
      fprintf(outfile, ", ");
      if (isRecord(get(3)->typeInfo()))
        fprintf(outfile, "&");
      get(3)->codegen(outfile);
      fprintf(outfile, ")");
      break;
    case PRIM_VMT_CALL: {
      SymExpr* se = toSymExpr(get(1));
      INT_ASSERT(se);
      FnSymbol* fn = toFnSymbol(se->var);
      INT_ASSERT(fn);
      gen(outfile, "((%A(*)(", fn->retType);
      bool comma = false;
      for_formals(arg, fn) {
        if (comma)
          gen(outfile, ", ");
        comma = true;
        gen(outfile, "%A", arg->type);
        if (isRecord(arg->type))
          gen(outfile, "*");
      }
      gen(outfile, "))(*CHPL_VMT_CALL(");
      if (fRuntime)
        gen(outfile, "chpl_rt_vmtable, ");
      else
        gen(outfile, "chpl_vmtable, ");
      gen(outfile, "%A, ", get(2));
      fprintf(outfile, "%d)))(", virtualMethodMap.get(fn));
      int i = 3;
      for_formals(arg, fn) {
        if (i > 3)
          gen(outfile, ", ");
        if (isRecord(arg->type))
          gen(outfile, "&");
        gen(outfile, "%A", get(i++));
      }
      gen(outfile, ")");
      break;
    }
    case NUM_KNOWN_PRIMS:
      INT_FATAL(this, "impossible");
      break;
    case PRIM_BLOCK_XMT_PRAGMA_NOALIAS:
      // This case allows __primitive("noalias pragma") to generate 
      // _Pragma("mta assert noalias <variable list>")
      // EVERY variable within a block is added to the variable list,
      // which means several variables will spawn warnings (ignored in
      // Makefile.cray-mta). Eventually, we'll only generate invariant 
      // pointer variables.
      if (!(strcmp(CHPL_TARGET_PLATFORM, "xmt"))) {
        Vec<SymExpr*> se;
        Vec<DefExpr*> de_parent;
        Vec<VarSymbol*> vs_parent;
        collectSymExprs(this->next, se);
        collectDefExprs(this->parentExpr, de_parent);
        forv_Vec(DefExpr*, def, de_parent) {
          VarSymbol* vs = toVarSymbol(def->sym);
          if (vs) vs_parent.add(vs);
        }
        forv_Vec(SymExpr*, sym, se) {
          if (isVarSymbol(sym->var) && (!(isPrimitiveType(sym->var->type)))) {
            VarSymbol* vs = toVarSymbol(sym->var);
            if (vs_parent.in(vs))
              fprintf(outfile, "_Pragma(\"mta assert noalias *%s\")\n", sym->var->cname);
          }
        }
        this->remove();
      }
      break;
    default:
      INT_FATAL(this, "primitive codegen fail; should it still be in the AST?");
      fprintf(outfile, "/* ERR %s */", primitive->name);
    }
    if (getStmtExpr() && getStmtExpr() == this)
      fprintf(outfile, ";\n");
    return;
  }

  FnSymbol* fn = isResolved();
  INT_ASSERT(fn);

  if (fn->hasFlag(FLAG_BEGIN_BLOCK)) {
    fputs("chpl_task_addToTaskList(", outfile);
    fprintf(outfile, "/* %s */ %d, ", fn->cname, ftableMap.get(fn));
    fputs("(void*)", outfile);
    if (Expr *actuals = get(1)) {
      actuals->codegen(outfile);
    } else {
      fputs("NULL", outfile);
    }
    ClassType *bundledArgsType = toClassType(toSymExpr(get(1))->typeInfo());
    int lastField = bundledArgsType->fields.length;  // this is the _endCount field
    if (bundledArgsType->getField(lastField)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      fputs(", (", outfile);
      get(1)->codegen(outfile);
      fputs(")->", outfile);
      bundledArgsType->getField(lastField)->codegen(outfile);
      fputs(".locale != chpl_localeID ? NULL : &((", outfile);
      get(1)->codegen(outfile);
      fputs(")->", outfile);
      bundledArgsType->getField(lastField)->codegen(outfile);
      fputs(".addr", outfile);
    } else {
      fputs(", &((", outfile);
      get(1)->codegen(outfile);
      fputs(")->", outfile);
      bundledArgsType->getField(lastField)->codegen(outfile);
    }
    fputs("->taskList), (", outfile);
    if (bundledArgsType->getField(lastField)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      get(1)->codegen(outfile);
      fputs(")->", outfile);
      bundledArgsType->getField(lastField)->codegen(outfile);
      fputs(".locale", outfile);
    } else
      fputs("chpl_localeID)", outfile);
    fprintf(outfile, ", true, %d, \"%s\");\n",
            fn->lineno, fn->getModule()->filename);
    return;
  } else if (fn->hasFlag(FLAG_COBEGIN_OR_COFORALL_BLOCK)) {
    fputs("chpl_task_addToTaskList(", outfile);
    fprintf(outfile, "/* %s */ %d, ", fn->cname, ftableMap.get(fn));
    fputs("(void*)", outfile);
    if (Expr *actuals = get(1)) {
      actuals->codegen (outfile);
    } else {
      fputs("NULL", outfile);
    }
    ClassType *bundledArgsType = toClassType(toSymExpr(get(1))->typeInfo());
    int endCountField = 0;
    for (int i = 1; i <= bundledArgsType->fields.length; i++) {
      if (!strcmp(bundledArgsType->getField(i)->typeInfo()->symbol->name,
                  "_ref(_EndCount)")
          || !strcmp(bundledArgsType->getField(i)->typeInfo()->symbol->name,
                     "__wide__ref__EndCount")
          || !strcmp(bundledArgsType->getField(i)->typeInfo()->symbol->name,
                     "_EndCount")
          || !strcmp(bundledArgsType->getField(i)->typeInfo()->symbol->name,
                     "__wide__EndCount")) {
        endCountField = i;
        break;
      }
    }
    if (endCountField == 0)
      INT_FATAL(this, "cobegin/codegen codegen - _EndCount field not found");
    if (bundledArgsType->getField(endCountField)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
      fputs(", (", outfile);
      get(1)->codegen(outfile);
      fputs(")->", outfile);
      bundledArgsType->getField(endCountField)->codegen(outfile);
      fputs(".locale != chpl_localeID ? NULL : &((", outfile);
      get(1)->codegen(outfile);
      fputs(")->", outfile);
      bundledArgsType->getField(endCountField)->codegen(outfile);
      fputs(".addr->addr", outfile);
    } else if (bundledArgsType->getField(endCountField)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      fputs(", (", outfile);
      get(1)->codegen(outfile);
      fputs(")->", outfile);
      bundledArgsType->getField(endCountField)->codegen(outfile);
      fputs(".locale != chpl_localeID ? NULL : &((", outfile);
      get(1)->codegen(outfile);
      fputs(")->", outfile);
      bundledArgsType->getField(endCountField)->codegen(outfile);
      fputs(".addr", outfile);
    } else if (bundledArgsType->getField(endCountField)->typeInfo()->symbol->hasFlag(FLAG_REF)) {
      fputs(", &((*(", outfile);
      get(1)->codegen(outfile);
      fputs(")->", outfile);
      bundledArgsType->getField(endCountField)->codegen(outfile);
      fputc(')', outfile);
    } else {
      fputs(", &(((", outfile);
      get(1)->codegen(outfile);
      fputs(")->", outfile);
      bundledArgsType->getField(endCountField)->codegen(outfile);
      fputc(')', outfile);
    }
    fputs("->taskList)", outfile);
    fprintf(outfile, ", chpl_localeID, false, %d, \"%s\");\n",
            baseExpr->lineno, baseExpr->getModule()->filename);
    return;
  } else if (fn->hasFlag(FLAG_ON_BLOCK)) {
    if (fn->hasFlag(FLAG_NON_BLOCKING))
      fprintf(outfile, "chpl_comm_fork_nb");
    else if (fn->hasFlag(FLAG_FAST_ON))
      fprintf(outfile, "chpl_comm_fork_fast");
    else
      fprintf(outfile, "chpl_comm_fork");
    gen(outfile, "(%A, /* %s */ %d, %A", get(1), fn->cname,
        ftableMap.get(fn), get(2));
    TypeSymbol* argType = toTypeSymbol(get(2)->typeInfo()->symbol);
    if (argType == NULL) {
      INT_FATAL("typeInfo() didn't return a type symbol");
    }
    gen(outfile, ", sizeof(_%A), ", argType);
    genTypeStructureIndex(outfile, argType);
    gen(outfile, ");\n", argType);
    return;
  } else if (fn->hasFlag(FLAG_TX_ON_BLOCK)) {
    fprintf(outfile, "chpl_stm_tx_fork(");
    gen(outfile, "%A, %A, /* %s */ %d, %A", 
        get(1), get(2), fn->cname, txftableMap.get(fn), get(3));
    TypeSymbol* argType = toTypeSymbol(get(3)->typeInfo()->symbol);
    if (argType == NULL) {
      INT_FATAL("typeInfo() didn't return a type symbol");
    }
    gen(outfile, ", sizeof(_%A));", argType);
    return;
  }

  baseExpr->codegen(outfile);
  if (fn->hasFlag(FLAG_GPU_ON)) {
    fprintf(outfile,"<<<");
    get(1)->codegen(outfile);
    fprintf(outfile,",");
    get(2)->codegen(outfile);
    fprintf(outfile, ">>>");
  }
  fprintf(outfile, "(");

  bool first_actual = true;
  int count = 0;
  for_formals_actuals(formal, actual, this) {
    bool closeDeRefParens = false;
    if (fn->hasFlag(FLAG_GPU_ON) && count < 2) {
      count++;
      continue; // do not print nBlocks and numThreadsPerBlock
    }
    if (first_actual)
      first_actual = false;
    else
      fprintf(outfile, ", ");
    if (fn->hasFlag(FLAG_EXTERN) && actual->typeInfo()->symbol->hasFlag(FLAG_WIDE))
      fprintf(outfile, "(");
    else if (fn->hasFlag(FLAG_EXTERN) && actual->typeInfo()->symbol->hasFlag(FLAG_FIXED_STRING))
      fprintf(outfile, "(");
    else if (fn->hasFlag(FLAG_EXTERN)) {
      if (formal->type->symbol->hasFlag(FLAG_REF) &&
          formal->type->symbol->getValType()->symbol->hasFlag(FLAG_STAR_TUPLE) &&
          actual->typeInfo()->symbol->hasFlag(FLAG_REF)) {
        fprintf(outfile, "*(");
        closeDeRefParens = true;
      }
    } else if (formal->requiresCPtr() && 
               !actual->typeInfo()->symbol->hasFlag(FLAG_REF)) {
      fprintf(outfile, "&(");
      closeDeRefParens = true;
    }
    if (fn->hasFlag(FLAG_EXTERN) && actual->typeInfo() == dtString)
      fprintf(outfile, "((char*)");
    SymExpr* se = toSymExpr(actual);
    if (se && isFnSymbol(se->var))
      fprintf(outfile, "(chpl_fn_p)&");
    actual->codegen(outfile);
    if (fn->hasFlag(FLAG_EXTERN) && actual->typeInfo() == dtString)
      fprintf(outfile, ")");
    if (fn->hasFlag(FLAG_EXTERN) && actual->typeInfo()->symbol->hasFlag(FLAG_WIDE))
      fprintf(outfile, ").addr");
    else if (closeDeRefParens)
      fprintf(outfile, ")");
  }
  fprintf(outfile, ")");
  if (getStmtExpr() && getStmtExpr() == this)
    fprintf(outfile, ";\n");
  if (fn->hasFlag(FLAG_GPU_ON)) {
    fprintf(outfile, "cudaThreadSynchronize();\n");
    fprintf(outfile, "cudaError_t err = cudaGetLastError();\n");
    fprintf(outfile, "if( cudaSuccess != err) {\n");
    fprintf(outfile, "fprintf(stderr, \"CUDA error: Kernel Execution Failed in file <%%s>, line %%i : %%s.\\n\",");
    fprintf(outfile, "__FILE__, __LINE__, cudaGetErrorString( err) );\n");
    fprintf(outfile, "chpl_exit_any(-1);\n");
    fprintf(outfile, "}\n");
  }

}


bool CallExpr::isPrimitive(PrimitiveTag primitiveTag) {
  return primitive && primitive->tag == primitiveTag;
}


bool CallExpr::isPrimitive(const char* primitiveName) {
  return primitive && !strcmp(primitive->name, primitiveName);
}


NamedExpr::NamedExpr(const char* init_name, Expr* init_actual) :
  Expr(E_NamedExpr),
  name(init_name),
  actual(init_actual)
{
  gNamedExprs.add(this);
}


void NamedExpr::verify() {
  Expr::verify();
  if (astTag != E_NamedExpr) {
    INT_FATAL(this, "Bad NamedExpr::astTag");
  }
  if (actual && actual->parentExpr != this)
    INT_FATAL(this, "Bad NamedExpr::actual::parentExpr");
}


NamedExpr*
NamedExpr::copyInner(SymbolMap* map) {
  return new NamedExpr(astr(name), COPY_INT(actual));
}


void NamedExpr::replaceChild(Expr* old_ast, Expr* new_ast) {
  if (old_ast == actual) {
    actual = new_ast;
  } else {
    INT_FATAL(this, "Unexpected case in NamedExpr::replaceChild");
  }
}


Type* NamedExpr::typeInfo(void) {
  return actual->typeInfo();
}


void NamedExpr::codegen(FILE* outfile) {
  INT_FATAL(this, "NamedExpr::codegen not implemented");
}


bool 
get_int(Expr *e, long *i) {
  if (e) {
    if (SymExpr *l = toSymExpr(e)) {
      if (VarSymbol *v = toVarSymbol(l->var)) {
        if (v->immediate) {
          if (v->immediate->const_kind == NUM_KIND_INT) {
            *i = v->immediate->int_value();
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool 
get_uint(Expr *e, unsigned long *i) {
  if (e) {
    if (SymExpr *l = toSymExpr(e)) {
      if (VarSymbol *v = toVarSymbol(l->var)) {
        if (v->immediate) {
          if (v->immediate->const_kind == NUM_KIND_UINT) {
            *i = v->immediate->uint_value();
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool 
get_string(Expr *e, const char **s) {
  if (e) {
    if (SymExpr *l = toSymExpr(e)) {
      if (VarSymbol *v = toVarSymbol(l->var)) {
        if (v->immediate && v->immediate->const_kind == CONST_KIND_STRING) {
          *s = v->immediate->v_string;
          return true;
        }
      }
    }
  }
  return false;
}

const char*
get_string(Expr* e) {
  const char* s = NULL;
  if (!get_string(e, &s))
    INT_FATAL(e, "string literal expression expected");
  return s;
}

VarSymbol * 
get_constant(Expr *e) {
  if (e) {
    if (SymExpr *l = toSymExpr(e)) {
      if (VarSymbol *v = toVarSymbol(l->var)) {
        if (v->immediate != 0)
          return v;
      }
    }
  }
  return 0;
}


// getNextExpr(expr) returns the lexically next expr in a normalized
// tree
#define AST_RET_CHILD(_t, _m) \
  if (((_t*)expr)->_m) return getFirstExpr(((_t*)expr)->_m)
#define AST_RET_LIST(_t, _m) \
  if (((_t*)expr)->_m.head) return getFirstExpr(((_t*)expr)->_m.head)

Expr* getFirstExpr(Expr* expr) {
  switch (expr->astTag) {
  default:
    INT_FATAL(expr, "unexpected expr in getFirstExpr");
    return NULL;
  case E_SymExpr:
  case E_UnresolvedSymExpr:
  case E_DefExpr:
    return expr;
  case E_BlockStmt:
    AST_RET_CHILD(BlockStmt, blockInfo);
    AST_RET_LIST(BlockStmt, body);
    break;
  case E_CondStmt:
    AST_RET_CHILD(CondStmt, condExpr);
    break;
  case E_GotoStmt:
    AST_RET_CHILD(GotoStmt, label);
    break;
  case E_CallExpr:
    AST_RET_CHILD(CallExpr, baseExpr);
    AST_RET_LIST(CallExpr, argList);
    break;
  case E_NamedExpr:
    AST_RET_CHILD(NamedExpr, actual);
    break;
  }
  return expr;
}

Expr* getNextExpr(Expr* expr) {
  if (expr->next)
    return getFirstExpr(expr->next);
  if (CallExpr* parent = toCallExpr(expr->parentExpr)) {
    if (expr == parent->baseExpr && parent->argList.head)
      return getFirstExpr(parent->argList.head);
  } else if (CondStmt* parent = toCondStmt(expr->parentExpr)) {
    if (expr == parent->condExpr && parent->thenStmt)
      return getFirstExpr(parent->thenStmt);
    else if (expr == parent->thenStmt && parent->elseStmt)
      return getFirstExpr(parent->elseStmt);
  } else if (BlockStmt* parent = toBlockStmt(expr->parentExpr)) {
    if (expr == parent->blockInfo && parent->body.head)
      return getFirstExpr(parent->body.head);
  }
  if (expr->parentExpr)
    return expr->parentExpr;
  return NULL;
}

static bool
isIdentifierChar(const char c) {
  return ((c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') ||
          (c == '$') ||
          (c == '_') || (c == '.'));
}

Expr*
new_Expr(const char* format, ...) {
  va_list vl;
  va_start(vl, format);
  Expr* result = new_Expr(format, vl);
  va_end(vl);
  return result;
}

Expr*
new_Expr(const char* format, va_list vl) {
  Vec<Expr*> stack;

  for (int i = 0; format[i] != '\0'; i++) {
    if (isIdentifierChar(format[i])) {
      int n = 1;
      while (isIdentifierChar(format[i+n]))
        n++;
      const char* str = asubstr(&format[i], &format[i+n]);
      i += n-1;
      if (!strcmp(str, "TYPE")) {
        BlockStmt* block = toBlockStmt(stack.v[stack.n-1]);
        INT_ASSERT(block);
        block->blockTag = BLOCK_TYPE;
      } else {
        stack.add(new UnresolvedSymExpr(str));
      }
    } else if (format[i] == '\'') {
      int n = 1;
      while (format[i+n] != '\'')
        n++;
      const char* str = asubstr(&format[i+1], &format[i+n]);
      i += n;
      if (format[i+1] == '(') {
        PrimitiveOp* prim = primitives_map.get(str);
        INT_ASSERT(prim);
        stack.add(new CallExpr(prim));
        i++;
      } else {
        stack.add(new SymExpr(new_StringSymbol(str)));
      }
    } else if (format[i] == '%') {
      i++;
      if (format[i] == 'S')
        stack.add(new SymExpr(va_arg(vl, Symbol*)));
      else if (format[i] == 'E')
        stack.add(va_arg(vl, Expr*));
      else
        INT_FATAL("unknown format specifier in new_Expr");
    } else if (format[i] == '(') {
      Expr* expr = stack.pop();
      INT_ASSERT(expr);
      stack.add(new CallExpr(expr));
      if (format[i+1] == ')') // handle empty calls
        i++;
    } else if (format[i] == ',') {
      Expr* expr = stack.pop();
      INT_ASSERT(expr);
      CallExpr* call = toCallExpr(stack.v[stack.n-1]);
      INT_ASSERT(call);
      call->insertAtTail(expr);
    } else if (format[i] == ')') {
      Expr* expr = stack.pop();
      INT_ASSERT(expr);
      CallExpr* call = toCallExpr(stack.v[stack.n-1]);
      INT_ASSERT(call);
      call->insertAtTail(expr);
    } else if (format[i] == '{') {
      stack.add(new BlockStmt());
    } else if (format[i] == ';') {
      Expr* expr = stack.pop();
      INT_ASSERT(expr);
      BlockStmt* block = toBlockStmt(stack.v[stack.n-1]);
      INT_ASSERT(block);
      block->insertAtTail(expr);
    } else if (format[i] == '}') {
      Expr* expr = stack.pop();
      INT_ASSERT(expr);
      BlockStmt* block = toBlockStmt(stack.v[stack.n-1]);
      INT_ASSERT(block);
      block->insertAtTail(expr);
    }
  }

  INT_ASSERT(stack.n == 1);
  return stack.v[0];
}
