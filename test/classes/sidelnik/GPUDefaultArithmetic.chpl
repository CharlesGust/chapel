type cptrtovoid = opaque;


class GPUDist: BaseDist {
  def newArithmeticDom(param rank: int, type idxType, param stridable: bool)
    return new GPUArithmeticDom(rank, idxType, stridable, this);

  def newAssociativeDom(type idxType)
    return new DefaultAssociativeDom(idxType, this);

  def newEnumDom(type idxType)
    return new DefaultEnumDom(idxType, this);

  def newOpaqueDom(type idxType)
    return new DefaultOpaqueDom(this);

  def newSparseDom(param rank: int, type idxType, dom: domain)
    return new DefaultSparseDom(rank, idxType, this, dom);
}

//
// Note that the replicated copies are set up in ChapelLocale on the
// other locales.  This just sets it up on this locale.
//
//pragma "private" var defaultDist: GPUDist = new GPUDist();

class GPUArithmeticDom: BaseArithmeticDom {
  param rank : int;
  type idxType;
  param stridable: bool;
  var dist: GPUDist;
  var ranges : rank*range(idxType,BoundedRangeType.bounded,stridable);

  def GPUArithmeticDom(param rank, type idxType, param stridable,
                                   dist) {
    this.dist = dist;
  }

  def clear() {
    var emptyRange: range(idxType, BoundedRangeType.bounded, stridable);
    for param i in 1..rank do
      ranges(i) = emptyRange;
  }
  
  def getIndices() return ranges;

  def setIndices(x) {
    if ranges.size != x.size then
      compilerError("rank mismatch in domain assignment");
    if ranges(1).eltType != x(1).eltType then
      compilerError("index type mismatch in domain assignment");
    ranges = x;
  }

  def these_help(param d: int) {
    if d == rank - 1 {
      for i in ranges(d) do
        for j in ranges(rank) do
          yield (i, j);
    } else {
      for i in ranges(d) do
        for j in these_help(d+1) do
          yield (i, (...j));
    }
  }

  def these_help(param d: int, block) {
    if d == block.size - 1 {
      for i in block(d) do
        for j in block(block.size) do
          yield (i, j);
    } else {
      for i in block(d) do
        for j in these_help(d+1, block) do
          yield (i, (...j));
    }
  }

  def these() {
    if rank == 1 {
      for i in ranges(1) do
        yield i;
    } else {
      for i in these_help(1) do
        yield i;
    }
  }

  def these(param tag: iterator) where tag == iterator.leader {
    if rank == 1 {
      yield 0..ranges(1).length-1;
    } else {
      var block = ranges;
      for param i in 1..rank do
        block(i) = 0..block(i).length-1;
      yield block;
    }
  }

  def these(param tag: iterator, follower) where tag == iterator.follower {
    if rank == 1 {
      if stridable {
        var block =
        if ranges(1).stride > 0 then
          ranges(1).low+follower.low*ranges(1).stride:ranges(1).eltType..ranges(1).low+follower.high*ranges(1).stride:ranges(1).eltType by ranges(1).stride
        else
          ranges(1).high+follower.high*ranges(1).stride:ranges(1).eltType..ranges(1).high+follower.low*ranges(1).stride:ranges(1).eltType by ranges(1).stride;
        for i in block do
          yield i;
      } else {
        var block = follower + ranges(1).low;
        for i in block do
          yield i;
      }
    } else {
      if stridable {
        for param i in 1..rank do
          follower(i) =
            if ranges(i).stride > 0 then
              ranges(i).low+follower(i).low*ranges(i).stride:ranges(i).eltType..ranges(i).low+follower(i).high*ranges(i).stride:ranges(i).eltType by ranges(i).stride
            else
              ranges(i).high+follower(i).high*ranges(i).stride:ranges(i).eltType..ranges(i).high+follower(i).low*ranges(i).stride:ranges(i).eltType by ranges(i).stride;
      } else {
        for param i in 1..rank do
          follower(i) = follower(i) + ranges(i).low;
      }
      for i in these_help(1, follower) do
        yield i;
    }
  }

  def member(ind: idxType) where rank == 1 {
    if !ranges(1).member(ind) then
      return false;
    return true;
  }

  def member(ind: rank*idxType) {
    for param i in 1..rank do
      if !ranges(i).member(ind(i)) then
        return false;
    return true;
  }

  def order(ind: idxType) where rank == 1 {
    return ranges(1).order(ind);
  }

