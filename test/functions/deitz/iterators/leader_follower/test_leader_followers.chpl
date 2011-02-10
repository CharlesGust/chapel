iter foo(n: int) {
  for i in 1..n do
    yield i;
}

iter foo(param tag: iterator, n: int) where tag == iterator.leader {
  yield 0..n-1;
}

iter foo(param tag: iterator, follower, n: int) where tag == iterator.follower {
  writeln("foo range follower called");
  for i in follower+1 do
    yield i;
}

iter bar(n: int) {
  for i in 1..n do
    yield i;
}

iter bar(param tag: iterator, n: int) where tag == iterator.leader {
  yield n-1;
}

iter bar(param tag: iterator, follower: range, n: int) where tag == iterator.follower {
  writeln("bar range follower called");
  for i in follower+1 do
    yield i;
}

iter bar(param tag: iterator, follower: int, n: int) where tag == iterator.follower {
  writeln("bar int follower called");
  for i in 1..follower+1 do
    yield i;
}

forall i in (foo(5), bar(5)) do
  writeln(i);

forall i in (bar(5), bar(5)) do
  writeln(i);
