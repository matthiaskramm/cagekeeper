#ifndef __language_interpreter_h__
#define  __language_interpreter_h__

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include "util.h"
#include "function.h"

typedef struct _language {
#ifdef DEBUG
#define LANG_MAGIC 0xa2b4c6d8
    int32_t magic;
#endif
    void*internal;
    void*user;
    const char*name;
    int verbosity;

    bool (*define_constant)(struct _language*li, const char*name, value_t*value);

    bool (*compile_script) (struct _language*li, const char*script);
    bool (*is_function) (struct _language*li, const char*name);

    value_t* (*call_function) (struct _language*li, const char*name, value_t*args);

    void (*destroy)(struct _language*li);

    FILE* error_file;
    const char*error;
} language_t;

int call_int_function(language_t* li, const char*name);
bool define_int_constant(language_t* li, const char*name, int value);
bool define_string_constant(language_t* li, const char*name, const char* value);

language_t* javascript_interpreter_new(function_def_t*methods);
language_t* lua_interpreter_new(function_def_t*methods);
language_t* python_interpreter_new(function_def_t*methods);
language_t* ruby_interpreter_new(function_def_t*methods);
language_t* interpreter_by_extension(const char*filename, function_def_t* functions);

void language_error(language_t*l, const char*error, ...);
#define language_log language_error

value_t* call_function_with_timeout(language_t*l, const char*function, value_t*args, int max_seconds, bool*timeout);
value_t* compile_and_run_function_with_timeout(language_t*l, const char*script, const char*function, value_t*args, int max_seconds, bool*timeout);

#endif //__language_interpreter_h__