  def order(ind: rank*idxType) {
    var totOrder: idxType;
    var blk: idxType = 1;
    for param d in 1..rank by -1 {
      const orderD = ranges(d).order(ind(d));
      if (orderD == -1) then return orderD;
      totOrder += orderD * blk;
      blk *= ranges(d).length;
    }
    return totOrder;
  }

  def position(ind: idxType) where rank == 1 {
    var pos: 1*idxType;
    pos(1) = order(ind);
    return pos;
  }

  def position(ind: rank*idxType) {
    var pos: rank*idxType;
    for d in 1..rank {
      pos(d) = ranges(d).order(ind(d));
    }
    return pos;
  }

  def dim(d : int)
    return ranges(d);

  def numIndices {
    var sum = 1:idxType;
    for param i in 1..rank do
      sum *= ranges(i).length;
    return sum;
    // WANT: return * reduce (this(1..rank).length);
  }

  def low {
    if rank == 1 {
      return ranges(1)._low;
    } else {
      var result: rank*idxType;
      for param i in 1..rank do
        result(i) = ranges(i)._low;
      return result;
    }
  }

  def high {
    if rank == 1 {
      return ranges(1)._high;
    } else {
      var result: rank*idxType;
      for param i in 1..rank do
        result(i) = ranges(i)._high;
      return result;
    }
  }

  def buildArray(type eltType) {
    return new GPUArithmeticArr(eltType, rank, idxType, stridable,
                                           dom=this);
  }

  def slice(param stridable: bool, ranges) {
    var d = new GPUArithmeticDom(rank, idxType,
                                     stridable || this.stridable, dist);
    for param i in 1..rank do
      d.ranges(i) = dim(i)(ranges(i));
    return d;
  }

  def rankChange(param rank: int, param stridable: bool, args) {
    def isRange(r: range(?e,?b,?s)) param return 1;
    def isRange(r) param return 0;

    var d = new GPUArithmeticDom(rank, idxType, stridable, dist);
    var i = 1;
    for param j in 1..args.size {
      if isRange(args(j)) {
        d.ranges(i) = dim(j)(args(j));
        i += 1;
      }
    }
    return d;
  }

  def translate(off: rank*idxType) {
    var x = new GPUArithmeticDom(rank, idxType, stridable, dist);
    for i in 1..rank do
      x.ranges(i) = dim(i).translate(off(i));
    return x;
  }

  def chpl__unTranslate(off: rank*idxType) {
    var x = new GPUArithmeticDom(rank, idxType, stridable, dist);
    for i in 1..rank do
      x.ranges(i) = dim(i).chpl__unTranslate(off(i));
    return x;
  }

  def interior(off: rank*idxType) {
    var x = new GPUArithmeticDom(rank, idxType, stridable, dist);
    for i in 1..rank do {
      if ((off(i) > 0) && (dim(i)._high+1-off(i) < dim(i)._low) ||
          (off(i) < 0) && (dim(i)._low-1-off(i) > dim(i)._high)) {
        halt("***Error: Argument to 'interior' function out of range in dimension ", i, "***");
      } 
      x.ranges(i) = dim(i).interior(off(i));
    }
    return x;
  }

  def exterior(off: rank*idxType) {
    var x = new GPUArithmeticDom(rank, idxType, stridable, dist);
    for i in 1..rank do
      x.ranges(i) = dim(i).exterior(off(i));
    return x;
  }

  def expand(off: rank*idxType) {
    var x = new GPUArithmeticDom(rank, idxType, stridable, dist);
    for i in 1..rank do {
      x.ranges(i) = ranges(i).expand(off(i));
      if (x.ranges(i)._low > x.ranges(i)._high) {
        halt("***Error: Degenerate dimension created in dimension ", i, "***");
      }
    }
    return x;
  }  

  def expand(off: idxType) {
    var x = new GPUArithmeticDom(rank, idxType, stridable, dist);
    for i in 1..rank do
      x.ranges(i) = ranges(i).expand(off);
    return x;
  }

  def strideBy(str : rank*int) {
    var x = new GPUArithmeticDom(rank, idxType, true, dist);
    for i in 1..rank do
      x.ranges(i) = ranges(i) by str(i);
    return x;
  }

  def strideBy(str : int) {
    var x = new GPUArithmeticDom(rank, idxType, true, dist);
    for i in 1..rank do
      x.ranges(i) = ranges(i) by str;
    return x;
  }
}

