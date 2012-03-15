#include "astutil.h"
#include "build.h"
#include "caches.h"
#include "callInfo.h"
#include "chpl.h"
#include "expr.h"
#include "resolution.h"
#include "stmt.h"
#include "symbol.h"


static FnSymbol*
buildEmptyWrapper(FnSymbol* fn, CallInfo* info) {
  FnSymbol* wrapper = new FnSymbol(fn->name);
  wrapper->addFlag(FLAG_WRAPPER);
  wrapper->addFlag(FLAG_INVISIBLE_FN);
  wrapper->addFlag(FLAG_INLINE);
  if (fn->hasFlag(FLAG_NO_PARENS))
    wrapper->addFlag(FLAG_NO_PARENS);
  if (fn->hasFlag(FLAG_CONSTRUCTOR))
    wrapper->addFlag(FLAG_CONSTRUCTOR);
  if (!fn->hasFlag(FLAG_ITERATOR_FN)) { // getValue is var, not iterator
    wrapper->retTag = fn->retTag;
    if (fn->setter)
      wrapper->setter = fn->setter->copy();
  }
  if (fn->hasFlag(FLAG_METHOD))
    wrapper->addFlag(FLAG_METHOD);
  wrapper->instantiationPoint = getVisibilityBlock(info->call);
  wrapper->addFlag(FLAG_TEMP);
  return wrapper;
}


//
// copy a formal and make the copy have blank intent. If the formal to copy has
// out intent or inout intent, flag the copy to make sure it is a reference
//
static ArgSymbol* copyFormalForWrapper(ArgSymbol* formal) {
  ArgSymbol* wrapperFormal = formal->copy();
  if (formal->intent == INTENT_OUT || formal->intent == INTENT_INOUT ||
      formal->hasFlag(FLAG_WRAP_OUT_INTENT)) {
    wrapperFormal->addFlag(FLAG_WRAP_OUT_INTENT);
  }
  wrapperFormal->intent = INTENT_BLANK;
  return wrapperFormal;
}


//
// return true if formal matches name of a subsequent formal
//
static bool
isShadowedField(ArgSymbol* formal) {
  DefExpr* tmp = toDefExpr(formal->defPoint->next);
  while (tmp) {
    if (!strcmp(tmp->sym->name, formal->name))
      return true;
    tmp = toDefExpr(tmp->next);
  }
  return false;
}


static void
insertWrappedCall(FnSymbol* fn, FnSymbol* wrapper, CallExpr* call) {
  if ((!fn->hasFlag(FLAG_EXTERN) && fn->getReturnSymbol() == gVoid) ||
      (fn->hasFlag(FLAG_EXTERN) && fn->retType == dtVoid)) {
    wrapper->insertAtTail(call);
  } else {
    Symbol* tmp = newTemp();
    tmp->addFlag(FLAG_EXPR_TEMP);
    tmp->addFlag(FLAG_MAYBE_PARAM);
    tmp->addFlag(FLAG_MAYBE_TYPE);
    wrapper->insertAtTail(new DefExpr(tmp));
    wrapper->insertAtTail(new CallExpr(PRIM_MOVE, tmp, call));
    wrapper->insertAtTail(new CallExpr(PRIM_RETURN, tmp));
  }
  fn->defPoint->insertAfter(new DefExpr(wrapper));
}


////
//// default wrapper code
////


