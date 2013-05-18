#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
#include "language.h"
#include "settings.h"

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

    log_msg("%s", buf);

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

language_t* proxy_new(language_t*language);

language_t* wrap_sandbox(language_t*language)
{
    if(language->internal) {
        fprintf(stderr, "Can't wrap sandbox, language already initialized\n");
        return NULL;
    }
    return proxy_new(language);
}

static language_t* raw_interpreter_by_extension(const char*filename)
{
    const char*dot = strrchr(filename, '.');
    const char*extension = dot ? dot+1 : filename;

    language_t*core = NULL;
    if(!strcmp(extension, "lua"))
        return lua_interpreter_new();
    else if(!strcmp(extension, "py"))
        return python_interpreter_new();
    else if(!strcmp(extension, "rb"))
        return ruby_interpreter_new();
    else
        return javascript_interpreter_new();
}

language_t* interpreter_by_extension(const char*filename)
{
    return wrap_sandbox(raw_interpreter_by_extension(filename));
}

language_t* unsafe_interpreter_by_extension(const char*filename)
{
    language_t*li = raw_interpreter_by_extension(filename);
    
    bool ret = li->initialize(li, config_maxmem);
    if(!ret) {
        li->destroy(li);
        return NULL;
    }
    return li;
}

void define_int_constant(language_t*li, const char*name, int i)
{
    value_t* v = value_new_int32(i);
    li->define_constant(li, name, v);
    value_destroy(v);
}

void define_string_constant(language_t*li, const char*name, const char*s)
{
    value_t* v = value_new_string(s);
    li->define_constant(li, name, v);
    value_destroy(v);
}

void define_function(language_t*li, const char*name, void*call, void*context, const char*params, const char*ret)
{
    value_t* v = cfunction_new(li, name, call, context, params, ret);
    li->define_function(li, name, v);
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