enum memType { CPU, GPU };
pragma "data class"
class GPUArithmeticArr: BaseArr {
  type eltType;
  param rank : int;
  type idxType;
  param stridable: bool;
  param reindexed: bool = false; // may have blk(rank) != 1

  var dom : GPUArithmeticDom(rank=rank, idxType=idxType,
                                         stridable=stridable);
  var off: rank*idxType;
  var blk: rank*idxType;
  var str: rank*int;
  var origin: idxType;
  var factoredOffs: idxType;
  var size : idxType;
  var data : _ddata(eltType);
  var noinit: bool = false;
  var D_data : cptrtovoid;
//  param mem : memType = memType.GPU;
  def mem param return memType.GPU;

  def ~GPUArithmeticArr() {
    //gpuFree(D_data);
    destroyData();
    destroyDom();
  }

  def destroyData() {
    var cnt = data.count - 1;
    data.count = cnt;
    if cnt < 0 then halt("count is negative!"); // should never happen!
    else if cnt == 0 then
      on data do delete data;
  }

  def destroyDom() {
    if !dom.supportsPrivatization() {
      var cnt = dom._count - 1;
      if cnt < 0 then halt("count is negative!"); // should never happen!
      else if cnt == 0 then
        on dom do delete dom;
      else {
        dom._arrs.remove(this);
        dom._count = cnt;
      }
    }
  }

  def these() var {
    for i in dom do
      yield this(i);
  }

  def these(param tag: iterator) where tag == iterator.leader {
    for block in dom.these(tag=iterator.leader) do
      yield block;
  }

  def these(param tag: iterator, follower) var where tag == iterator.follower {
    for i in dom.these(tag=iterator.follower, follower) do
      yield this(i);
  }

  def computeFactoredOffs() {
    factoredOffs = 0:idxType;
    for i in 1..rank do {
      factoredOffs = factoredOffs + blk(i) * off(i);
    }
  }

  def initialize() {
    if noinit == true then return;
    for param dim in 1..rank {
      off(dim) = dom.dim(dim)._low;
      str(dim) = dom.dim(dim)._stride;
      writeln("off(",dim,") = ",off(dim)," str(",dim,") = ",str(dim));
    }
    blk(rank) = 1:idxType;
    for param dim in 1..rank-1 by -1 do
      blk(dim) = blk(dim+1) * dom.dim(dim+1).length;
    computeFactoredOffs();
    size = blk(1) * dom.dim(1).length;
    writeln("cudaMalloc of size ", size);
    //gpuAllocate(D_data, size);

    data = new _ddata(eltType);
//    writeln("data = ",data);
    data.init(size);
  }

  pragma "inline"
  def this(ind: idxType ...1) var where rank == 1
    return this(ind);

  pragma "inline"
  def this(ind : rank*idxType) var {
    if boundsChecking then
      if !dom.member(ind) then
        halt("array index out of bounds: ", ind);
    var sum = origin;
    if stridable {
      for param i in 1..rank do
        sum += (ind(i) - off(i)) * blk(i) / str(i):idxType;
    } else {
      if reindexed {
        for param i in 1..rank do
          sum += ind(i) * blk(i);
      } else {
        for param i in 1..rank-1 do
          sum += ind(i) * blk(i);
        sum += ind(rank);
      }
      sum -= factoredOffs;
    }
    return __primitive("array_get", this, sum);
    //return data(sum);
  }

  def reindex(d: GPUArithmeticDom) {
    if rank != d.rank then
      compilerError("illegal implicit rank change");
    for param i in 1..rank do
      if d.dim(i).length != dom.dim(i).length then
        halt("extent in dimension ", i, " does not match actual");
    var alias = new GPUArithmeticArr(eltType, d.rank, d.idxType,
                                                d.stridable, true, d,
                                                noinit=true);
    //    was:  (eltType, rank, idxType, d.stridable, true, d, noinit=true);
    d._count += 1;
    data.count += 1;
    alias.data = data;
    alias.size = size: d.idxType;
    for param i in 1..rank {
      alias.off(i) = d.dim(i)._low;
      alias.blk(i) = (blk(i) * dom.dim(i)._stride / str(i)) : d.idxType;
      alias.str(i) = d.dim(i)._stride;
    }
    alias.origin = origin:d.idxType;
    alias.computeFactoredOffs();
    return alias;
  }

  def checkSlice(ranges) {
    for param i in 1..rank do
      if !dom.dim(i).boundsCheck(ranges(i)) then
        halt("array slice out of bounds in dimension ", i, ": ", ranges(i));
  }

