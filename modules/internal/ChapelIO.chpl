// class file
//
//  chapel-level implementations of read, write, writeln
//  chapel-level implementations of assert, halt

enum FileAccessMode { read, write };

//
// functions on _file primitive type, the C file pointer type
//
pragma "inline" def =(a: _file, b: _file) return b;
pragma "inline" def ==(a: _file, b: _file) return __primitive("==", a, b);
pragma "inline" def !=(a: _file, b: _file) return __primitive("!=", a, b);

pragma "inline" def _readLitChar(fp: _file, val: string, ignoreWhiteSpace: bool)
  return __primitive("_fscan_literal", fp, val, ignoreWhiteSpace);

const stdin  = new file("stdin", FileAccessMode.read, "/dev", __primitive("get_stdin"));
const stdout = new file("stdout", FileAccessMode.write, "/dev", __primitive("get_stdout"));
const stderr = new file("stderr", FileAccessMode.write, "/dev", __primitive("get_stderr"));

class file: Writer {
  var filename : string = "";
  var mode : FileAccessMode = FileAccessMode.read;
  var path : string = ".";
  var _fp : _file;
  var _lock : sync uint(64);    // for serializing output

  def open() {
    on this {
      if this == stdin || this == stdout || this == stderr then
        halt("***Error: It is not necessary to open \"", filename, "\"***");

      var fullFilename = path + "/" + filename;

      var modestring: string;
      select mode {
        when FileAccessMode.read  do modestring = "r";
        when FileAccessMode.write do modestring = "w";
      }
      _fp = __primitive("fopen", filename, modestring);

      if _fp == __primitive("get_nullfile") {
        const err = __primitive("get_errno");
        halt("***Error: Unable to open \"", fullFilename, "\": ", err, "***");
      }
    }
  }

  def _checkFileStateChangeLegality(state) {
    if (isOpen) {
      halt("Cannot change ", state, " of file ", path, "/", filename, 
           " while it is open");
    }
  }

  def filename var : string {
    if setter then
      _checkFileStateChangeLegality("filename");
    return filename;
  }

  def path var : string {
    if setter then
      _checkFileStateChangeLegality("path");
    return path;
  }

  def mode var {
    if setter then
      _checkFileStateChangeLegality("mode");
    return mode;
  }

  def isOpen: bool {
    var openStatus: bool = false;
    //    if atomicSupport {
      if (_fp != __primitive("get_nullfile")) {
	openStatus = true;
      }
      //    } else {
      //      openStatus = true;
      //    }
    return openStatus;
  }
  
  def close() {
    on this {
      if (this == stdin || this == stdout || this == stderr) {
        halt("***Error: You may not close \"", filename, "\"***");
      }
      if (_fp == __primitive("get_nullfile")) {
        var fullFilename = path + "/" + filename;
        halt("***Error: Trying to close \"", fullFilename, 
             "\" which isn't open***");
      }
      var returnVal: int = __primitive("fclose", _fp);
      if (returnVal < 0) {
        var fullFilename = path + "/" + filename;
        const err = __primitive("get_errno");
        halt("***Error: The close of \"", fullFilename, "\" failed: ", err, 
             "***");
      }
      _fp = __primitive("get_nullfile");
    }
  }
}

def file.writeThis(f: Writer) {
  f.write("(filename = ",this.filename);
  f.write(", path = ",this.path);
  f.write(", mode = ",this.mode);
  f.write(")");
}

def file.flush() {
  on this do __primitive("fflush", _fp);
}

def _checkOpen(f: file, isRead: bool) {
  if !f.isOpen {
    var fullFilename:string = f.path + "/" + f.filename;
    if isRead {
      halt("***Error: You must open \"", fullFilename, 
           "\" before trying to read from it***");
    } else {
      halt("***Error: You must open \"", fullFilename, 
           "\" before trying to write to it***");
    }
  }
}

def file.readln() {
  on this {
    if !isOpen then
      _checkOpen(this, isRead=true);
    __primitive("_readToEndOfLine",_fp);
  }
}

def file.readln(inout list ...?n) {
  read((...list));
  on this {
    __primitive("_readToEndOfLine",_fp);
  }
} 

def file.readln(type t) {
  var val: t;
  this.readln(val);
  return val;
}

def file.read(inout first, inout rest ...?n) {
  read(first);
  for param i in 1..n do
    read(rest(i));
}

def file.read(inout val: int) {
  if !isOpen then
    _checkOpen(this, isRead=true);
  var x: int;
  on this {
    x = __primitive("_fscan_int32", _fp);
  }
  val = x;
}

