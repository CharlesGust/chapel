record R {
  var x1: int;
  var x2: int;
  var x3: int;
}

proc main {
  var r: R;
  r.x1 = 17;
  r.x2 = 19;
  r.x3 = 23;
  _debugWriteln(here.id, ": ", r.x1, " ", r.x2, " ", r.x3);

  on Locales(1) {
    _debugWriteln(here.id, ": ", r.x1, " ", r.x2, " ", r.x3);
  }

  _debugWriteln(here.id, ": ", r.x1, " ", r.x2, " ", r.x3);
}
