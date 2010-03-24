/*
 * The Computer Language Benchmarks Game
 * http://shootout.alioth.debian.org/
 *
 * Original C contributed by Sebastien Loisel
 * Conversion to Chapel by Albert Sidelnik
 */

use Time, BlockDist;
config const timer : bool = false;
var t : Timer;
config const NUM = 500 : int(64);


/* Return: 1.0 / (i + j) * (i + j +1) / 2 + i + 1; */
def eval_A(i,j) : real
{
	/*
	 * 1.0 / (i + j) * (i + j +1) / 2 + i + 1;
	 * n * (n+1) is even number. Therefore, just (>> 1) for (/2)
	 */
	var d = (((i + j) * (i + j + 1)) >> 1) + i + 1;
	return 1.0 / d;
}

def eval_A_times_u(U : [] real, inRange, Au : [] real)
{
	forall (au,i) in (Au, [0..#inRange]) do 
		au = + reduce [j in 0..#inRange] (U(j) * eval_A(i,j));

}

def eval_At_times_u(U : [] real, inRange, Au : [] real)
{
	forall (au,i) in (Au, [0..#inRange]) do
		au = + reduce [j in 0..#inRange] (U(j) * eval_A(j,i));
}

def eval_AtA_times_u(u,AtAu,v : [] real, inRange)
{
	   eval_A_times_u(u, inRange, v);
	   eval_At_times_u(v, inRange, AtAu);
}

def spectral_game(N) : real
{
	var Dist = new dist(new Block(rank=1, idxType=int(64), boundingBox=[0..#N],
                                      maxDataParallelism=here.numCores));
	var Dom : domain(1, int(64)) distributed Dist = [0..#N];

	var tmp, U, V : [Dom] real;

	U = 1.0;

	for 1..10 do {
		eval_AtA_times_u(U,V,tmp,N);
		eval_AtA_times_u(V,U,tmp,N);
	}

	var vv = + reduce [v in V] (v * v);
	var vBv = + reduce [(u,v) in (U,V)] (u * v);

	return sqrt(vBv/vv);
}

def main() {
	if timer then
		t.start();
	writeln(spectral_game(NUM));
	if timer then {
		t.stop();
		writeln("Time elapsed : ", t.elapsed(), " seconds");
	}
}
