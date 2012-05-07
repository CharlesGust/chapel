//
// Atomics primer
//
// This primer illustrates Chapel's atomic variables.  For more information
// on Chapel's atomics, see $CHPL_HOME/docs/technotes/README.atomics
//
config const n = 31;
const R = 1..n;

// The 'atomic' keyword is a type qualifiers that can be applied to
// the following Chapel primitive types to declare atomic variables:
//
// - bool
// - int (all supported sizes)
// - uint (all supported sizes)
//
// Atomic variables support a limited number of operations that are
// currently implemented as methods.
//

var x: atomic int;

// Atomic variables can only be used or assigned via the read() and
// write() methods, respectively.
//

x.write(n);
if x.read() != n then
  halt("Error: x (", x.read(), ") != n (", n, ")");

// All atomic types support atomic exchange(),
// compareExchangeStrong(), and compareExchangeWeak().
//
// The exchange() method atomically swaps the old value with the given
// argument and returns the old value.  The compareExchangeStrong()
// and compareExchangeWeak() methods only perform the swap if the
// current value is equal to the first method argument, returning true
// if the exchange succeeded.  See README.atomics for the difference
// between the weak and strong variety.
//
// In the following example, n parallel tasks are created via a
// coforall statement.  Each task tries repeatedly to set the current
// value of x to id-1, but only succeeds when x's current value is
// equal to id.
//
coforall id in R {
  while !x.compareExchangeWeak(id, id-1) do ;
}
if x.read() != 0 then
  halt("Error: x != 0 (", x.read(), ")");

// As a convenience, atomic bools also support testAndSet() and clear().
// The testAndSet() method atomically reads the value of the variable, sets
// it to true, then returns the original value.  The clear() method
// atomically sets the value to false.
//
// In the following example, we create n tasks and each task calls
// testAndSet() on the atomic variable named flag and saves the
// result in a unique element of the result array.
//
var flag: atomic bool;
var result: [R] bool;
coforall r in result do
  r = flag.testAndSet();

// When the coforall is complete, only one of the above testAndSet()
// calls should have returned false.
//
var found = false;
for r in result {
  if !r then
    if found then
      halt("Error: found multiple times!");
    else
      found = true;
}
if !found then
  halt("Error: not found!");
flag.clear();


// The integral atomic types also support the following atomic
// fetch operations:
//
// - fetchAdd
// - fetchSub
// - fetchOr (bit-wise)
// - fetchAnd (bit-wise)
//
// Each of the above atomically reads the variable, stores the result
// of the operation (+, -, |, or &) using the value and the method
// argument, then returns the original value.
//
// In the following example, we create n tasks to atomically increment
// the atomic variable a with the square of the task's given id.
//
var a: atomic int;
coforall id in R do a.fetchAdd(id*id);

// The sum of this finite series should be n(n+1)*(2n+1)/6
//
if a.read() != n*(n+1)*(2*n+1)/6 then
  halt("Error: a=", a, " (should be", n*(n+1)*(2*n+1)/6, ")");
