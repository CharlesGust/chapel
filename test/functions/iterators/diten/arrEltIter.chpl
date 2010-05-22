
def f() {
  var Arr: [0..10] int;
  for (c, d) in (Arr, 0..10) {
    c = d; // nil dereference in assigning to "c"
  }
  for elem in Arr do
    yield elem;
}

def main {
  for i in f() do
    writeln(i);
}
