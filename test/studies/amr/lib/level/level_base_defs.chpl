//===> Description ===>
//
// Definitions for the BaseLevel class.
//
//<=== Description <===

use grid_base_defs;



//=======================================================================>
//==================== FUNDAMENTAL LEVEL DEFINITIONS  ===================>
//=======================================================================>


//===> BaseLevel class ===>
//========================>
class BaseLevel {

  var fixed: bool = false;
  
  var x_low, x_high:       dimension*real,
      n_cells:             dimension*int,
      n_child_ghost_cells: dimension*int,
      dx:                  dimension*real;


  //==== Level cell domains ====
  //--------------------------------------------------------------------
  // No plans to iterate over these, but they're nice for the subdomain
  // declarations below.
  //--------------------------------------------------------------------
  var ext_cells: domain(dimension, stridable=true);
  var cells:     subdomain(ext_cells);


  //==== Child grid info ====
  //---------------------------------------------------------------------
  // For each child grid, the level stores its list of neighbors, the
  // ghost cells it shares with its neighbors (one arithmetic domain per
  // neighbor) and a sparse list of the ghost cells which belong to the
  // physical boundary.
  //---------------------------------------------------------------------
  var child_grids:     domain(BaseGrid);
  var neighbors:       [child_grids] BaseGrid;

  var shared_ghosts:   [child_grids] [child_grids] subdomain(cells);
  var boundary_ghosts: [child_grids] sparse subdomain(ext_cells);

  



  //===> initialize method ===>
  //==========================>
  //--------------------------------------------------------------------
  // Sets dx and cell domains, based on the mandatory input parameters.
  //--------------------------------------------------------------------
  def initialize() {
    dx = (x_high - x_low) / n_cells;


    //==== Set cell domains ====
    var range_tuple: dimension*range(stridable = true);
    for d in dimensions do
      range_tuple(d) = 1 .. #2*n_cells(d) by 2;
    cells = range_tuple;

    var size: dimension*int;
    for d in dimensions do
      size(d) = 2*n_child_ghost_cells(d);
    ext_cells = cells.expand(size);

  }
  //<=== initialize method <===
  //<==========================

  

  //===> snapToVertex method ===>
  //============================>
  //------------------------------------------------------
  // Provided a real coordinate, returns the index of the
  // nearest vertex.  Useful when defining a grid by
  // its real bounds.
  //------------------------------------------------------
  def snapToVertex(x: dimension*real) {

    var idx: dimension*int;

    for d in dimensions do
      idx(d) = 2 * round((x(d) - x_low(d)) / dx(d)) : int;

    return idx;

  }
  //<=== snapToVertex method <===
  //<============================

  

}
//<=== BaseLevel class <===
//<========================





//===> BaseLevel.addGrid method ===>
//=================================>
//--------------------------------------------------------
// This version is based on indices, and probably best to
// use in practice, as integer arithmetic is cleaner than
// real arithmetic.
//--------------------------------------------------------
def BaseLevel.addGrid(
  i_low_grid:  dimension*int,
  i_high_grid: dimension*int)
{

  //==== Safety check ====  
  assert(fixed == false);


  //==== Derive grid fields from index bounds and parent (this) info ====
  var x_low_grid, x_high_grid: dimension*real;
  var n_cells_grid:            dimension*int;

  for d in dimensions {
    x_low_grid(d)   = x_low(d) + i_low_grid(d)  * dx(d)/2.0;
    x_high_grid(d)  = x_low(d) + i_high_grid(d) * dx(d)/2.0;
    n_cells_grid(d) = (i_high_grid(d) - i_low_grid(d)) / 2;
  }


  //==== Create and add new grid ====
  var new_grid = new BaseGrid(x_low         = x_low_grid,
			      x_high        = x_high_grid,
			      i_low         = i_low_grid,
			      n_cells       = n_cells_grid,
			      n_ghost_cells = n_child_ghost_cells);

  child_grids.add(new_grid);
}


//----------------------------------------------------------
// This version takes in real bounds, and snaps them to the
// level's discretization.
//----------------------------------------------------------
def BaseLevel.addGrid(
  x_low_grid:  dimension*real,
  x_high_grid: dimension*real)
{

  var i_low_grid = snapToVertex(x_low_grid);
  var i_high_grid = snapToVertex(x_high_grid);

  addGrid(i_low_grid, i_high_grid);

}
//<=== BaseLevel.addGrid method <===
//<=================================






//===> BaseLevel.fix method ===>
//=============================>
//----------------------------------------------------------------
// This method is meant to be called after all grids are added to
// the level.  Neighbor data is set on each grid, and other post-
// processing can be added here as needed.
//----------------------------------------------------------------
def BaseLevel.fix() {

  assert(fixed == false);
  fixed = true;

  partitionSharedGhosts();
  partitionBoundaryGhosts();

}
//<=== BaseLevel.fix method <===
//<=============================





//===> BaseLevel.partitionSharedGhosts method ===>
//===============================================>
//----------------------------------------------------------------------
// For each child grid 'grid', locates all neighboring grids.  For each 
// neighbor 'nbr', stores the domain of grid's ghost cells that are
// shared with nbr's interior.
//----------------------------------------------------------------------
def BaseLevel.partitionSharedGhosts() {
  
  coforall grid in child_grids {

    //==== Set shared_ghosts for each sibling ====
    for sib in child_grids do
      shared_ghosts(grid)(sib) = intersectDomains(grid.ext_cells, sib.cells);

    //==== Need to fix grid's intersection with itself ====
    var empty_domain: domain(dimension);
    shared_ghosts(grid)(grid) = empty_domain;

  }
}
//<=== BaseLevel.partitionSharedGhosts method <===
//<===============================================




//===> BaseLevel.partitionBoundaryGhosts method ===>
//=================================================>
//-----------------------------------------------------------------------
// For each child grid, sets up the sparse domain boundary_ghosts(grid).
//-----------------------------------------------------------------------
def BaseLevel.partitionBoundaryGhosts() {

  coforall grid in child_grids {
    
    //==== Initialize all ghost cells as boundary ====
    for cell in grid.ghost_cells do
      boundary_ghosts(grid).add(cell);


    //==== Remove ghost cells that are shared ====
    for sib in child_grids {
      for cell in shared_ghosts(grid)(sib) do
	boundary_ghosts(grid).remove(cell);
    }

  }

}
//<=== BaseLevel.partitionBoundaryGhosts method <===
//<=================================================





//===> LevelBoundaryDomain class ===>
//==================================>
//---------------------------------------------------------------------
// Container class that holds a GridBoundaryDomain for each grid on
// the level.  Not actually used by default for single-level problems,
// but it definitely belongs here in the library.  Needed by AMR
// hierarchies, and may also be useful for certain LevelBCs.
//---------------------------------------------------------------------
class LevelBoundaryDomain {

  const level: BaseLevel;
  var grid_boundary_domains: [level.grids] GridBoundaryDomain;

  def initialize() {
    coforall grid in level.grids do
      grid_boundary_domains(grid) = new GridBoundaryDomain(grid = grid);
  }

  def this(grid: BaseGrid) {
    return grid_boundary_domains(grid);
  }

}
//<=== LevelBoundaryDomain class <===
//<==================================