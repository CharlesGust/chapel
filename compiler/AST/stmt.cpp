#include <cstring>
#include "astutil.h"
#include "expr.h"
#include "files.h"
#include "misc.h"
#include "passes.h"
#include "stmt.h"
#include "stringutil.h"


void codegenStmt(FILE* outfile, Expr* stmt) {
  if (stmt->lineno > 0) {
    if (printCppLineno) {
      fprintf(outfile, "/* ZLINE: %d %s */\n", stmt->lineno, stmt->getModule()->filename);
    } 
  }
}


BlockStmt::BlockStmt(Expr* init_body, BlockTag init_blockTag) :
  Expr(E_BlockStmt),
  blockTag(init_blockTag),
  body(),
  blockInfo(NULL),
  modUses(NULL),
  breakLabel(NULL),
  continueLabel(NULL),
  userLabel(NULL)
{
  body.parent = this;
  if (init_body)
    body.insertAtTail(init_body);
  gBlockStmts.add(this);
}


BlockStmt::~BlockStmt() { }


void BlockStmt::verify() {
  Expr::verify();
  if (astTag != E_BlockStmt) {
    INT_FATAL(this, "Bad BlockStmt::astTag");
  }
  if (body.parent != this)
    INT_FATAL(this, "Bad AList::parent in BlockStmt");
  for_alist(expr, body) {
    if (expr->parentExpr != this)
      INT_FATAL(this, "Bad BlockStmt::body::parentExpr");
  }
  if (blockInfo && blockInfo->parentExpr != this)
    INT_FATAL(this, "Bad BlockStmt::blockInfo::parentExpr");
  if (modUses && modUses->parentExpr != this)
    INT_FATAL(this, "Bad BlockStmt::blockInfo::parentExpr");
}


BlockStmt*
BlockStmt::copyInner(SymbolMap* map) {
  BlockStmt* _this = new BlockStmt();
  _this->blockTag = blockTag;
  for_alist(expr, body)
    _this->insertAtTail(COPY_INT(expr));
  _this->blockInfo = COPY_INT(blockInfo);
  _this->modUses = COPY_INT(modUses);
  _this->breakLabel = breakLabel;
  _this->continueLabel = continueLabel;
  return _this;
}


void BlockStmt::replaceChild(Expr* old_ast, Expr* new_ast) {
  if (old_ast == blockInfo)
    blockInfo = toCallExpr(new_ast);
  else if (old_ast == modUses)
    modUses = toCallExpr(new_ast);
  else
    INT_FATAL(this, "Unexpected case in BlockStmt::replaceChild");
}


void BlockStmt::codegen(FILE* outfile) {
  codegenStmt(outfile, this);

  if (blockInfo) {
    if (blockInfo->isPrimitive(PRIM_BLOCK_WHILEDO_LOOP)) {
      fprintf(outfile, "while (");
      blockInfo->get(1)->codegen(outfile);
      fprintf(outfile, ") ");
    } else if (blockInfo->isPrimitive(PRIM_BLOCK_DOWHILE_LOOP)) {
      fprintf(outfile, "do ");
    } else if (blockInfo->isPrimitive(PRIM_BLOCK_FOR_LOOP)) {
      fprintf(outfile, "for (;");
      blockInfo->get(1)->codegen(outfile);
      fprintf(outfile, ";) ");
    }
  }

  if (this != getFunction()->body)
    fprintf(outfile, "{\n");

  body.codegen(outfile, "");

  if (blockInfo && blockInfo->isPrimitive(PRIM_BLOCK_DOWHILE_LOOP)) {
    fprintf(outfile, "} while (");
    blockInfo->get(1)->codegen(outfile);
    fprintf(outfile, ");\n");
  } else if (this != getFunction()->body) {
    fprintf(outfile, "}");
    CondStmt* cond = toCondStmt(parentExpr);
    if (!cond || !(cond->thenStmt == this && cond->elseStmt))
      fprintf(outfile, "\n");
  }
}


void
BlockStmt::insertAtHead(Expr* ast) {
  body.insertAtHead(ast);
}


void
BlockStmt::insertAtTail(Expr* ast) {
  body.insertAtTail(ast);
}


