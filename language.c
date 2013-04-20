#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
#include "language.h"

void language_error(language_t*li, const char*error, ...)
{
    char buf[1024];
    int l;
    va_list arglist;
    va_start(arglist, error);
    vsnprintf(buf, sizeof(buf)-1, error, arglist);
    va_end(arglist);
    l = strlen(buf);
    while(l && buf[l-1]=='\n') {
	buf[l-1] = 0;
	l--;
    }
    if(li->error) {
        free((void*)li->error);
    }
#ifdef DEBUG
    printf("%s\n", buf);fflush(stdout);
#endif
    if(li->error_file) {
        fprintf(li->error_file, "%s\n", buf);
    }
    li->error = strdup(buf);
}

static jmp_buf timeout_jmp;
static void sigalarm(int signal)
{
    longjmp(timeout_jmp, 1);
}

static value_t* with_timeout(language_t*l, const char*script, const char*function, value_t*args, int max_seconds, bool*timeout)
{
    value_t*ret = NULL;
    if(timeout) {
        *timeout = false;
    }
    void*old_signal;
    alarm(max_seconds);
    if(setjmp(timeout_jmp)) {
        alarm(0);
        if(timeout) {
            *timeout = true;
        }
        language_error(l, "TIMEOUT");
        signal(SIGPROF, old_signal);
        return NULL;
    }
    old_signal = signal(SIGPROF, sigalarm);

    if(script) {
        int ret = l->compile_script(l, script);
        if(!ret) {
            alarm(0);
            language_error(l, "Couldn't compile");
            return NULL;
        }
    }

    if(function) {
        if(l->is_function(l, function)) {
            // TODO: check for errors, allow void function calls
            ret = l->call_function(l, function, args);
        } else {
            if(!script) {
                alarm(0);
                /* Only report an error if we're not also compiling a script;
                   startup functions are usually optional */
                language_error(l, "No such function: %s\n", function);
            }
            ret = value_new_void();
        }
    }
    alarm(0);

    signal(SIGPROF, old_signal);
    return ret;
}

value_t* call_function_with_timeout(language_t*l, const char*function, value_t*args, int max_seconds, bool*timeout)
{
    return with_timeout(l, NULL, function, args, max_seconds, timeout);
}

value_t* compile_and_run_function_with_timeout(language_t*l, const char*script, const char*function, value_t*args, int max_seconds, bool*timeout)
{
    return with_timeout(l, script, function, args, max_seconds, timeout);
}

language_t* interpreter_by_extension(const char*filename, function_def_t* methods)
{
    const char*dot = strrchr(filename, '.');
    const char*extension = dot ? dot+1 : filename;

    if(!strcmp(extension, "lua"))
        //return lua_interpreter_new(methods);
        return NULL;
    else if(!strcmp(extension, "py"))
        return python_interpreter_new(methods);
    else
        return javascript_interpreter_new(methods);
}

bool define_int_constant(language_t*li, const char*name, int i)
{
    value_t* v = value_new_int32(i);
    bool ret = li->define_constant(li, name, v);
    value_destroy(v);
    return ret;
}

bool define_string_constant(language_t*li, const char*name, const char*s)
{
    value_t* v = value_new_string(s);
    bool ret = li->define_constant(li, name, v);
    value_destroy(v);
    return ret;
}

int call_int_function(language_t*li, const char*name)
{
    value_t*args = array_new();
    value_t*ret = li->call_function(li, name, args);
    value_destroy(args);
    int r;
    if(ret->type != TYPE_INT32) {
        language_error(li, "expected return value to be an integer");
        r = -1;
    } else {
        r = ret->i32;
    }
    value_destroy(ret);
    return r;
}

