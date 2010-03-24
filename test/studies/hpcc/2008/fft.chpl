//
// Use standard modules for Bit operations, Random numbers, and Timing
//
use BitOps, Random, Time;

//
// Use shared user module for computing HPCC problem sizes
//
use HPCCProblemSize;

const radix = 4;               // the radix of this FFT implementation

const numVectors = 2;          // the number of vectors to be stored
type elemType = complex(128);  // the element type of the vectors

//
// A configuration constant defining log2(problem size) -- n -- and a
// constant defining the problem size itself -- m
//
config const n = computeProblemSize(numVectors, elemType, returnLog2 = true);
const m = 2**n;

//
// Configuration constants defining the epsilon and threshold values
// used to verify the result
//
config const epsilon = 2.0 ** -51.0,
             threshold = 16.0;

//
// Configuration constants to indicate whether or not to use a
// pseudo-random seed (based on the clock) or a fixed seed; and to
// specify the fixed seed explicitly
//
config const useRandomSeed = true,
             seed = if useRandomSeed then SeedGenerator.currentTime else 314159265;

//
// Configuration constants to control what's printed -- benchmark
// parameters, input and output arrays, and/or statistics
//
config const printParams = true,
             printArrays = false,
             printStats = true;

//
// The program entry point
//
def main() {
  printConfiguration();          // print the problem size

  //
  // TwiddleDom describes the index set used to define the vector of
  // twiddle values and is a 1D domain indexed by 64-bit ints from 0
  // to m/4-1.  Twiddles is the vector of twiddle values.
  //
  const TwiddleDom: domain(1, int(64)) = [0..m/4-1];
  var Twiddles: [TwiddleDom] elemType;

  //
  // ProblemDom describes the index set used to define the input and
  // output vectors and is also a 1D domain indexed by 64-bit ints
  // from 0 to m-1.  Z and z are the vectors themselves
  //
  const ProblemDom: domain(1, int(64)) = [0..m-1];
  var Z, z: [ProblemDom] elemType;

  initVectors(Twiddles, z);            // initialize twiddles and input vector z

  const startTime = getCurrentTime();  // capture the start time

  Z = conjg(z);                        // store the conjugate of z in Z
  bitReverseShuffle(Z);                // permute Z
  dfft(Z, Twiddles);                   // compute the discrete Fourier transform

  const execTime = getCurrentTime() - startTime;     // store the elapsed time

  const validAnswer = verifyResults(z, Z, Twiddles); // validate the answer
  printResults(validAnswer, execTime);               // print the results
}

