//===> Description ===>
//
// Basic class and method definitions for a rectangular grid of
// generic dimension (though it must be a compile-time constant.)
//
// This version is based on an underlying mesh of width dx/2, which
// contains all cell centers, interface centers, and vertices.  As
// a result, the domain of cell centers is strided by 2, corresponding
// to jumps of dx.
//
//<=== Description <===


config param dimension = 2;
const dimensions = [1..dimension];

enum line_position {low=-1, interior=0, high=1};
const ghost_locations = (line_position.low..line_position.high)**dimension;




//|""""""""""""""""""""""""\
//|===> GhostCells class ===>
//|________________________/
class GhostCells {

  const grid:    Grid;
  const domains: [ghost_locations] domain(dimension, stridable=true);

  //|-------------------->
  //|===> Constructor ===>
  //|-------------------->
  def GhostCells(grid: Grid) {

    this.grid = grid;

    var ranges: dimension*range(stridable=true);
    var interior_location: dimension*int;
    for d in dimensions do interior_location(d) = line_position.interior;

    for g_loc in ghost_locations {
      if g_loc != interior_location {
	for d in dimensions do ranges(d) = setRange(d,g_loc(d));
	domains(g_loc) = ranges;
      }
    }

  }
  //<--------------------|
  //<=== Constructor <===|
  //<--------------------|



  //|------------------------>
  //|===> setRange method ===>
  //|------------------------>
  //-----------------------------------------------------------------
  // Given a dimension and position, sets the corresponding range of
  // ghost cells.
  //-----------------------------------------------------------------
  def setRange(d: int, p: int) {
    var R: range(stridable=true);

    if p == line_position.low then
      R = (grid.ext_cells.low(d).. by 2) #grid.n_ghost_cells(d);
    else if p == line_position.interior then
      R = grid.cells.dim(d);
    else
      R = (..grid.ext_cells.high(d) by 2) #grid.n_ghost_cells(d);

    return R;
  }
  //<------------------------|
  //<=== setRange method <===|
  //<------------------------|



  def this(loc: dimension*int) {
    return domains(loc);
  }


}
// /""""""""""""""""""""""""|
//<=== GhostCells class <===|
// \________________________|







//|""""""""""""""""""\
//|===> Grid class ===>
//|__________________/
class Grid {
  
  const x_low, x_high:          dimension*real;
  const i_low, i_high:          dimension*int;
  const n_cells, n_ghost_cells: dimension*int;

  const dx: dimension*real;
          
  const ext_cells: domain(dimension, stridable=true);
  const cells:     subdomain(ext_cells);
  
  const ghost_cells: GhostCells;


  //|-------------------->
  //|===> Constructor ===>
  //|-------------------->
  def Grid(
    x_low:         dimension*real,
    x_high:        dimension*real,
    i_low:         dimension*int,
    n_cells:       dimension*int,
    n_ghost_cells: dimension*int)
  {

    //==== Assign inputs to fields ====
    this.x_low         = x_low;
    this.x_high        = x_high;
    this.i_low         = i_low;
    this.n_cells       = n_cells;
    this.n_ghost_cells = n_ghost_cells;

    //==== Sanity check ====
    sanityChecks();

    //==== dx ====
    dx = (x_high - x_low) / n_cells;

    //==== i_high ====
    for d in dimensions do
      i_high(d) = i_low(d) + 2*n_cells(d);

    //==== Physical cells ====
    var ranges: dimension*range(stridable = true);
    for d in dimensions do ranges(d) = (i_low(d)+1 .. by 2) #n_cells(d);
    cells = ranges;

    //==== Extended cells (includes ghost cells) ====
    var size: dimension*int;
    for d in dimensions do size(d) = 2*n_ghost_cells(d);
    ext_cells = cells.expand(size);

    //==== Ghost cells ====
    ghost_cells = new GhostCells(this);
  }
  //<--------------------|
  //<=== Constructor <===|
  //<--------------------|



  //|---------------------------->
  //|===> sanityChecks method ===>
  //|---------------------------->
  //--------------------------------------------------------------
  // Performs some basic sanity checks on the constructor inputs.
  //--------------------------------------------------------------
  def sanityChecks() {
    var d_string: string;
    for d in dimensions do {
      d_string = format("%i", d);

      assert(x_low(d) < x_high(d),
	     "error: Grid: x_low(" + d_string + ") must be strictly less than x_high(" + d_string + ").");

      assert(n_cells(d) > 0,
             "error: Grid: n_cells(" + d_string + ") must be positive.");

      assert(n_ghost_cells(d) > 0,
	     "error: Grid: n_ghost_cells(" + d_string + ") must be positive.");
    }
  }
  //<----------------------------|
  //<=== sanityChecks method <===|
  //<----------------------------|



  //|------------------------->
  //|===> writeThis method ===>
  //|------------------------->
  def writeThis(w: Writer) {
    writeln("x_low: ", x_low, ",  x_high: ", x_high);
    write("i_low: ", i_low, ",  i_high: ", i_high);
  }
  //<-------------------------|
  //<=== writeThis method <===|
  //<-------------------------|

}
// /""""""""""""""""""|
//<=== Grid class <===|
// \__________________|







//|--------------------------->
//|===> Grid.xValue method ===>
//|--------------------------->
//----------------------------------------
// Converts indices to coordinate values.
//----------------------------------------
def Grid.xValue (point_index: dimension*int) {

  var coord: dimension*real;

  if dimension == 1 then {
    coord(1) = x_low(1) + (point_index(1) - i_low(1)) * dx(1)/2.0;
  }
  else {
    forall d in dimensions do
      coord(d) = x_low(d) + (point_index(d) - i_low(d)) * dx(d)/2.0;
  }

  return coord;
}
//<---------------------------|
//<=== Grid.xValue method <===|
//<---------------------------|










//|------------------------>
//|===> tuplify routine ===>
//|------------------------>
//-----------------------------------------------------------
// This is used to fix the "1D problem", in that indices of
// a one-dimensional domain are of type int, whereas for all
// other dimensions, they're dimension*int.
//-----------------------------------------------------------
def tuplify(idx) {
  if isTuple(idx) then return idx;
  else return tuple(idx);
}
//<------------------------|
//<=== tuplify routine <===|
//<------------------------|



//|----------------------------*
//|==== range exponentiation ===>
//|----------------------------*
def **(R: range(stridable=?s), param n: int) {
  var ranges: n*R.type;
  for i in [1..n] do ranges(i) = R;

  var D: domain(n,stridable=s) = ranges;
  return D;
}
// *----------------------------|
//<=== range exponentiation <===|
// *----------------------------|









def main {

  var x_low = (0.0,1.0);
  var x_high = (2.0,3.0);
  var i_low = (0,0);
  var n_cells = (20,40);
  var n_ghost_cells = (2,2);

  var grid = new Grid(x_low, x_high, i_low, n_cells, n_ghost_cells);

  writeln(grid);
  writeln("grid.cells = ", grid.cells);
  writeln("grid.ext_cells = ", grid.ext_cells);

  writeln("");
  writeln("Ghost cell domains:");
  for g_loc in ghost_locations do
    writeln( grid.ghost_cells(g_loc) );

  writeln("");
  writeln( grid.ghost_cells.domains.domain );

}