void
BlockStmt::insertAtHead(const char* format, ...) {
  va_list args;

  va_start(args, format);
  insertAtHead(new_Expr(format, args));
  va_end(args);
}


void
BlockStmt::insertAtTail(const char* format, ...) {
  va_list args;

  va_start(args, format);
  insertAtTail(new_Expr(format, args));
  va_end(args);
}


void
BlockStmt::insertAtTailBeforeGoto(Expr* ast) {
  if (isGotoStmt(body.tail))
    body.tail->insertBefore(ast);
  else
    body.insertAtTail(ast);
}


bool
BlockStmt::isLoop(void) {
  return (blockInfo &&
          (blockInfo->isPrimitive(PRIM_BLOCK_DOWHILE_LOOP) ||
           blockInfo->isPrimitive(PRIM_BLOCK_WHILEDO_LOOP) ||
           blockInfo->isPrimitive(PRIM_BLOCK_PARAM_LOOP) ||
           blockInfo->isPrimitive(PRIM_BLOCK_FOR_LOOP)));
}


int
BlockStmt::length(void) {
  return body.length;
}


void
BlockStmt::addUse(ModuleSymbol* mod) {
  if (!modUses) {
    modUses = new CallExpr(PRIM_USED_MODULES_LIST);
    if (parentSymbol)
      insert_help(modUses, this, parentSymbol);
  }
  modUses->insertAtTail(mod);
}


CondStmt::CondStmt(Expr* iCondExpr, BaseAST* iThenStmt, BaseAST* iElseStmt) :
  Expr(E_CondStmt),
  condExpr(iCondExpr),
  thenStmt(NULL),
  elseStmt(NULL)
{
  if (Expr* s = toExpr(iThenStmt)) {
    BlockStmt* bs = toBlockStmt(s);
    if (bs && bs->blockTag == BLOCK_NORMAL && !bs->blockInfo)
      thenStmt = bs;
    else
      thenStmt = new BlockStmt(s);
    } else
    INT_FATAL(iThenStmt, "Bad then-stmt passed to CondStmt constructor");
  if (iElseStmt) {
    if (Expr* s = toExpr(iElseStmt)) {
      BlockStmt* bs = toBlockStmt(s);
      if (bs && bs->blockTag == BLOCK_NORMAL && !bs->blockInfo)
        elseStmt = bs;
      else
        elseStmt = new BlockStmt(s);
    } else
      INT_FATAL(iElseStmt, "Bad else-stmt passed to CondStmt constructor");
  }
  gCondStmts.add(this);
}


Expr*
CondStmt::fold_cond_stmt() {
  Expr* result = NULL;
  if (SymExpr* cond = toSymExpr(condExpr)) {
    if (VarSymbol* var = toVarSymbol(cond->var)) {
      if (var->immediate &&
          var->immediate->const_kind == NUM_KIND_UINT &&
          var->immediate->num_index == INT_SIZE_1) {
        result = new CallExpr(PRIM_NOOP);
        this->insertBefore(result);
        if (var->immediate->v_bool == gTrue->immediate->v_bool) {
          Expr* then_stmt = thenStmt;
          then_stmt->remove();
          this->replace(then_stmt);
        } else if (var->immediate->v_bool == gFalse->immediate->v_bool) {
          Expr* else_stmt = elseStmt;
          if (else_stmt) {
            else_stmt->remove();
            this->replace(else_stmt);
          } else {
            this->remove();
          }
        }
      }
    }
  }
  return result;
}


void CondStmt::verify() {
  Expr::verify();
  if (astTag != E_CondStmt) {
    INT_FATAL(this, "Bad CondStmt::astTag");
  }

  if (!condExpr) {
    INT_FATAL(this, "CondStmt has no condExpr");
  }

  if (!thenStmt) {
    INT_FATAL(this, "CondStmt has no thenStmt");
  }

  if (condExpr->list) {
    INT_FATAL(this, "CondStmt::condExpr is a list");
  }

  if (thenStmt->list) {
    INT_FATAL(this, "CondStmt::thenStmt is a list");
  }

  if (elseStmt && elseStmt->list) {
    INT_FATAL(this, "CondStmt::elseStmt is a list");
  }

  if (condExpr && condExpr->parentExpr != this)
    INT_FATAL(this, "Bad CondStmt::condExpr::parentExpr");

  if (thenStmt && thenStmt->parentExpr != this)
    INT_FATAL(this, "Bad CondStmt::thenStmt::parentExpr");

  if (elseStmt && elseStmt->parentExpr != this)
    INT_FATAL(this, "Bad CondStmt::elseStmt::parentExpr");

}