  def slice(d: GPUArithmeticDom) {
    var alias = new GPUArithmeticArr(eltType, rank, idxType,
                                                d.stridable, reindexed,
                                                d, noinit=true);
    d._count += 1;
    data.count += 1;
    alias.data = data;
    alias.size = size;
    alias.blk = blk;
    alias.str = str;
    alias.origin = origin;
    for param i in 1..rank {
      alias.off(i) = d.dim(i)._low;
      alias.origin += blk(i) * (d.dim(i)._low - off(i)) / str(i);
    }
    alias.computeFactoredOffs();
    return alias;
  }

  def checkRankChange(args) {
    def isRange(r: range(?e,?b,?s)) param return 1;
    def isRange(r) param return 0;

    for param i in 1..args.size do
      if isRange(args(i)) then
        if !dom.dim(i).boundsCheck(args(i)) then
          halt("array slice out of bounds in dimension ", i, ": ", args(i));
  }

  def rankChange(param newRank: int, param newStridable: bool, args) {
    var d = dom.rankChange(newRank, newStridable, args);

    def isRange(r: range(?e,?b,?s)) param return 1;
    def isRange(r) param return 0;

    var alias = new GPUArithmeticArr(eltType, newRank, idxType,
                                                newStridable, true, d,
                                                noinit=true);
    d._count += 1;
    data.count += 1;
    alias.data = data;
    alias.size = size;
    var i = 1;
    alias.origin = origin;
    for param j in 1..args.size {
      if isRange(args(j)) {
        alias.off(i) = d.dim(i)._low;
        alias.origin += blk(j) * (d.dim(i)._low - off(j)) / str(j);
        alias.blk(i) = blk(j);
        alias.str(i) = str(j);
        i += 1;
      } else {
        alias.origin += blk(j) * (args(j) - off(j)) / str(j);
      }
    }
    alias.computeFactoredOffs();
    return alias;
  }

  def reallocate(d: domain) {
    if (d._value.type == dom.type) {
      var copy = new GPUArithmeticArr(eltType, rank, idxType,
                                                 d._value.stridable, reindexed,
                                                 d._value);
      copy.dom._count += 2;
      for i in d((...dom.ranges)) do
        copy(i) = this(i);
      off = copy.off;
      blk = copy.blk;
      str = copy.str;
      origin = copy.origin;
      factoredOffs = copy.factoredOffs;
      size = copy.size;
      destroyData();
      data = copy.data;
      data.count += 1;
      delete copy;
    } else {
      halt("illegal reallocation");
    }
  }

  def tupleInit(b: _tuple) {
    def _tupleInitHelp(j, param rank: int, b: _tuple) {
      if rank == 1 {
        for param i in 1..b.size {
          j(this.rank-rank+1) = dom.dim(this.rank-rank+1).low + i - 1;
          this(j) = b(i);
        }
      } else {
        for param i in 1..b.size {
          j(this.rank-rank+1) = dom.dim(this.rank-rank+1).low + i - 1;
          _tupleInitHelp(j, rank-1, b(i));
        }
      }
    }

    if rank == 1 {
      for param i in 1..b.size do
        this(this.dom.dim(1).low + i - 1) = b(i);
    } else {
      var j: rank*int;
      _tupleInitHelp(j, rank, b);
    }
  }
}

def GPUArithmeticDom.writeThis(f: Writer) {
  f.write("[", dim(1));
  for i in 2..rank do
    f.write(", ", dim(i));
  f.write("]");
}

def GPUArithmeticArr.writeThis(f: Writer) {
  if dom.numIndices == 0 then return;
  var i : rank*idxType;
  for dim in 1..rank do
    i(dim) = dom.dim(dim)._low;
  label next while true {
    f.write(this(i));
    if i(rank) <= (dom.dim(rank)._high - dom.dim(rank)._stride:idxType) {
      f.write(" ");
      i(rank) += dom.dim(rank)._stride:idxType;
    } else {
      for dim in 1..rank-1 by -1 {
        if i(dim) <= (dom.dim(dim)._high - dom.dim(dim)._stride:idxType) {
          i(dim) += dom.dim(dim)._stride:idxType;
          for dim2 in dim+1..rank {
            f.writeln();
            i(dim2) = dom.dim(dim2)._low;
          }
          continue next;
        }
      }
      break;
    }
  }
}

def main() {
	const GPUBlockDist = new GPUDist();
	const space : domain(1) dmapped GPUBlockDist = [1..100];

	var A, B : [space] int;
}
