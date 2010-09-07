//===> Description ===>
//
// Initializes grid_base_defs and creates a method for a CTU
// (corner transport upwind) solver.
//
//<=== Description <===


use grid_solution_defs;
use grid_bc_defs;



//===> BaseGrid.stepCTU method ===>
//================================>
//-------------------------------------------------------------
// The output of the oprator will be stored in q.old, and then
// q.old and q.current will be swapped.
//-------------------------------------------------------------
def BaseGrid.stepCTU(
  sol:      ScalarGridSolution,
  bc:       GridBC,
  velocity: dimension*real,
  dt:       real
){


  //==== Assign names to solution components ====
  var q_current => sol.space_data(2).value;
  var q_new     => sol.space_data(1).value; // overwriting old values
  var t_current = sol.time(2);
  var t_new     = sol.time(2) + dt;


  //==== Fill ghost cells ====
  bc.ghostFill(q_current, t_current);
  

  //-----------------------------------------------------------
  //---- Domain of alignments is [0..1, 0..1, ..., 0..1].
  //---- In each dimension, alignment 1 indicates that it's
  //---- aligned with the cell, and alignment 0 indicates that
  //---- it's upwind.  (What is "it"?)
  //-----------------------------------------------------------
  var alignments: domain(dimension);
  alignments = alignments.expand(1);
  
  //===> Calculate new value ===>
  forall precell in cells {

    //==== 1D/tuple fix ====
    var cell = tuplify(precell);


    q_new(cell) = 0.0;
    var volume_fraction: real;
    var upwind_cell: dimension*int;


    //===> Update for each alignment ===>
    for prealignment in alignments {

      //==== 1D/tuple fix ====
      var alignment = tuplify(prealignment);

        
      volume_fraction = 1.0;
      for d in dimensions {
        //-------------------------------------------------------
        // For each alignment, set the volume fraction and index
        // of the upwind cell. 
        //-------------------------------------------------------
        if alignment(d) == 0 then {
          volume_fraction *= abs(velocity(d))*dt / dx(d);
        if velocity(d) < 0.0 then
          upwind_cell(d) = cell(d)+2;
        else // the case velocity(d)==0 can refer to any cell
          upwind_cell(d) = cell(d)-2;
	      }
	      else {
          volume_fraction *= 1.0 - abs(velocity(d))*dt / dx(d);
          upwind_cell(d) = cell(d);
        }
          
      }

          
      //==== Update cell value ====
      q_new(cell) += volume_fraction * q_current(upwind_cell);
  
    }
    //<=== Update for each alignment <===
    
  }
  //<=== Calculate new value <===
    
  
  
  //==== Update solution structure ====
  sol.time(1) = t_current;
  sol.time(2) = t_new;
  sol.space_data(1) <=> sol.space_data(2);

}
//<================================
//<=== BaseGrid.stepCTU method <===






//===> ZeroInflowAdvectionGridBC class ===>
//========================================>
class ZeroInflowAdvectionGridBC: GridBC {
  
  
  //===> ghostFill method ===>
  //=========================>
  def ghostFill(q: [grid.ext_cells] real, t: real) {
    //==== This type of BC is homogeneous ====
    homogeneousGhostFill(q);
  }
  //<=== ghostFill method <===
  //<=========================
  
  
  //===> homogeneousGhostFill method ===>
  //====================================>
  def homogeneousGhostFill(q: [grid.ext_cells] real) {

    for cell in grid.ghost_cells do
      q(cell) = 0.0;
    
  }
  //<=== homogeneousGhostFill method <===
  //<====================================
  
}
//<=== ZeroInflowAdvectionGridBC class <===
//<========================================