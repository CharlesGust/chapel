#ifndef CHPL_FLAGS_H_
#define CHPL_FLAGS_H_


//
// HOW TO ADD A FLAG: Add an entry (sorted alphabetically) to the
// enumeration Flag in this file and add a call to defineFlag in
// initFlags() in flags.cpp.  Then to add the flag to a symbol, use
// Symbol::addFlag(Flag) and, to check whether a symbol has a flag,
// use Symbol::hasFlag(Flag).  Flags can be added to Chapel code using
// the pragma production as in
//
//   pragma "my pragma" def FunctionName
//   pragma "my pragma" var VariableName
//   pragma "my pragma" class ClassName
//
// HOW TO VIEW A FLAG IN GDB: Use the command "flags ast" where ast is
// a Symbol.  If you just know the id of the ast, use the command
// "flags aid(id)" where id is the integral symbol id.
//


#include "chpl.h"


class BaseAST;


enum Flag {
  FLAG_UNKNOWN = 0,
  FLAG_ALLOW_REF,
  FLAG_ARRAY,
  FLAG_ARRAY_ALIAS, // array alias declared via => syntax
  FLAG_HAS_RUNTIME_TYPE, // type that has an associated runtime type
  FLAG_RUNTIME_TYPE_VALUE, // associated runtime type (value)
  FLAG_AUTO_II,
  FLAG_BASE_ARRAY,
  FLAG_BASE_DIST,
  FLAG_BEGIN,
  FLAG_BEGIN_BLOCK,
  FLAG_CALLS_CONSTRUCTOR, // for functions that return constructor return values
  FLAG_CCACHE, // constant cache memory for GPU
  FLAG_COBEGIN_OR_COFORALL,
  FLAG_COBEGIN_OR_COFORALL_BLOCK,
  FLAG_COMMAND_LINE_SETTING,
  FLAG_COMPILER_NESTED_FUNCTION,
  FLAG_CONCURRENTLY_ACCESSED, // local variables accessed by multiple threads
  FLAG_CONFIG, // config variable, constant, or parameter
  FLAG_CONST, // constant
  FLAG_CONST_MEM_SIZE,
  FLAG_DATA_CLASS,
  FLAG_DATA_SET_ERROR,
  FLAG_DEFAULT_CONSTRUCTOR,
  FLAG_DESTRUCTOR,
  FLAG_DISTRIBUTION,
  FLAG_DOMAIN,
  FLAG_DONT_DISABLE_REMOTE_VALUE_FORWARDING,
  FLAG_EXPAND_TUPLES_WITH_VALUES,
  FLAG_EXPLICIT_GPU_ARRAY, // maker to designate gpu data
  FLAG_EXPORT,
  FLAG_EXPR_TEMP, // temporary that stores the result of an expression
  FLAG_EXTERN, // extern variables, types, and functions
  FLAG_FIXED_STRING, // fixed-length strength
  FLAG_FUNCTION_CLASS, // first-class function class representation
  FLAG_GENERIC, // generic types and functions
  FLAG_GEN_CC, // marker to generate constant cache mem
  FLAG_GEN_SHARED, // marker to generate shared decl
  FLAG_HEAP,
  FLAG_HEAP_ALLOCATE,
  FLAG_STAR_TUPLE, // mark tuple types as star tuple types
  FLAG_IMPLICIT_ALIAS_FIELD,
  FLAG_IMPLICIT_GPU_ARRAY, // maker to designate implicit gpu data
  FLAG_INDEX_VAR,
  FLAG_INLINE,
  FLAG_INLINE_ITERATOR, // iterators that are always inlined, e.g., leaders
  FLAG_INVISIBLE_FN, // invisible function (not a candidate for resolution)
  FLAG_INSERT_AUTO_COPY,
  FLAG_INSERT_AUTO_DESTROY,
  FLAG_INSERT_AUTO_DESTROY_FOR_EXPLICIT_NEW,
  FLAG_INSERT_LINE_FILE_INFO,
  FLAG_IS_MEME,
  FLAG_ITERATOR_CLASS,
  FLAG_ITERATOR_FN,
  FLAG_ITERATOR_RECORD,
  FLAG_LABEL_BREAK,
  FLAG_LABEL_CONTINUE,
  FLAG_LOOP_BODY_ARGUMENT_CLASS,
  FLAG_MAYBE_PARAM, // symbol can resolve to a param
  FLAG_MAYBE_TYPE, // symbol can resolve to a type
  FLAG_METHOD, // function that is a method
  FLAG_NO_AUTO_DESTROY,
  FLAG_NO_COPY, // do not apply chpl__initCopy to initialization of a variable
  FLAG_NO_DEFAULT_FUNCTIONS,
  FLAG_NO_FORMAL_TMP,
  FLAG_NO_IMPLICIT_COPY, // function does not require autoCopy/autoDestroy
  FLAG_NO_OBJECT,
  FLAG_NO_PARENS, // function without parentheses
  FLAG_NO_WIDE_CLASS,
  FLAG_NON_BLOCKING, // with FLAG_ON/FLAG_ON_BLOCK, non-blocking on functions
  FLAG_FAST_ON, // with FLAG_ON/FLAG_ON_BLOCK, use fast spawning option (if available)
  FLAG_OBJECT_CLASS,
  FLAG_OMIT_FROM_CONSTRUCTOR,
  FLAG_GPU_ON, // Flag to mark GPU device kernel
  FLAG_GPU_CALL, // Flag to mark caller of GPU kernel
  FLAG_ON,
  FLAG_ON_BLOCK,
  FLAG_PARAM, // parameter (compile-time constant)
  FLAG_PRIVATE,
  FLAG_PRIVATIZED_CLASS, // privatized array or domain class
  FLAG_PROMOTION_WRAPPER,
  FLAG_RANGE,
  FLAG_REDUCE, // mark a function as reduction
  FLAG_REF,
  FLAG_REF_ITERATOR_CLASS,
  FLAG_REF_THIS,
  FLAG_REMOVABLE_AUTO_COPY,
  FLAG_REMOVABLE_AUTO_DESTROY,
  FLAG_SHARED, // shared mem for GPU
  FLAG_SINGLE,
  FLAG_SPECIFIED_RETURN_TYPE,
  FLAG_SUPER_CLASS,
  FLAG_SYNC,
  FLAG_SYNTACTIC_DISTRIBUTION,
  FLAG_TEMP, // compiler-inserted temporary
  FLAG_TUPLE,
  FLAG_TYPE_CONSTRUCTOR,
  FLAG_TYPE_VARIABLE, // contains a type instead of a value
  FLAG_USE_GPU_REDUCTION, // Use specialized implementation
  FLAG_VIRTUAL,
  FLAG_WIDE,
  FLAG_WIDE_CLASS,
  FLAG_WRAPPER, // wrapper function
  FLAG_WRAP_OUT_INTENT, // formal argument for wrapper for out/inout intent
  NUM_FLAGS
};


int str2flag(const char* str);
void initFlags();
void viewFlags(BaseAST* sym);
TypeSymbol* getDataClassType(TypeSymbol* ts);
void setDataClassType(TypeSymbol* ts, TypeSymbol* ets);

#endif
