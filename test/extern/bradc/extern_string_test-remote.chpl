_extern proc return_string_test():string;
_extern proc return_string_arg_test(inout string);

writeln("returned string ",return_string_test());
var s:string;
on Locales(1) do
  return_string_arg_test(s);
writeln("returned string arg ",s);

