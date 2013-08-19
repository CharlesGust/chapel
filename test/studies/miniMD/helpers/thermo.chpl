
use miniMD;
use initMD;
use MDForce;
use Time;

var mstat : int; // stores information about iterations
var t_act, p_act, e_act : real; // 
var t_scale, p_scale, e_scale, mvv2e, dof_boltz : real;

// holds data regarding temperature, energy, pressure
// later used in more verbose output (unimplemented)
var stepSpace : domain(1);
var steps : [stepSpace] int;
var temps : [stepSpace] real;
var engs : [stepSpace] real;
var prs : [stepSpace] real;

proc initThermo() {
	var maxstat : int;
	if nstat == 0 then maxstat = 2;
	else maxstat = nsteps / nstat + 2;
	stepSpace = {1..maxstat};

	// from the c++ version, looks like scientific constants
	if units == "lj" {
		mvv2e = 1.0;
		dof_boltz = natoms * 3 - 3;
		t_scale = mvv2e / dof_boltz;
		p_scale = 1.0 / 3 / dim.x / dim.y / dim.z;
		e_scale = 0.5;
	} else {
		mvv2e = 1.036427e-4;
		dof_boltz = (natoms * 3 - 3) * 8.617343e-5;
		t_scale = mvv2e / dof_boltz;
		p_scale = 1.602176e6 / 3 / dim.x / dim.y / dim.z;
		e_scale = 524287.985533;
		dtforce /= mvv2e;
	} 
}

proc compute( flag : int, f : Force, total : Timer) {
	var t, eng, p : real;
	if flag > 0 && (flag % nstat >= 1) then return;
	if flag == -1 && nstat > 0 && (nsteps % nstat == 0) then return;

	t_act = 0;
	e_act = 0;
	p_act = 0;

	// calculate
	t = temperature();
	eng = energy(f);
	p = pressure(t, f);

	var istep = flag;
	if flag == -1 then istep = nsteps;
	if flag == 0 then mstat = 1;

	// store data
	steps[mstat] = istep;
	temps[mstat] = t;
	engs[mstat] = eng;
	prs[mstat] = p;

	mstat += 1;

	var tval : real = 0.0;
	if istep != 0 then tval = total.elapsed();
		if printOriginal then writef("%i %er %er %er %.6dr\n", istep, t, eng, p, tval);
		else if printCorrect then  writef("%i %er %er %er\n", istep, t, eng, p);
}

proc energy(f : Force) {
	e_act = f.eng_vdwl;

	if half_neigh then e_act *= 2.0;

	e_act *= e_scale;

	return e_act / natoms;
}

proc temperature() {
	t_act = 0;
	t_act = + reduce forall (b,c) in zip(bins[realStencil],binCount[realStencil]) do + reduce forall a in b[1..c] do (a.v.dot(a.v) * mass);

	return t_act * t_scale;
}

proc pressure(t : real, f : Force) {
	return (t * dof_boltz + f.virial) * p_scale;
}