//
// compute the discrete fast Fourier transform of a vector A declared
// over domain ADom using twiddle vector W
//
def dfft(A: [?ADom], W) {
  const numElements = A.numElements;

  //
  // loop over the phases of the DFT sequentially using custom
  // iterator genDFTStrideSpan that yields the stride and span for
  // each bank of butterfly calculations
  //
  for (str, span) in genDFTStrideSpan(numElements) {
    //
    // loop in parallel over each of the banks of butterflies with
    // shared twiddle factors, zippering with the unbounded range
    // 0.. to get the base twiddle indices
    //
    forall (bankStart, twidIndex) in (ADom by 2*span, 0..) {
      //
      // compute the first set of multipliers for the low bank
      //
      var wk2 = W(twidIndex),
          wk1 = W(2*twidIndex),
          wk3 = (wk1.re - 2 * wk2.im * wk1.im,
                 2 * wk2.im * wk1.re - wk1.im):elemType;

      //
      // loop in parallel over the low bank, computing butterflies
      // Note: lo..#num         == lo, lo+1, lo+2, ..., lo+num-1
      //       lo.. by str #num == lo, lo+str, lo+2*str, ... lo+(num-1)*str
      //
      forall lo in bankStart..#str do
        butterfly(wk1, wk2, wk3, A[lo.. by str #radix]);

      //
      // update the multipliers for the high bank
      //
      wk1 = W(2*twidIndex+1);
      wk3 = (wk1.re - 2 * wk2.re * wk1.im,
             2 * wk2.re * wk1.re - wk1.im):elemType;
      wk2 *= 1.0i;

      //
      // loop in parallel over the high bank, computing butterflies
      //
      forall lo in bankStart+span..#str do
        butterfly(wk1, wk2, wk3, A[lo.. by str #radix]);
    }
  }

  //
  // Do the last set of butterflies...
  //
  const str = radix**log4(numElements-1);
  //
  // ...using the radix-4 butterflies with 1.0 multipliers if the
  // problem size is a power of 4
  //
  if (str*radix == numElements) then
    forall lo in 0..#str do
      butterfly(1.0, 1.0, 1.0, A[lo.. by str #radix]);
  //
  // ...otherwise using a simple radix-2 butterfly scheme
  //
  else
    forall lo in 0..#str {
      const a = A(lo),
            b = A(lo+str);
      A(lo)     = a + b;
      A(lo+str) = a - b;
    }
}

//
// this is the radix-4 butterfly routine that takes multipliers wk1,
// wk2, and wk3 and a 4-element array (slice) A.
//
def butterfly(wk1, wk2, wk3, A) {
  var X: [0..#radix] elemType = A;  // make a local copy of A on this locale
  var x0 = X(0) + X(1),
      x1 = X(0) - X(1),
      x2 = X(2) + X(3),
      x3rot = (X(2) - X(3))*1.0i;

  X(0) = x0 + x2;                   // compute the butterfly in-place on X
  x0 -= x2;
  X(2) = wk2 * x0;
  x0 = x1 + x3rot;
  X(1) = wk1 * x0;
  x0 = x1 - x3rot;
  X(3) = wk3 * x0;

  A = X;                            // copy the result back into A
}

//
// this iterator generates the stride and span values for the phases
// of the DFFT simply by yielding tuples: (radix**i, radix**(i+1))
//
def genDFTStrideSpan(numElements) {
  var stride = 1;
  for 1..log4(numElements-1) {
    const span = stride * radix;
    yield (stride, span);
    stride = span;
  }
}

//
// Print the problem size
//
def printConfiguration() {
  if (printParams) {
    if (printStats) then printLocalesTasks(tasksPerLocale=1);
    printProblemSize(elemType, numVectors, m);
  }
}

//
// Initialize the twiddle vector and random input vector and
// optionally print them to the console
//
def initVectors(Twiddles, z) {
  computeTwiddles(Twiddles);
  bitReverseShuffle(Twiddles);

  fillRandom(z, seed);

  if (printArrays) {
    writeln("After initialization, Twiddles is: ", Twiddles, "\n");
    writeln("z is: ", z, "\n");
  }
}

//
// Compute the twiddle vector values
//
def computeTwiddles(Twiddles) {
  const numTwdls = Twiddles.numElements,
        delta = 2.0 * atan(1.0) / numTwdls;

  Twiddles(0) = 1.0;
  Twiddles(numTwdls/2) = let x = cos(delta * numTwdls/2)
                          in (x, x): elemType;
  forall i in 1..numTwdls/2-1 {
    const x = cos(delta*i),
          y = sin(delta*i);
    Twiddles(i)            = (x, y): elemType;
    Twiddles(numTwdls - i) = (y, x): elemType;
  }
}

//
// Perform a permutation of the argument vector by reversing the bits
// of the indices
//
def bitReverseShuffle(Vect: [?Dom]) {
  const numBits = log2(Vect.numElements),
        Perm: [Dom] Vect.eltType = [i in Dom] Vect(bitReverse(i, revBits=numBits));
  Vect = Perm;
}

//
// Reverse the low revBits bits of val
//
def bitReverse(val: ?valType, revBits = 64) {
  param mask = 0x0102040810204080;
  const valReverse64 = bitMatMultOr(mask, bitMatMultOr(val:uint(64), mask)),
        valReverse = bitRotLeft(valReverse64, revBits);
  return valReverse: valType;
}

//
// Compute the log base 4 of x
//
def log4(x) return logBasePow2(x, 2);  

//
// verify that the results are correct by reapplying the dfft and then
// calculating the maximum error, comparing against epsilon
//
def verifyResults(z, Z, Twiddles) {
  if (printArrays) then writeln("After FFT, Z is: ", Z, "\n");

  Z = conjg(Z) / m;
  bitReverseShuffle(Z);
  dfft(Z, Twiddles);

  if (printArrays) then writeln("After inverse FFT, Z is: ", Z, "\n");

  var maxerr = max reduce sqrt((z.re - Z.re)**2 + (z.im - Z.im)**2);
  maxerr /= (epsilon * n);
  if (printStats) then writeln("error = ", maxerr);

  return (maxerr < threshold);
}

//
// print out sucess/failure, the timing, and the Gflop/s value
//
def printResults(successful, execTime) {
  writeln("Validation: ", if successful then "SUCCESS" else "FAILURE");
  if (printStats) {
    writeln("Execution time = ", execTime);
    writeln("Performance (Gflop/s) = ", 5 * (m * n / execTime) * 1e-9);
  }
}