def file.read(inout val: uint) {
  if !isOpen then
    _checkOpen(this, isRead=true);
  var x: uint;
  on this {
    x = __primitive("_fscan_uint32", _fp);
  }
  val = x;
}

def file.read(inout val: real) {
  if !isOpen then
    _checkOpen(this, isRead=true);
  var x: real;
  on this {
    x = __primitive("_fscan_real64", _fp);
  }
  val = x;
}

def file.read(inout val: complex) {
  var realPart: real;
  var imagPart: real;
  var imagI: string;
  var matchingCharWasRead: int;
  var isNeg: bool;

  read(realPart);
  on this {
    matchingCharWasRead = _readLitChar(_fp, "+", true);
  }
  if (matchingCharWasRead != 1) {
    on this {
      matchingCharWasRead = _readLitChar(_fp, "-", true);
    }
    if (matchingCharWasRead != 1) {
      halt("***Error: Incorrect format for complex numbers***");
    }
    isNeg = true;
  }

  read(imagPart);
  on this {
    matchingCharWasRead = _readLitChar(_fp, "i", false);
  }
  if (matchingCharWasRead != 1) {
    halt("***Error: Incorrect format for complex numbers***");
  }

  val.re = realPart;
  if (isNeg) {
    val.im = -imagPart;
  } else {
    val.im = imagPart;
  }
}

def file.read(inout val: string) {
  if !isOpen then
    _checkOpen(this, isRead=true);
  var x: string;
  on this {
    x = __primitive("_fscan_string", _fp);
  }
  val = x;
}

def file.read(inout val: bool) {
  var s: string;
  this.read(s);
  if (s == "true") {
    val = true;
  } else if (s == "false") {
    val = false;
  } else {
    halt("read of bool value that is neither true nor false");
  }
}

def file.read(type t) {
  var val: t;
  this.read(val);
  return val;
}

def string.writeThis(f: Writer) {
  f.writeIt(this);
}

def numeric.writeThis(f: Writer) {
  f.writeIt(this:string);
}

def enumerated.writeThis(f: Writer) {
  f.writeIt(this:string);
}

def bool.writeThis(f: Writer) {
  f.writeIt(this:string);
}

def chpl_taskID_t.writeThis(f: Writer) {
  var tmp : uint(64) = this : uint(64);
  f.writeIt(tmp : string);
}

def file.writeIt(s: string) {
  //  if atomicSupport {
    if !isOpen then
      _checkOpen(this, isRead = false);
    if mode != FileAccessMode.write then
      halt("***Error: ", path, "/", filename, " not open for writing***");
    var status = __primitive("fprintf", _fp, "%s", s);
    if status < 0 {
      const err = __primitive("get_errno");
      halt("***Error: Write failed: ", err, "***");
    }
    //  }
}

class StringClass: Writer {
  var s: string;
  def writeIt(s: string) { this.s += s; }
}

pragma "ref this" pragma "dont disable remote value forwarding"
def string.write(args ...?n) {
  var sc = new StringClass(this);
  sc.write((...args));
  this = sc.s;
  delete sc;
}

def file.lockWrite() {
  var me: uint(64) = __primitive("task_id") : uint(64);
  if _lock.isFull then
    if _lock.readXX() == me then
      return false;
  _lock = me;
  return true;
}

def file.unlockWrite() {
  _lock.reset();
}

class Writer {
  def writeIt(s: string) { }
  def lockWrite() return false;
  def unlockWrite() { }
  def write(args ...?n) {
    def isNilObject(val) {
      def helper(o: object) return o == nil;
      def helper(o)         return false;
      return helper(val);
    }

    on this {
      var need_release: bool;
      need_release = lockWrite();
      for param i in 1..n do
        if isNilObject(args(i)) then
          "nil".writeThis(this);
        else
          args(i).writeThis(this);
      if need_release then
        unlockWrite();
    }
  }
  def writeln(args ...?n) {
    write((...args), "\n");
  }
  def writeln() {
    write("\n");
  }
}

def write(args ...?n) {
  stdout.write((...args));
  stdout.flush();
}

def writeln(args ...?n) {
  stdout.writeln((...args));
  stdout.flush();
}

def writeln() {
  stdout.writeln();
  stdout.flush();
}

def read(inout args ...?n) {
  stdin.read((...args));
}

def readln(inout args ...?n) {
  stdin.readln((...args));
}

