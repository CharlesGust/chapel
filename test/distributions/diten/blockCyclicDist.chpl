class BlockCyclicDist {
  type idxType;
  param nDims:int = 2;
  const blockSize: nDims*int;
  const startLoc:  nDims*int;
  const localeDomain: domain(nDims);
  const locales: [localeDomain] locale;

  def idxToLocaleInd(ind: idxType...nDims) {
    var locInd: nDims*idxType;
    for i in 1..nDims {
      locInd(i) = (startLoc(i) + (ind(i)-1)/blockSize(i)) % localeDomain.dim(i).length;
    }
    return locInd;
  }

  def idxToLocale(ind: idxType...nDims) {
    return locales(idxToLocaleInd((...ind)));
  }
  def getBlock(ind: idxType...nDims) {
    var locInd: nDims*idxType;
    for i in 1..nDims {
      locInd(i) = (ind(i) - 1) / (localeDomain.dim(i).length*blockSize(i));
    }
    return locInd;
  }
  def getBlockPosition(ind: idxType...nDims) {
    var locInd: nDims*idxType;
    for i in 1..nDims {
      locInd(i) = ((ind(i)-1) % blockSize(i)) + 1;
    }
    return locInd;
  }
  def getLocalPosition(ind:idxType...nDims) {
    var blk = getBlock((...ind));
    var blkPos = getBlockPosition((...ind));
    var position: nDims*idxType;
    for i in 1..nDims {
      position(i) = blk(i)*blockSize(i) + blkPos(i);
    }
    return position;
  }

  def buildDomain(d: domain(nDims)) {
    return new BlockCyclicDom(nDims, idxType, d, this);
  }

}

class BlockCyclicDom {
  param nDims: int = 2;
  type idxType;
  const whole: domain(nDims, idxType);
  const dist: BlockCyclicDist(idxType);
  const locDoms: [dist.localeDomain] LocBlockCyclicDom(nDims, idxType);
  def initialize() {
    var blksInDim: nDims*idxType;
    for i in 1..nDims {
      blksInDim(i) = ceil(whole.dim(i).length:real(64) /
                          dist.blockSize(i)):idxType;
    }

    for pos in dist.localeDomain {
      var locSize: nDims*idxType;
      
      for dim in 1..nDims {
        var remainder: idxType;
        locSize(dim) = dist.blockSize(dim) * (blksInDim(dim) / dist.localeDomain.dim(dim).length);
        remainder = whole.dim(dim).length - (locSize(dim) * dist.localeDomain.dim(dim).length);
        if ((1+pos(dim))*dist.blockSize(dim) <= remainder) {
          locSize(dim) += dist.blockSize(dim);
        } else if (remainder - pos(dim)*dist.blockSize(dim) > 0) {
          locSize(dim) += remainder - pos(dim)*dist.blockSize(dim);
        }
      }
      on dist.locales(pos) {
        var locRanges: nDims*range(idxType, BoundedRangeType.bounded);
        for i in 1..nDims {
          locRanges(i) = 1..locSize(i);
        }
        locDoms(pos) = new LocBlockCyclicDom(nDims, idxType, this, locRanges);
      }
      //writeln(pos, locSize);
    }
  }

  def buildArray(type eltType) {
    return new BlockCyclicArr(nDims, idxType, eltType, this);
  }

}


class LocBlockCyclicDom {
  param nDims: int;
  type idxType;
  const whole: BlockCyclicDom(nDims, idxType);
  const locRanges: nDims*range(idxType, BoundedRangeType.bounded);
  const locDom: domain(nDims);
  def initialize() {
    locDom.setIndices(locRanges);
  }
}


class BlockCyclicArr {
  param nDims: int;
  type idxType;
  type eltType;
  const dom: BlockCyclicDom(nDims, idxType);
  const locArrs: [dom.dist.localeDomain] LocBlockCyclicArr(nDims, idxType, eltType);
  def initialize() {
    for ind in dom.dist.localeDomain {
      on dom.dist.locales(ind) {
        locArrs(ind) = new LocBlockCyclicArr(nDims, idxType, eltType, this, dom.locDoms(ind));
      }
    }
  }
  def this(ind:idxType...nDims) var {
    return locArrs(dom.dist.idxToLocaleInd((...ind))).arr(dom.dist.getLocalPosition((...ind)));
  }
}

class LocBlockCyclicArr {
  param nDims: int;
  type idxType;
  type eltType;
  const whole: BlockCyclicArr(nDims, idxType, eltType);
  const locDom: LocBlockCyclicDom(nDims, idxType);
  const arr: [locDom.locDom] eltType;
}

config const m, n = 2;


def main {
  var locDom: domain(2) = [0..#m, 0..#n];
  var locs: [locDom] locale;
  var undistributedDom: domain(2) = [1..5, 1..5];

  [(i,j) in locDom] locs(i,j) = Locales((i*n + j) % numLocales);

  var dist = new BlockCyclicDist(idxType=int, nDims=2,
                                   blockSize=(2,2), startLoc=(0,0),
                                   localeDomain=locDom, locales=locs);
  var dom = dist.buildDomain(undistributedDom);
  var arr = dom.buildArray(int);
  for (i,j) in undistributedDom {
    arr(i,j) = i*10 + j;
  }
  for (i,j) in undistributedDom {
    writeln((i,j), " ", arr(i,j));
    //writeln((i,j), " ", arr(i,j), " ", arr(i,j).locale);
  }
}
