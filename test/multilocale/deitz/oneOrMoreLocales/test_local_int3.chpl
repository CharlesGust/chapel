proc main {
  var x: int = 17;
  _debugWriteln(here.id + " x=" + x);
  x = x+1;
  for i in 0..numLocales-1 {
    on Locales(i) {
      _debugWriteln(here.id + " x=" + x);
      x = x+1;
    }
  }
  _debugWriteln(here.id + " x=" + x);
}