static FnSymbol*
buildDefaultWrapper(FnSymbol* fn,
                    Vec<Symbol*>* defaults,
                    SymbolMap* paramMap,
                    CallInfo* info) {
  if (FnSymbol* cached = checkCache(defaultsCache, fn, defaults))
    return cached;
  SET_LINENO(fn);
  FnSymbol* wrapper = buildEmptyWrapper(fn, info);
  if (!fn->hasFlag(FLAG_ITERATOR_FN))
    wrapper->retType = fn->retType;
  wrapper->cname = astr("_default_wrap_", fn->cname);
  SymbolMap copy_map;
  bool specializeDefaultConstructor =
    fn->hasFlag(FLAG_DEFAULT_CONSTRUCTOR) &&
    !isSyncType(fn->_this->type) &&
    !fn->_this->type->symbol->hasFlag(FLAG_REF);
  if (specializeDefaultConstructor) {
    wrapper->removeFlag(FLAG_TEMP);
    wrapper->_this = fn->_this->copy();
    copy_map.put(fn->_this, wrapper->_this);
    wrapper->insertAtTail(new DefExpr(wrapper->_this));
    if (defaults->v[defaults->n-1]->hasFlag(FLAG_IS_MEME)) {
      if (!isRecord(fn->_this->type) && !isUnion(fn->_this->type)) {
        wrapper->insertAtTail(new CallExpr(PRIM_MOVE, wrapper->_this,
                                new CallExpr(PRIM_CHPL_ALLOC, wrapper->_this,
                                newMemDesc(fn->_this->type->symbol->name))));
        wrapper->insertAtTail(new CallExpr(PRIM_SETCID, wrapper->_this));
      }
    }
    wrapper->insertAtTail(new CallExpr(PRIM_INIT_FIELDS, wrapper->_this));
  }
  CallExpr* call = new CallExpr(fn);
  call->square = info->call->square;
  for_formals(formal, fn) {
    SET_LINENO(formal);
    if (!defaults->in(formal)) {
      ArgSymbol* wrapper_formal = copyFormalForWrapper(formal);
      if (fn->_this == formal)
        wrapper->_this = wrapper_formal;
      if (formal->hasFlag(FLAG_IS_MEME))
        wrapper->_this->defPoint->insertAfter(new CallExpr(PRIM_MOVE, wrapper->_this, wrapper_formal)); // unexecuted none/gasnet on 4/25/08
      wrapper->insertFormalAtTail(wrapper_formal);
      Symbol* temp;
      if (formal->type->symbol->hasFlag(FLAG_REF)) {
        temp = newTemp();
        temp->addFlag(FLAG_MAYBE_PARAM);
        if (formal->hasFlag(FLAG_TYPE_VARIABLE))
          temp->addFlag(FLAG_TYPE_VARIABLE); // unexecuted none/gasnet on 4/25/08
        wrapper->insertAtTail(new DefExpr(temp));
        wrapper->insertAtTail(new CallExpr(PRIM_MOVE, temp, new CallExpr(PRIM_SET_REF, wrapper_formal)));
      } else if (specializeDefaultConstructor && wrapper_formal->typeExpr &&
                 isRefCountedType(wrapper_formal->type)) {
        temp = newTemp();
        if (Symbol* field = wrapper->_this->type->getField(formal->name, false))
          if (field->defPoint->parentSymbol == wrapper->_this->type->symbol)
            temp->addFlag(FLAG_INSERT_AUTO_DESTROY);
        wrapper->insertAtTail(new DefExpr(temp));
        BlockStmt* typeExpr = wrapper_formal->typeExpr->copy();
        for_alist(expr, typeExpr->body) {
          wrapper->insertAtTail(expr->remove());
        }
        bool isArrayAliasField = false;
        const char* aliasFieldArg = astr("chpl__aliasField_", formal->name);
        for_formals(formal, fn)
          if (formal->name == aliasFieldArg && !defaults->set_in(formal))
            isArrayAliasField = true;
        if (isArrayAliasField) {
          Expr* arrayTypeExpr = wrapper->body->body.tail->remove();
          Symbol* arrayTypeTmp = newTemp();
          arrayTypeTmp->addFlag(FLAG_MAYBE_TYPE);
          arrayTypeTmp->addFlag(FLAG_EXPR_TEMP);
          temp->addFlag(FLAG_EXPR_TEMP);
          wrapper->insertAtTail(new DefExpr(arrayTypeTmp));
          wrapper->insertAtTail(new CallExpr(PRIM_MOVE, arrayTypeTmp, arrayTypeExpr));
          wrapper->insertAtTail(new CallExpr(PRIM_MOVE, temp, new CallExpr("reindex", gMethodToken, wrapper_formal, new CallExpr("chpl__getDomainFromArrayType", arrayTypeTmp))));
        } else {
          wrapper->insertAtTail(new CallExpr(PRIM_MOVE, temp, new CallExpr(PRIM_INIT, wrapper->body->body.tail->remove())));
          wrapper->insertAtTail(new CallExpr(PRIM_MOVE, temp, new CallExpr("=", temp, wrapper_formal)));
        }
      } else
        temp = wrapper_formal;
      copy_map.put(formal, temp);
      call->insertAtTail(temp);
      if (Symbol* value = paramMap->get(formal))
        paramMap->put(wrapper_formal, value);
      if (specializeDefaultConstructor && strcmp(fn->name, "_construct__tuple")) // && strcmp(fn->name, "_construct__square_tuple"))
        if (!formal->hasFlag(FLAG_TYPE_VARIABLE) && !paramMap->get(formal) && formal->type != dtMethodToken)
          if (Symbol* field = wrapper->_this->type->getField(formal->name, false))
            if (field->defPoint->parentSymbol == wrapper->_this->type->symbol)
              if (!isShadowedField(formal)) {
                Symbol* copyTemp = newTemp();
                wrapper->insertAtTail(new DefExpr(copyTemp));
                wrapper->insertAtTail(new CallExpr(PRIM_MOVE, copyTemp, new CallExpr("chpl__autoCopy", temp)));
                wrapper->insertAtTail(
                  new CallExpr(PRIM_SET_MEMBER, wrapper->_this,
                               new_StringSymbol(formal->name), copyTemp));
                copy_map.put(formal, copyTemp);
                call->argList.tail->replace(new SymExpr(copyTemp));
              }
    } else if (paramMap->get(formal)) {
      // handle instantiated param formals
      call->insertAtTail(paramMap->get(formal));
    } else if (formal->hasFlag(FLAG_IS_MEME)) {

      //
      // hack: why is the type of meme set to dtNil?
      //
      formal->type = wrapper->_this->type;

      call->insertAtTail(wrapper->_this);
    } else {
      const char* temp_name = astr("_default_temp_", formal->name);
      VarSymbol* temp = newTemp(temp_name);
      if (formal->intent != INTENT_INOUT && formal->intent != INTENT_OUT) {
        temp->addFlag(FLAG_MAYBE_PARAM);
        temp->addFlag(FLAG_EXPR_TEMP);
      }
      if (formal->hasFlag(FLAG_TYPE_VARIABLE))
        temp->addFlag(FLAG_TYPE_VARIABLE);
      copy_map.put(formal, temp);
      wrapper->insertAtTail(new DefExpr(temp));
      if (formal->intent == INTENT_OUT ||
          !formal->defaultExpr ||
          (formal->defaultExpr->body.length == 1 &&
           isSymExpr(formal->defaultExpr->body.tail) &&
           toSymExpr(formal->defaultExpr->body.tail)->var == gTypeDefaultToken)) {
        // use default value for type as default value for formal argument
        if (formal->typeExpr) {
          BlockStmt* typeExpr = formal->typeExpr->copy();
          for_alist(expr, typeExpr->body) {
            wrapper->insertAtTail(expr->remove());
          }
          if (formal->hasFlag(FLAG_TYPE_VARIABLE))
            wrapper->insertAtTail(new CallExpr(PRIM_MOVE, temp, wrapper->body->body.tail->remove()));
          else
            wrapper->insertAtTail(new CallExpr(PRIM_MOVE, temp, new CallExpr(PRIM_INIT, wrapper->body->body.tail->remove())));
        } else {
          if (formal->hasFlag(FLAG_TYPE_VARIABLE))
            wrapper->insertAtTail(new CallExpr(PRIM_MOVE, temp, new SymExpr(formal->type->symbol)));
          else
            wrapper->insertAtTail(new CallExpr(PRIM_MOVE, temp, new CallExpr(PRIM_INIT, new SymExpr(formal->type->symbol))));
        }
      } else {
        BlockStmt* defaultExpr = formal->defaultExpr->copy();
        for_alist(expr, defaultExpr->body) {
          wrapper->insertAtTail(expr->remove());
        }
        if (formal->intent != INTENT_INOUT) {
          wrapper->insertAtTail(new CallExpr(PRIM_MOVE, temp, wrapper->body->body.tail->remove()));
        } else {
          wrapper->insertAtTail(new CallExpr(PRIM_MOVE, temp, new CallExpr("chpl__initCopy", wrapper->body->body.tail->remove())));
          INT_ASSERT(!temp->hasFlag(FLAG_EXPR_TEMP));
          temp->removeFlag(FLAG_MAYBE_PARAM);
        }
      }
      call->insertAtTail(temp);
      if (specializeDefaultConstructor && strcmp(fn->name, "_construct__tuple")) // && strcmp(fn->name, "_construct__square_tuple"))
        if (!formal->hasFlag(FLAG_TYPE_VARIABLE))
          if (Symbol* field = wrapper->_this->type->getField(formal->name, false))
            if (field->defPoint->parentSymbol == wrapper->_this->type->symbol)
              if (!isShadowedField(formal))
                wrapper->insertAtTail(
                  new CallExpr(PRIM_SET_MEMBER, wrapper->_this,
                               new_StringSymbol(formal->name), temp));
    }
  }
  update_symbols(wrapper->body, &copy_map);

  insertWrappedCall(fn, wrapper, call);

  addCache(defaultsCache, fn, wrapper, defaults);
  normalize(wrapper);
  return wrapper;
}


