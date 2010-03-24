use List;

config const maxDataParallelism = 0;
config const limitDataParallelism = true;
config const minDataParallelismSize: uint(64) = 0;

//
// Abstract distribution class
//
pragma "base dist"
class BaseDist {
  var _distCnt$: sync int = 0; // distribution reference count and lock
  var _doms: list(BaseDom);    // arrays declared over this domain

  pragma "dont disable remote value forwarding"
  def destroyDist(dom: BaseDom = nil) {
    var cnt = _distCnt$ - 1;
    if cnt < 0 then
      halt("distribution reference count is negative!");
    if dom then
      on dom do
        _doms.remove(dom);
    _distCnt$ = cnt;
    return cnt;
  }

  def dsiNewArithmeticDom(param rank: int, type idxType, param stridable: bool) {
    compilerError("arithmetic domains not supported by this distribution");
  }

  def dsiNewAssociativeDom(type idxType) {
    compilerError("associative domains not supported by this distribution");
  }

  def dsiNewAssociativeDom(type idxType) where __primitive("isEnumType", idxType) {
    compilerError("enumerated domains not supported by this distribution");
  }

  def dsiNewOpaqueDom(type idxType) {
    compilerError("opaque domains not supported by this distribution");
  }

  def dsiNewSparseDom(param rank: int, type idxType, dom: domain) {
    compilerError("sparse domains not supported by this distribution");
  }

  def dsiSupportsPrivatization() param return false;
  def dsiRequiresPrivatization() param return false;

  def dsiDestroyDistClass() { }
}

//
// Abstract domain classes
//
class BaseDom {
  var _domCnt$: sync int = 0; // domain reference count and lock
  var _arrs: list(BaseArr);   // arrays declared over this domain

  def getBaseDist(): BaseDist {
    return nil;
  }

  pragma "dont disable remote value forwarding"
  def destroyDom(arr: BaseArr = nil) {
    var cnt = _domCnt$ - 1;
    if cnt < 0 then
      halt("domain reference count is negative!");
    if arr then
      on arr do
        _arrs.remove(arr);
    _domCnt$ = cnt;
    if cnt == 0 {
      var dist = getBaseDist();
      if dist then on dist {
        var cnt = dist.destroyDist(this);
        if cnt == 0 then
          delete dist;
      }
    }
    return cnt;
  }

  // used for associative domains/arrays
  def _backupArrays() {
    for arr in _arrs do
      arr._backupArray();
  }

  def _removeArrayBackups() {
    for arr in _arrs do
      arr._removeArrayBackup();
  }

  def _preserveArrayElement(oldslot, newslot) {
    for arr in _arrs do
      arr._preserveArrayElement(oldslot, newslot);
  }

  def dsiSupportsPrivatization() param return false;
  def dsiRequiresPrivatization() param return false;

  // false for default distribution so that we don't increment the
  // default distribution's reference count and add domains to the
  // default distribution's list of domains
  def linksDistribution() param return true;
}

class BaseArithmeticDom : BaseDom {
  def dsiClear() {
    halt("clear not implemented for this distribution");
  }

  def clearForIteratableAssign() {
    compilerError("Illegal assignment to an arithmetic domain");
  }

  def dsiAdd(x) {
    compilerError("Cannot add indices to an arithmetic domain");
  }
}

class BaseSparseDom : BaseDom {
  def dsiClear() {
    halt("clear not implemented for this distribution");
  }

  def clearForIteratableAssign() {
    dsiClear();
  }
}

class BaseAssociativeDom : BaseDom {
  def dsiClear() {
    halt("clear not implemented for this distribution");
  }

  def clearForIteratableAssign() {
    dsiClear();
  }
}

class BaseOpaqueDom : BaseDom {
  def dsiClear() {
    halt("clear not implemented for this distribution");
  }

  def clearForIteratableAssign() {
    dsiClear();
  }
}

//
// Abstract array class
//
pragma "base array"
class BaseArr {
  var _arrCnt$: sync int = 0; // array reference count (and eventually lock)
  var _arrAlias: BaseArr;     // reference to base array if an alias

  def dsiStaticFastFollowCheck(type leadType) param return false;

  def canCopyFromDevice param return false;
  def canCopyFromHost param return false;

  def dsiGetBaseDom(): BaseDom {
    return nil;
  }

  pragma "dont disable remote value forwarding"
  def destroyArr(): int {
    var cnt = _arrCnt$ - 1;
    if cnt < 0 then
      halt("array reference count is negative!");
    _arrCnt$ = cnt;
    if cnt == 0 {
      if _arrAlias {
        on _arrAlias {
          var cnt = _arrAlias.destroyArr();
          if cnt == 0 then
            delete _arrAlias;
        }
      } else {
        dsiDestroyData();
      }
      var dom = dsiGetBaseDom();
      on dom {
        var cnt = dom.destroyDom(this);
        if cnt == 0 then
          delete dom;
      }
    }
    return cnt;
  }

  def dsiDestroyData() { }

  def dsiReallocate(d: domain) {
    halt("reallocating not supported for this array type");
  }

  // This method is unsatisfactory -- see bradc's commit entries of
  // 01/02/08 around 14:30 for details
  def _purge( ind: int) {
    halt("purging not supported for this array type");
  }

  def _resize( length: int, old_map) {
    halt("resizing not supported for this array type");
  }

  def sparseShiftArray(shiftrange, initrange) {
    halt("sparseGrowDomain not supported for non-sparse arrays");
  }

  // methods for associative arrays
  def _backupArray() {
    halt("_backupArray() not supported for non-associative arrays");
  }

  def _removeArrayBackup() {
    halt("_removeArrayBackup() not supported for non-associative arrays");
  }

  def _preserveArrayElement(oldslot, newslot) {
    halt("_preserveArrayElement() not supported for non-associative arrays");
  }

  def dsiSupportsAlignedFollower() param return false;

  def dsiSupportsPrivatization() param return false;
  def dsiRequiresPrivatization() param return false;
}
