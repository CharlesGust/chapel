var x, y = 0;
proc A(i: int) var {
  if i < 0 || i > 1 then
    halt("array access out of bounds");
  if setter && i == 1 && x <= 0 then
    halt("cannot assign value to A(1) if A(0) <= 0");
  if i == 0 then
    return x;
  else
    return y;
}
A(0) = 1;
A(1) = 2;

writeln(A(0) + A(1));