FnSymbol*
defaultWrap(FnSymbol* fn,
            Vec<ArgSymbol*>* actualFormals,
            CallInfo* info) {
  FnSymbol* wrapper = fn;
  int num_actuals = actualFormals->n;
  int num_formals = fn->numFormals();
  if (num_formals > num_actuals) {
    Vec<Symbol*> defaults;
    for_formals(formal, fn) {
      bool used = false;
      forv_Vec(ArgSymbol, arg, *actualFormals) {
        if (arg == formal)
          used = true;
      }
      if (!used)
        defaults.add(formal);
    }
    wrapper = buildDefaultWrapper(fn, &defaults, &paramMap, info);
    resolveFormals(wrapper);

    // update actualFormals for use in orderWrap
    int j = 1;
    for_formals(formal, fn) {
      for (int i = 0; i < actualFormals->n; i++) {
        if (actualFormals->v[i] == formal) {
          ArgSymbol* newFormal = wrapper->getFormal(j);
          actualFormals->v[i] = newFormal;
          j++;
        }
      }
    }
  }
  return wrapper;
}


////
//// order wrapper code
////


static FnSymbol*
buildOrderWrapper(FnSymbol* fn,
                  SymbolMap* order_map,
                  CallInfo* info) {
  // return cached if we already created this order wrapper
  if (FnSymbol* cached = checkCache(ordersCache, fn, order_map))
    return cached;
  SET_LINENO(fn);
  FnSymbol* wrapper = buildEmptyWrapper(fn, info);
  wrapper->cname = astr("_order_wrap_", fn->cname);
  CallExpr* call = new CallExpr(fn);
  call->square = info->call->square;
  SymbolMap copy_map;
  for_formals(formal, fn) {
    SET_LINENO(formal);
    ArgSymbol* wrapper_formal = copyFormalForWrapper(formal);
    if (fn->_this == formal)
      wrapper->_this = wrapper_formal;
    copy_map.put(formal, wrapper_formal);
  }
  for_formals(formal, fn) {
    SET_LINENO(formal);
    wrapper->insertFormalAtTail(copy_map.get(order_map->get(formal)));
    if (formal->instantiatedParam)
      call->insertAtTail(paramMap.get(formal));
    else
      call->insertAtTail(copy_map.get(formal));
  }
  insertWrappedCall(fn, wrapper, call);
  normalize(wrapper);
  addCache(ordersCache, fn, wrapper, order_map);
  return wrapper;
}


