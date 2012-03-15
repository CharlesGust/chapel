use SysBasic;

// here's what we need from Sys
extern proc sys_strerror_str(error:syserr, inout err_in_strerror:syserr):string;
// here's what we need from QIO
extern proc qio_quote_string_chpl(ptr:string, len:ssize_t):string;

proc ioerror(error:syserr, msg:string)
{
  var errstr:string;
  var strerror_err:syserr = ENOERR;
  errstr = sys_strerror_str(error, strerror_err); 
  __primitive("chpl_error", errstr + " " + msg);
}

proc ioerror(error:syserr, msg:string, path:string)
{
  if( error ) {
    var errstr:string;
    var quotedpath:string;
    var strerror_err:syserr = ENOERR;
    errstr = sys_strerror_str(error, strerror_err); 
    quotedpath = qio_quote_string_chpl(path, path.length);
    __primitive("chpl_error", errstr + " " + msg + " with path " + quotedpath);
  }
}

proc ioerror(error:syserr, msg:string, path:string, offset:int(64))
{
  var errstr:string;
  var quotedpath:string;
  var strerror_err:syserr = ENOERR;
  errstr = sys_strerror_str(error, strerror_err); 
  quotedpath = qio_quote_string_chpl(path, path.length);
  __primitive("chpl_error", errstr + " " + msg + " with path " + quotedpath + " offset " + offset:string);
}


proc errorToString(error:syserr):string
{
  var errstr:string = "unknown";
  var strerror_err:syserr = ENOERR;
  errstr = sys_strerror_str(error, strerror_err); 
  return errstr;
}
