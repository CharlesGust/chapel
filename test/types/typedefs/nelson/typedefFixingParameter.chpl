
record X {
  param p = 0;
  proc initialize() { 
    writeln("X has p=", p); 
  }
}


// // this works
// proc main() {
//  type t = X;
//  var y : t;
//}

// but this doesn't 
type t = X;
var y : t;
