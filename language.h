#ifndef __language_interpreter_h__
#define  __language_interpreter_h__

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include "util.h"
#include "function.h"

typedef struct _language {
    void*internal;
    const char*name;

    bool timeout;

    bool (*initialize)(struct _language*li, size_t maxmem);

    void (*define_constant)(struct _language*li, const char*name, value_t*value);
    void (*define_function)(struct _language*li, const char*name, function_t*f);

    bool (*compile_script) (struct _language*li, const char*script);
    bool (*is_function) (struct _language*li, const char*name);

    value_t* (*call_function) (struct _language*li, const char*name, value_t*args);

    void (*destroy)(struct _language*li);

    FILE* error_file;
    const char*error;
} language_t;

int call_int_function(language_t* li, const char*name);
void define_int_constant(language_t* li, const char*name, int value);
void define_string_constant(language_t* li, const char*name, const char* value);
void define_function(language_t*li, const char*name, void*call, void*context, const char*params, const char*ret);

language_t* javascript_interpreter_new();
language_t* lua_interpreter_new();
language_t* python_interpreter_new();
language_t* ruby_interpreter_new();

language_t* wrap_sandbox(language_t*language);

language_t* interpreter_by_extension(const char*filename);
language_t* unsafe_interpreter_by_extension(const char*filename);

void language_error(language_t*l, const char*error, ...);
#define language_log language_error

value_t* call_function_with_timeout(language_t*l, const char*function, value_t*args, int max_seconds, bool*timeout);
value_t* compile_and_run_function_with_timeout(language_t*l, const char*script, const char*function, value_t*args, int max_seconds, bool*timeout);

#endif //__language_interpreter_h__