CondStmt*
CondStmt::copyInner(SymbolMap* map) {
  return new CondStmt(COPY_INT(condExpr),
                      COPY_INT(thenStmt),
                      COPY_INT(elseStmt));
}


void CondStmt::replaceChild(Expr* old_ast, Expr* new_ast) {
  if (old_ast == condExpr) {
    condExpr = new_ast;
  } else if (old_ast == thenStmt) {
    thenStmt = toBlockStmt(new_ast);
  } else if (old_ast == elseStmt) {
    elseStmt = toBlockStmt(new_ast);
  } else {
    INT_FATAL(this, "Unexpected case in CondStmt::replaceChild");
  }
}


void CondStmt::codegen(FILE* outfile) {
  codegenStmt(outfile, this);
  fprintf(outfile, "if (");
  condExpr->codegen(outfile);
  fprintf(outfile, ") ");
  thenStmt->codegen(outfile);
  if (elseStmt) {
    fprintf(outfile, " else ");
    elseStmt->codegen(outfile);
  }
}


GotoStmt::GotoStmt(GotoTag init_gotoTag, const char* init_label) :
  Expr(E_GotoStmt),
  gotoTag(init_gotoTag),
  label(init_label ? (Expr*)new UnresolvedSymExpr(init_label) : (Expr*)new SymExpr(gNil))
{
  gGotoStmts.add(this);
}


GotoStmt::GotoStmt(GotoTag init_gotoTag, Symbol* init_label) :
  Expr(E_GotoStmt),
  gotoTag(init_gotoTag),
  label(new SymExpr(init_label))
{
  gGotoStmts.add(this);
}


GotoStmt::GotoStmt(GotoTag init_gotoTag, Expr* init_label) :
  Expr(E_GotoStmt),
  gotoTag(init_gotoTag),
  label(init_label)
{
  if (!init_label)
    INT_FATAL(this, "GotoStmt initialized with null label");

  if (init_label->parentSymbol)
    INT_FATAL(this, "GotoStmt initialized with label already in tree");

  gGotoStmts.add(this);
}


void GotoStmt::verify() {
  Expr::verify();
  if (astTag != E_GotoStmt) {
    INT_FATAL(this, "Bad GotoStmt::astTag");
  }
  if (!label)
    INT_FATAL(this, "GotoStmt has no label");
  if (label->list)
    INT_FATAL(this, "GotoStmt::label is a list");
  if (label && label->parentExpr != this)
    INT_FATAL(this, "Bad GotoStmt::label::parentExpr");
  if (SymExpr* se = toSymExpr(label)) {
    if (isLabelSymbol(se->var)) {
      if (!isFnSymbol(se->var->defPoint->parentSymbol))
        INT_FATAL(this, "goto label is not in a function");
      if (se->var->defPoint->parentSymbol != this->parentSymbol)
        INT_FATAL(this, "goto label is in a different function than the goto");
    }
  }
}


GotoStmt*
GotoStmt::copyInner(SymbolMap* map) {
  return new GotoStmt(gotoTag, COPY_INT(label));
}


void GotoStmt::replaceChild(Expr* old_ast, Expr* new_ast) {
  if (old_ast == label) {
    label = toSymExpr(new_ast);
  } else {
    INT_FATAL(this, "Unexpected case in GotoStmt::replaceChild");
  }
}


void GotoStmt::codegen(FILE* outfile) {
  codegenStmt(outfile, this);
  fprintf(outfile, "goto ");
  label->codegen(outfile);
  fprintf(outfile, ";\n");
}


const char* GotoStmt::getName() {
  if (SymExpr* se = toSymExpr(label))
    return se->var->name;
  else if (UnresolvedSymExpr* use = toUnresolvedSymExpr(label))
    return use->unresolved;
  else
    return NULL;
}
