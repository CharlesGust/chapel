#include "astutil.h"
#include "bb.h"
#include "bitVec.h"
#include "expr.h"
#include "optimizations.h"
#include "stmt.h"
#include "view.h"


static void
reachingDefinitionsAnalysis(FnSymbol* fn,
                            Vec<SymExpr*>& defs,
                            Map<SymExpr*,int>& defMap,
                            Vec<SymExpr*>& useSet,
                            Vec<SymExpr*>& defSet,
                            Vec<BitVec*>& IN) {
  Vec<Symbol*> locals;
  Map<Symbol*,int> localMap;
  buildLocalsVectorMap(fn, locals, localMap);
  buildDefUseSets(locals, fn, defSet, useSet);

  //
  // compute defs and defMap in an order such that defs of the same
  // variable are adjacent (this is important!); use localDefsMap to
  // make this linear; localDefsMap is like the defMap computed by
  // buildDefUseMaps, but is computed from the defSet in this more
  // efficient manner
  //
  int localDefs[locals.n];
  for (int i = 0; i < locals.n; i++)
    localDefs[i] = 0;
  forv_Vec(SymExpr, se, defSet) {
    if (se) {
      localDefs[localMap.get(se->var)]++;
      defs.add(NULL);
    }
  }
  int sum = 0;
  for (int i = 0; i < locals.n; i++) {
    int nextSum = localDefs[i];
    localDefs[i] = sum;
    sum += nextSum;
  }
  forv_Vec(SymExpr, se, defSet) {
    if (se) {
      int i = localDefs[localMap.get(se->var)]++;
      defs.v[i] = se;
      defMap.put(se, i);
    }
  }

  Vec<BitVec*> KILL;
  Vec<BitVec*> GEN;
  Vec<BitVec*> OUT;

  forv_Vec(BasicBlock, bb, *fn->basicBlocks) {
    Vec<Symbol*> bbDefSet;
    BitVec* kill = new BitVec(defs.n);
    BitVec* gen = new BitVec(defs.n);
    BitVec* in = new BitVec(defs.n);
    BitVec* out = new BitVec(defs.n);
    for (int i = bb->exprs.n-1; i >= 0; i--) {
      Expr* expr = bb->exprs.v[i];
      Vec<SymExpr*> symExprs;
      collectSymExprs(expr, symExprs);
      forv_Vec(SymExpr, se, symExprs) {
        if (defSet.set_in(se)) {
          if (!bbDefSet.set_in(se->var)) {
            gen->set(defMap.get(se));
          }
          bbDefSet.set_add(se->var);
        }
      }
    }
    for (int i = 0; i < defs.n; i++) {
      if (bbDefSet.set_in(defs.v[i]->var))
        kill->set(i);
    }
    KILL.add(kill);
    GEN.add(gen);
    IN.add(in);
    OUT.add(out);
  }

#ifdef DEBUG_REACHING
  list_view(fn);
  printBasicBlocks(fn);
  printDefsVector(defs, defMap);
  printf("KILL:\n"); printBitVectorSets(KILL);
  printf("GEN:\n"); printBitVectorSets(GEN);
  printf("IN:\n"); printBitVectorSets(IN);
  printf("OUT:\n"); printBitVectorSets(OUT);
#endif

  forwardFlowAnalysis(fn, GEN, KILL, IN, OUT, false);

#ifdef DEBUG_REACHING
  printf("IN:\n"); printBitVectorSets(IN);
  printf("OUT:\n"); printBitVectorSets(OUT);
#endif

  forv_Vec(BitVec, gen, GEN)
    delete gen;

  forv_Vec(BitVec, kill, KILL)
    delete kill;

  forv_Vec(BitVec, out, OUT)
    delete out;
}


void
buildDefUseChains(FnSymbol* fn,
                  Map<SymExpr*,Vec<SymExpr*>*>& DU,
                  Map<SymExpr*,Vec<SymExpr*>*>& UD) {
  Vec<SymExpr*> defs;
  Map<SymExpr*,int> defMap;
  Vec<SymExpr*> useSet;
  Vec<SymExpr*> defSet;
  Vec<BitVec*> IN;
  reachingDefinitionsAnalysis(fn, defs, defMap, useSet, defSet, IN);

  //
  // map from symbol to index into defs vector where defs of symbol
  // begin
  //
  Map<Symbol*,int> defsIndexMap;
  Symbol* last = 0;
  int i = 0;
  forv_Vec(SymExpr, se, defs) {
    if (se->var != last)
      defsIndexMap.put(se->var, i);
    last = se->var;
    i++;
  }

  forv_Vec(SymExpr, def, defs) {
    DU.put(def, new Vec<SymExpr*>());
  }

  for (int i = 0; i < fn->basicBlocks->n; i++) {
    BasicBlock* bb = fn->basicBlocks->v[i];
    BitVec* in = IN.v[i];
    forv_Vec(Expr, expr, bb->exprs) {
      Vec<SymExpr*> symExprs;
      collectSymExprs(expr, symExprs);
      forv_Vec(SymExpr, se, symExprs) {
        if (useSet.set_in(se)) {
          UD.put(se, new Vec<SymExpr*>());
          for (int j = defsIndexMap.get(se->var); j < defs.n; j++) {
            if (defs.v[j]->var != se->var)
              break;
            if (in->get(j)) {
              DU.get(defs.v[j])->add(se);
              UD.get(se)->add(defs.v[j]);
            }
          }
        }
      }
      forv_Vec(SymExpr, se, symExprs) {
        if (defSet.set_in(se)) {
          for (int j = defsIndexMap.get(se->var); j < defs.n; j++) {
            if (defs.v[j]->var != se->var)
              break;
            if (defs.v[j] == se)
              in->set(j);
            else
              in->unset(j);
          }
        }
      }
    }
  }
  forv_Vec(BitVec, in, IN)
    delete in;
}

typedef MapElem<SymExpr*,Vec<SymExpr*>*> SymExprToVecSymExprMapElem;

void
freeDefUseChains(Map<SymExpr*,Vec<SymExpr*>*>& DU,
                 Map<SymExpr*,Vec<SymExpr*>*>& UD) {
  form_Map(SymExprToVecSymExprMapElem, e, DU)
    delete e->value;
  form_Map(SymExprToVecSymExprMapElem, e, UD)
    delete e->value;
}