FnSymbol*
orderWrap(FnSymbol* fn,
          Vec<ArgSymbol*>* actualFormals,
          CallInfo* info) {
  bool order_wrapper_required = false;
  SymbolMap formals_to_formals;
  int i = 0;
  for_formals(formal, fn) {
    i++;

    int j = 0;
    forv_Vec(ArgSymbol, af, *actualFormals) {
      j++;
      if (af == formal) {
        if (i != j)
          order_wrapper_required = true;
        formals_to_formals.put(formal, actualFormals->v[i-1]);
      }
    }
  }
  if (order_wrapper_required) {
    fn = buildOrderWrapper(fn, &formals_to_formals, info);
    resolveFormals(fn);
  }
  return fn;
}


////
//// coercion wrapper code
////


static FnSymbol*
buildCoercionWrapper(FnSymbol* fn,
                     SymbolMap* coercion_map,
                     Map<ArgSymbol*,bool>* coercions,
                     CallInfo* info) {
  // return cached if we already created this coercion wrapper
  if (FnSymbol* cached = checkCache(coercionsCache, fn, coercion_map))
    return cached;

  SET_LINENO(fn);
  FnSymbol* wrapper = buildEmptyWrapper(fn, info);

  //
  // stopgap: important for, for example, --no-local on
  // test/parallel/cobegin/bradc/varsEscape-workaround.chpl; when
  // function resolution is out-of-order, disabling this for
  // unspecified return types may not be necessary
  //
  if (fn->hasFlag(FLAG_SPECIFIED_RETURN_TYPE) && !fn->hasFlag(FLAG_ITERATOR_FN))
    wrapper->retType = fn->retType;

  wrapper->cname = astr("_coerce_wrap_", fn->cname);
  CallExpr* call = new CallExpr(fn);
  call->square = info->call->square;
  for_formals(formal, fn) {
    SET_LINENO(formal);
    ArgSymbol* wrapperFormal = copyFormalForWrapper(formal);
    if (fn->_this == formal)
      wrapper->_this = wrapperFormal;
    wrapper->insertFormalAtTail(wrapperFormal);
    if (coercions->get(formal)) {
      TypeSymbol *ts = toTypeSymbol(coercion_map->get(formal));
      INT_ASSERT(ts);
      wrapperFormal->type = ts->type;
      if (getSyncFlags(ts).any()) {
        //
        // apply readFF or readFE to single or sync actual unless this
        // is a member access of the sync or single actual
        //
        if (fn->numFormals() >= 2 &&
            fn->getFormal(1)->type == dtMethodToken &&
            formal == fn->_this)
          call->insertAtTail(new CallExpr("value", gMethodToken, wrapperFormal));
        else if (ts->hasFlag(FLAG_SINGLE))
          call->insertAtTail(new CallExpr("readFF", gMethodToken, wrapperFormal));
        else if (ts->hasFlag(FLAG_SYNC))
          call->insertAtTail(new CallExpr("readFE", gMethodToken, wrapperFormal));
        else
          INT_ASSERT(false);    // Unhandled case.
      } else if (ts->hasFlag(FLAG_REF)) {
        //
        // dereference reference actual
        //
        call->insertAtTail(new CallExpr(PRIM_GET_REF, wrapperFormal));
      } else if (wrapperFormal->instantiatedParam) {
        call->insertAtTail(new CallExpr("_cast", formal->type->symbol, paramMap.get(formal)));
      } else {
        call->insertAtTail(new CallExpr("_cast", formal->type->symbol, wrapperFormal));
      }
    } else {
      if (Symbol* actualTypeSymbol = coercion_map->get(formal))
        if (!(formal == fn->_this && fn->hasFlag(FLAG_REF_THIS)))
          wrapperFormal->type = actualTypeSymbol->type;
      call->insertAtTail(wrapperFormal);
    }
  }
  insertWrappedCall(fn, wrapper, call);
  normalize(wrapper);
  addCache(coercionsCache, fn, wrapper, coercion_map);
  return wrapper;
}