def readln() {
  stdin.readln();
}

def readln(type t) {
  return stdin.readln(t);
}

def read(type t)
  return stdin.read(t);

def file.readln(type t ...?numTypes) where numTypes > 1 {
  var tupleVal: t;
  for param i in 1..numTypes-1 do
    tupleVal(i) = this.read(t(i));
  tupleVal(numTypes) = this.readln(t(numTypes));
  return tupleVal;
}

def file.read(type t ...?numTypes) where numTypes > 1 {
  var tupleVal: t;
  for param i in 1..numTypes do
    tupleVal(i) = this.read(t(i));
  return tupleVal;
}

def readln(type t ...?numTypes) where numTypes > 1
  return stdin.readln((...t));

def read(type t ...?numTypes) where numTypes > 1
  return stdin.read((...t));
  
def _tuple2string(t) {
  var s: string;
  for param i in 1..t.size do
    s.write(t(i));
  return s;
}

def assert(test: bool) {
  if !test then
    __primitive("chpl_error", "assert failed");
}

def assert(test: bool, args ...?numArgs) {
  if !test then
    __primitive("chpl_error", "assert failed - "+_tuple2string(args));
}

def halt() {
  __primitive("chpl_error", "halt reached");
}

def halt(args ...?numArgs) {
  __primitive("chpl_error", "halt reached - "+_tuple2string(args));
}

def _debugWrite(args...?n) {
  def getString(a: ?t) {
    if t == bool(8) || t == bool(16) || t == bool(32) || t == bool(64) ||
       t == int(8) || t == int(16) || t == int(32) || t == int(64) ||
       t == uint(8) || t == uint(16) || t == uint(32) || t == uint(64) ||
       t == real(32) || t == real(64) || t == imag(32) || t == imag(64) ||
       t == complex(64) || t == complex(128) ||
       t == bool || t == string || __primitive("isEnumType", t) then
      return a:string;
    else 
      compilerError("Cannot call _debugWrite on value of type ",
                    typeToString(t));
  }
  for param i in 1..n {
    var status = __primitive("fprintf", __primitive("get_stdout"), "%s", getString(args(i)));
    if status < 0 {
      const err = __primitive("get_errno");
      halt("_debugWrite failed with status ", err);
    }
  }
  __primitive("fflush", __primitive("get_stdout"));
}

def _debugWriteln(args...?n) {
  _debugWrite((...args), "\n");
}

def _debugWriteln() {
  _debugWrite("\n");
}

def _ddata.writeThis(f: Writer) {
  halt("cannot write the _ddata class");
}

def format(fmt: string, x:?t) where _isIntegralType(t) || _isFloatType(t) {
  if fmt.substring(1) == "#" {
    var fmt2 = _getoutputformat(fmt);
    if _isImagType(t) then
      return (__primitive("_format", fmt2, _i2r(x))+"i");
    else
      return __primitive("_format", fmt2, x:real);
  } else 
    return __primitive("_format", fmt, x);
}

def format(fmt: string, x:?t) where _isComplexType(t) {
  if fmt.substring(1) == "#" {
    var fmt2 = _getoutputformat(fmt);
    return (__primitive("_format", fmt2, x.re)+" + "+ __primitive("_format", fmt2, x.im)+"i");
  } else 
    return __primitive("_format", fmt, x);
}

def format(fmt: string, x: ?t) {
  return __primitive("_format", fmt, x);
}

def _getoutputformat(s: string):string {
  var sn = length(s);
  var afterdot = false;
  var dplaces = 0;
  for i in 1..sn {
    if ((s.substring(i) == '#') & afterdot) then dplaces += 1;
    if (s.substring(i) == '.') then afterdot=true;
  }

  return("%" + sn + "." + dplaces + "f");
}

//
// When this flag is used during compilation, calls to chpl__testPar
// will output a message to indicate that a portion of the code has been
// parallelized.
//
config param chpl__testParFlag = false;
var chpl__testParOn = false;

def chpl__testParStart() {
  chpl__testParOn = true;
}

def chpl__testParStop() {
  chpl__testParOn = false;
}

pragma "inline"
def chpl__testPar(args...) where chpl__testParFlag == false { }

def chpl__testPar(args...) where chpl__testParFlag == true {
  if chpl__testParFlag && chpl__testParOn {
    const file : string = __primitive("_get_user_file");
    const line : int = __primitive("_get_user_line");
    writeln("CHPL TEST PAR (", file, ":", line, "): ", (...args));
  }
}
