interface LessThan {
  proc <(x:self, y:self):bool;
}

int implements LessThan;

proc min(type T, x:T, y:T) : T where T implements LessThan {
  if (y < x) {
    return y;
  } else {
    return x;
  }
}

proc test() : int checked {
  min(int, 3, 4);
}

int implements LessThan;
min(int,4,5);