FnSymbol*
coercionWrap(FnSymbol* fn, CallInfo* info) {
  SymbolMap subs;
  Map<ArgSymbol*,bool> coercions;
  int j = -1;
  bool coerce = false;
  for_formals(formal, fn) {
    j++;
    Type* actualType = info->actuals.v[j]->type;
    Symbol* actualSym = info->actuals.v[j];
    if (actualType != formal->type) {
      if (canCoerce(actualType, actualSym, formal->type, fn) || isDispatchParent(actualType, formal->type)) {
        subs.put(formal, actualType->symbol);
        coercions.put(formal,true);
        coerce = true;
      } else {
        subs.put(formal, actualType->symbol);
      }
    }
  }
  if (coerce) {
    fn = buildCoercionWrapper(fn, &subs, &coercions, info);
    resolveFormals(fn);
  }
  return fn;  
}


////
//// promotion wrapper code
////


static FnSymbol*
buildPromotionWrapper(FnSymbol* fn,
                      SymbolMap* promotion_subs,
                      CallInfo* info) {
  // return cached if we already created this promotion wrapper
  SymbolMap map;
  Vec<Symbol*> keys;
  promotion_subs->get_keys(keys);
  forv_Vec(Symbol*, key, keys)
    map.put(key, promotion_subs->get(key));
  map.put(fn, (Symbol*)info->call->square); // add value of square to cache
  if (FnSymbol* cached = checkCache(promotionsCache, fn, &map))
    return cached;

  SET_LINENO(fn);
  FnSymbol* wrapper = buildEmptyWrapper(fn, info);
  wrapper->addFlag(FLAG_PROMOTION_WRAPPER);
  wrapper->cname = astr("_promotion_wrap_", fn->cname);
  CallExpr* indicesCall = new CallExpr("_build_tuple"); // destructured in build
  CallExpr* iterator = new CallExpr(info->call->square ? "chpl__buildDomainExpr" : "_build_tuple");
  CallExpr* actualCall = new CallExpr(fn);
  int i = 1;
  for_formals(formal, fn) {
    SET_LINENO(formal);
    ArgSymbol* new_formal = copyFormalForWrapper(formal);
    if (Symbol* p = paramMap.get(formal))
      paramMap.put(new_formal, p);
    if (fn->_this == formal)
      wrapper->_this = new_formal;
    if (Symbol* sub = promotion_subs->get(formal)) {
      TypeSymbol* ts = toTypeSymbol(sub);
      if (!ts)
        INT_FATAL(fn, "error building promotion wrapper");
      new_formal->type = ts->type;
      wrapper->insertFormalAtTail(new_formal);
      iterator->insertAtTail(new_formal);
      VarSymbol* index = newTemp(astr("_p_i_", istr(i)));
      wrapper->insertAtTail(new DefExpr(index));
      indicesCall->insertAtTail(index);
      actualCall->insertAtTail(index);
    } else {
      wrapper->insertFormalAtTail(new_formal);
      actualCall->insertAtTail(new_formal);
    }
    i++;
  }

  Expr* indices = indicesCall;
  if (indicesCall->numActuals() == 1)
    indices = indicesCall->get(1)->remove();
  if ((!fn->hasFlag(FLAG_EXTERN) && fn->getReturnSymbol() == gVoid) ||
      (fn->hasFlag(FLAG_EXTERN) && fn->retType == dtVoid)) {
    if (fSerial || fSerialForall)
      wrapper->insertAtTail(new BlockStmt(buildForLoopStmt(indices, iterator, new BlockStmt(actualCall))));
    else
      wrapper->insertAtTail(new BlockStmt(buildForallLoopStmt(indices, iterator, new BlockStmt(actualCall))));
  } else {
    wrapper->addFlag(FLAG_ITERATOR_FN);
    wrapper->removeFlag(FLAG_INLINE);
    if (!fSerial && !fSerialForall) {
      SymbolMap leaderMap;
      FnSymbol* lifn = wrapper->copy(&leaderMap);
      iteratorLeaderMap.put(wrapper,lifn);
      lifn->body = new BlockStmt(); // indices are not used in leader
      form_Map(SymbolMapElem, e, leaderMap) {
        if (Symbol* s = paramMap.get(e->key))
          paramMap.put(e->value, s);
      }
      ArgSymbol* lifnTag = new ArgSymbol(INTENT_PARAM, "tag", gLeaderTag->type);
      lifn->addFlag(FLAG_INLINE_ITERATOR);
      lifn->insertFormalAtTail(lifnTag);
      lifn->where = new BlockStmt(new CallExpr("==", lifnTag, gLeaderTag));
      VarSymbol* leaderIndex = newTemp("_leaderIndex");
      VarSymbol* leaderIterator = newTemp("_leaderIterator");
      leaderIterator->addFlag(FLAG_EXPR_TEMP);
      lifn->insertAtTail(new DefExpr(leaderIterator));
      lifn->insertAtTail(new CallExpr(PRIM_MOVE, leaderIterator, new CallExpr("_toLeader", iterator->copy(&leaderMap))));
      BlockStmt* body = new BlockStmt(new CallExpr(PRIM_YIELD, leaderIndex));
      BlockStmt* loop = buildForLoopStmt(new SymExpr(leaderIndex), new SymExpr(leaderIterator), body);
      body->insertAtHead(new DefExpr(leaderIndex));
      lifn->insertAtTail(loop);
      theProgram->block->insertAtTail(new DefExpr(lifn));
      normalize(lifn);
      lifn->instantiationPoint = getVisibilityBlock(info->call);

      SymbolMap followerMap;
      FnSymbol* fifn = wrapper->copy(&followerMap);
      iteratorFollowerMap.put(wrapper,fifn);
      form_Map(SymbolMapElem, e, followerMap) {
        if (Symbol* s = paramMap.get(e->key))
          paramMap.put(e->value, s);
      }
      ArgSymbol* fifnTag = new ArgSymbol(INTENT_PARAM, "tag", gFollowerTag->type);
      fifn->insertFormalAtTail(fifnTag);
      ArgSymbol* fifnFollower = new ArgSymbol(INTENT_BLANK, iterFollowthisArgname, dtAny);
      fifn->insertFormalAtTail(fifnFollower);
      fifn->where = new BlockStmt(new CallExpr("==", fifnTag, gFollowerTag));
      VarSymbol* followerIterator = newTemp("_followerIterator");
      followerIterator->addFlag(FLAG_EXPR_TEMP);
      fifn->insertAtTail(new DefExpr(followerIterator));
      fifn->insertAtTail(new CallExpr(PRIM_MOVE, followerIterator, new CallExpr("_toFollower", iterator->copy(&followerMap), fifnFollower)));
      BlockStmt* followerBlock = new BlockStmt();
      Symbol* yieldTmp = newTemp();
      yieldTmp->addFlag(FLAG_EXPR_TEMP);
      followerBlock->insertAtTail(new DefExpr(yieldTmp));
      followerBlock->insertAtTail(new CallExpr(PRIM_MOVE, yieldTmp, actualCall->copy(&followerMap)));
      followerBlock->insertAtTail(new CallExpr(PRIM_YIELD, yieldTmp));
      fifn->insertAtTail(buildForLoopStmt(indices->copy(&followerMap), new SymExpr(followerIterator), followerBlock));
      theProgram->block->insertAtTail(new DefExpr(fifn));
      normalize(fifn);
      fifn->addFlag(FLAG_GENERIC);
      fifn->instantiationPoint = getVisibilityBlock(info->call);
    }
    BlockStmt* yieldBlock = new BlockStmt();
    Symbol* yieldTmp = newTemp();
    yieldTmp->addFlag(FLAG_EXPR_TEMP);
    yieldBlock->insertAtTail(new DefExpr(yieldTmp));
    yieldBlock->insertAtTail(new CallExpr(PRIM_MOVE, yieldTmp, actualCall));
    yieldBlock->insertAtTail(new CallExpr(PRIM_YIELD, yieldTmp));
    wrapper->insertAtTail(new BlockStmt(buildForLoopStmt(indices, iterator, yieldBlock)));
  }
  fn->defPoint->insertBefore(new DefExpr(wrapper));
  normalize(wrapper);
  addCache(promotionsCache, fn, wrapper, &map);
  return wrapper;
}


FnSymbol*
promotionWrap(FnSymbol* fn, CallInfo* info) {
  Vec<Symbol*>* actuals = &info->actuals;
  if (!strcmp(fn->name, "="))
    return fn;
  bool promotion_wrapper_required = false;
  SymbolMap promoted_subs;
  int j = -1;
  for_formals(formal, fn) {
    j++;
    Type* actualType = actuals->v[j]->type;
    Symbol* actualSym = actuals->v[j];
    bool promotes = false;
    if (canDispatch(actualType, actualSym, formal->type, fn, &promotes)){
      if (promotes) {
        promotion_wrapper_required = true;
        promoted_subs.put(formal, actualType->symbol);
      }
    }
  }
  if (promotion_wrapper_required) {
    if (fWarnPromotion)
      USR_WARN(info->call, "promotion on %s", toString(info));
    fn = buildPromotionWrapper(fn, &promoted_subs, info);
    resolveFormals(fn);
  }
  return fn;
}
