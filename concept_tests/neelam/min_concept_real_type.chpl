interface LessThan {
  proc <(x:self, y:self):bool;
}

real implements LessThan;

proc min(type T, x:T, y:T) : T where T implements LessThan {
  if (y < x) {
    return y;
  } else {
    return x;
  }
}

min(real,3.0, 4.0);
