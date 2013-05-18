#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include "language.h"
#include "dict.h"
#include "seccomp.h"
#include "settings.h"

typedef struct _proxy_internal {
    language_t*li;
    language_t*old;
    pid_t child_pid;
    int fd_w;
    int fd_r;
    int timeout;
    dict_t*callback_functions;
    bool in_call;
} proxy_internal_t;

enum {
    DEFINE_CONSTANT = 1,
    DEFINE_FUNCTION = 2,
    COMPILE_SCRIPT = 3,
    IS_FUNCTION = 4,
    CALL_FUNCTION =  5,
};

enum {
    RESP_CALLBACK = 10,
    RESP_RETURN = 11,
    RESP_ERROR = 12,
    RESP_LOG = 13,
};

#define MAX_ARRAY_SIZE 1024
#define MAX_STRING_SIZE 4096

static void write_byte(int fd, uint8_t b)
{
    write(fd, &b, 1);
}

static void write_string(int fd, const char*name)
{
    int l = strlen(name);
    write(fd, &l, sizeof(l));
    write(fd, name, l);
}

static char* read_string(int fd, int max_size, struct timeval* timeout)
{
    int l = 0;
    if(!read_with_timeout(fd, &l, sizeof(l), timeout))
        return NULL;
    if(l<0 || (max_size && l>=max_size))
        return NULL;
    char* s = malloc(l+1);
    if(!s)
        return NULL;
    if(!read_with_timeout(fd, s, l, timeout))
        return NULL;
    s[l]=0;
    return s;
}

static const char* write_value(int fd, value_t*v)
{ 
    char b = v->type;
    write(fd, &b, 1);

    switch(v->type) {
        case TYPE_VOID:
            return;
        case TYPE_FLOAT32:
            write(fd, &v->f32, sizeof(v->f32));
            return;
        case TYPE_INT32:
            write(fd, &v->i32, sizeof(v->i32));
            return;
        case TYPE_BOOLEAN:
            write(fd, &v->b, sizeof(v->b));
            return;
        case TYPE_STRING:
            write_string(fd, v->str);
            return;
        case TYPE_ARRAY:
            write(fd, &v->length, sizeof(v->length));
            int i;
            for(i=0;i<v->length;i++) {
                write_value(fd, v->data[i]);
            }
            return;
    }
}

static value_t* _read_value(int fd, int*count, int max_string_size, int max_array_size, struct timeval* timeout)
{ 
    char b = 0;
    if(!read_with_timeout(fd, &b, 1, timeout)) {
        return NULL;
    }
    value_t dummy;

    switch(b) {
        case TYPE_VOID:
            return value_new_void();
        case TYPE_FLOAT32:
            if(!read_with_timeout(fd, &dummy.f32, sizeof(dummy.f32), timeout)) {
                return NULL;
            }
            return value_new_float32(dummy.f32);
        case TYPE_INT32:
            if(!read_with_timeout(fd, &dummy.i32, sizeof(dummy.i32), timeout)) {
                return NULL;
            }
            return value_new_int32(dummy.i32);
        case TYPE_BOOLEAN:
            if(!read_with_timeout(fd, &dummy.b, sizeof(dummy.b), timeout)) {
                return NULL;
            }
            return value_new_boolean(!!dummy.b);
        case TYPE_STRING: {
            char*s = read_string(fd, max_string_size, timeout);
            if(!s)
                return NULL;
            value_t* v = value_new_string(s);
            free(s);
            return v;
        }
        case TYPE_ARRAY: {
            if(!read_with_timeout(fd, &dummy.length, sizeof(dummy.length), timeout)) {
                return NULL;
            }

            /* protect against int overflows */
            if(max_array_size && dummy.length >= max_array_size)
                return NULL;
            if(dummy.length >= INT_MAX - *count)
                return NULL;

            if(max_array_size && dummy.length + *count >= max_array_size)
                return NULL;

            value_t*array = array_new();
            int i;
            for(i=0;i<dummy.length;i++) {
                value_t*entry = _read_value(fd, count, max_string_size, max_array_size, timeout);
                if(entry == NULL) {
                    value_destroy(array);
                    return NULL;
                }
                array_append(array, entry);
            }
            *count += dummy.length;
            return array;
        }
        default:
            return NULL;
    }
}

static value_t* read_value(int fd, struct timeval* timeout)
{
    int count = 0;
    return _read_value(fd, &count, MAX_STRING_SIZE, MAX_ARRAY_SIZE, timeout);
}

static value_t* read_value_nolimit(int fd)
{
    int count = 0;
    return _read_value(fd, &count, 0, 0, NULL);
}


static void define_constant_proxy(language_t*li, const char*name, value_t*value)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    log_dbg("[proxy] define_constant(%s)", name);
    write_byte(proxy->fd_w, DEFINE_CONSTANT);
    write_string(proxy->fd_w, name);
    write_value(proxy->fd_w, value);
}

static void define_function_proxy(language_t*li, const char*name, function_t*f)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    log_dbg("[proxy] define_function(%s)", name);
    
    /* let the child know that we're accepting callbacks for this function name */
    write_byte(proxy->fd_w, DEFINE_FUNCTION);
    write_string(proxy->fd_w, name);
    write_byte(proxy->fd_w, f->num_params);

    if(dict_contains(proxy->callback_functions, name)) {
        language_error(li, "function %s already defined", name);
        return;
    }

    dict_put(proxy->callback_functions, name, f);
}

static bool process_callbacks(language_t*li, struct timeval* timeout)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    while(1) {
        char resp = 0;
        if(!read_with_timeout(proxy->fd_r, &resp, 1, timeout)) {
            return false;
        }

        switch(resp) {
            case RESP_CALLBACK: {
                char*name = read_string(proxy->fd_r, MAX_STRING_SIZE, timeout);
                if(!name) {
                    return false;
                }
                value_t*args = read_value(proxy->fd_r, timeout);
                if(!args) {
                    free(name);
                    return false;
                }
                value_t*function = dict_lookup(proxy->callback_functions, name);
                if(!function) {
                    language_error(li, "Calling unknown callback function\n");
                    value_destroy(args);
                    free(name);
                    return false;
                }
                value_t*ret = function->call(function, args);
                if(!ret) {
                    value_destroy(args);
                    free(name);
                    return false;
                }
                write_value(proxy->fd_w, ret);
                value_destroy(ret);
                value_destroy(args);
                free(name);
            }
            break;
            case RESP_LOG: {
                char*message = read_string(proxy->fd_r, MAX_STRING_SIZE, timeout);
                language_log(li, message);                
            }
            break;
            case RESP_ERROR:
            return false;
            case RESP_RETURN:
            return true;
        }
    }
}

static bool compile_script_proxy(language_t*li, const char*script)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    log_dbg("[proxy] compile_script()");
    write_byte(proxy->fd_w, COMPILE_SCRIPT);
    write_string(proxy->fd_w, script);

    struct timeval timeout;
    timeout.tv_sec = proxy->timeout;
    timeout.tv_usec = 0;

    if(proxy->in_call) {
        language_error(li, "You called (or compiled) the guest program, and the guest program called back. You can't invoke the guest again from your callback function.");
        return NULL;
    }

    bool ret = false;

    proxy->in_call = true;
    ret = process_callbacks(li, &timeout);
    if(!ret) {
        if(!timeout.tv_sec && !timeout.tv_usec) {
            li->timeout = true;
            language_error(li, "Timeout while compiling\n");
        }
        return false;
    }
    proxy->in_call = false;

    if(!read_with_timeout(proxy->fd_r, &ret, 1, &timeout)) {
        if(!timeout.tv_sec && !timeout.tv_usec) {
            // TODO: verify that select does indeed set these values to 0 on timeout
            li->timeout = true;
            language_error(li, "Timeout while compiling.\n");
        }
        return false;
    }
    return !!ret;
}

static bool is_function_proxy(language_t*li, const char*name)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    log_dbg("[proxy] is_function(%s)", name);
    write_byte(proxy->fd_w, IS_FUNCTION);
    write_string(proxy->fd_w, name);

    struct timeval timeout;
    timeout.tv_sec = proxy->timeout;
    timeout.tv_usec = 0;

    bool ret = false;
    if(!read_with_timeout(proxy->fd_r, &ret, 1, &timeout)) {
        return false;
    }
    return !!ret;
}

static value_t* call_function_proxy(language_t*li, const char*name, value_t*args)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    log_dbg("[proxy] call_function(%s)", name);
    write_byte(proxy->fd_w, CALL_FUNCTION);
    write_string(proxy->fd_w, name);
    write_value(proxy->fd_w, args);

    struct timeval timeout;
    timeout.tv_sec = proxy->timeout;
    timeout.tv_usec = 0;

    if(proxy->in_call) {
        language_error(li, "You called the guest program, and the guest program called back. You can't invoke the guest again from your callback function.");
        return NULL;
    }

    bool ret;

    proxy->in_call = true;
    ret = process_callbacks(li, &timeout);
    if(!ret) {
        if(!timeout.tv_sec && !timeout.tv_usec) {
            li->timeout = true;
            language_error(li, "Timeout while calling function %s\n", name);
        }
        return NULL;
    }
    proxy->in_call = false;

    value_t*value = read_value(proxy->fd_r, &timeout);
    if(!value) {
        if(!timeout.tv_sec && !timeout.tv_usec) {
            li->timeout = true;
            language_error(li, "Timeout while calling function %s.\n", name);
        }
        return NULL;
    }
    return value;
}

typedef struct _proxy_function {
    language_t*li;
    char*name;
} proxy_function_t;

static void proxy_function_destroy(value_t*v)
{
    proxy_function_t*f = (proxy_function_t*)v->internal; 
    free(f->name);
    free(v->internal);
    free(v);
}

static value_t* proxy_function_call(value_t*v, value_t*args)
{
    proxy_function_t*f = (proxy_function_t*)v->internal; 
    log_dbg("[sandbox] invoking callback %s", f->name);
    language_t*li = f->li;
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    write_byte(proxy->fd_w, RESP_CALLBACK);
    write_string(proxy->fd_w, f->name);
    write_value(proxy->fd_w, args);
    return read_value_nolimit(proxy->fd_r);
}

static void child_loop(language_t*li)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;
    language_t*old = proxy->old;

    int r = proxy->fd_r;
    int w = proxy->fd_w;

    while(1) {
        char command;
        if(!read_with_retry(r, &command, 1)) {
            log_dbg("[sandbox] Couldn't read command- parent terminated?");
            _exit(1);
        }

        log_dbg("[sandbox] command=%d", command);
        switch(command) {
            case DEFINE_CONSTANT: {
                char*s = read_string(r, 0, NULL);
                log_dbg("[sandbox] define constant(%s)", s);
                value_t*v = read_value_nolimit(r);
                old->define_constant(old, s, v);
            }
            break;
            case DEFINE_FUNCTION: {
                char*name = read_string(r, 0, NULL);
                uint8_t num_params = 0;
                read_with_retry(r, &num_params, 1);

                log_dbg("[sandbox] define function(%s), %d parameters", name, num_params);

                proxy_function_t*pf = calloc(sizeof(proxy_function_t), 1);
                pf->li = li;
                pf->name = name;

                value_t*value = calloc(sizeof(value_t), 1);
                value->type = TYPE_FUNCTION;
                value->internal = pf;
                value->destroy = proxy_function_destroy;
                value->call = proxy_function_call;
                value->num_params = num_params;

                old->define_function(old, name, value);
            }
            break;
            case COMPILE_SCRIPT: {
                char*script = read_string(r, 0, NULL);
                log_dbg("[sandbox] compile script");
                bool ret = old->compile_script(old, script);
                write_byte(w, RESP_RETURN);
                write_byte(w, ret);
                free(script);
            }
            break;
            case IS_FUNCTION: {
                char*function_name = read_string(r, 0, NULL);
                log_dbg("[sandbox] is_function(%s)", function_name);
                bool ret = old->is_function(old, function_name);
                write_byte(w, ret);
                free(function_name);
            }
            break;
            case CALL_FUNCTION: {
                char*function_name = read_string(r, 0, NULL);
                log_dbg("[sandbox] call_function(%s)", function_name, old->name);
                value_t*args = read_value_nolimit(r);
                value_t*ret = old->call_function(old, function_name, args);
                if(ret) {
                    log_dbg("[sandbox] returning function value (type:%s)", type_to_string(ret->type));
                    write_byte(w, RESP_RETURN);
                    write_value(w, ret);
                    value_destroy(ret);
                } else {
                    log_dbg("[sandbox] error calling function %s", function_name);
                    write_byte(w, RESP_ERROR);
                }
                free(function_name);
                value_destroy(args);
            }
            break;
            default: {
                fprintf(stderr, "Invalid command %d\n", command);
            }
        }
    }
}

static void close_all_fds(int*keep, int keep_num)
{
    int max=sysconf(_SC_OPEN_MAX);
    int fd;
    for(fd=0; fd<max; fd++) {
        int j;
        bool do_keep = false;
        for(j=0;j<keep_num;j++) {
            do_keep |= keep[j] == fd;
        }
        if(!do_keep) {
            close(fd);
        }
    }
}

static void sandbox_log(void*user, const char*str)
{
    proxy_internal_t*proxy = (proxy_internal_t*)user;

    write_byte(proxy->fd_w, RESP_LOG);
    write_string(proxy->fd_w, str);
}

static bool spawn_child(language_t*li)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    int p_to_c[2];
    int c_to_p[2];
    int stdout_pipe[2];
    int stderr_pipe[2];

    if(pipe(p_to_c) || pipe(c_to_p) || pipe(stdout_pipe) || pipe(stderr_pipe)) {
        perror("create pipe");
        return false;
    }

    proxy->child_pid = fork();
    if(!proxy->child_pid) {
        //child
        proxy->fd_r = p_to_c[0];
        proxy->fd_w = c_to_p[1];

        int keep[] = {1, 2, proxy->fd_r, proxy->fd_w};
        close_all_fds(keep, sizeof(keep)/sizeof(keep[0]));

        /* We haven't loaded any 3rd party code yet. 
           Give the language interpreter a chance to do some initializations 
           (with all syscalls still available) before we switch into secure mode.
         */
        bool ret = proxy->old->initialize(proxy->old, config_maxmem);
        if(!ret) {
            _exit(44);
        }

        /* log messages are passed back to the parent */
        proxy->old->log = sandbox_log;
        proxy->old->user = proxy;

        seccomp_lockdown();
        fflush(stdout);

        child_loop(li);
        _exit(0);
    }

    //parent
    close(c_to_p[1]); // close write
    close(p_to_c[0]); // close read
    proxy->fd_r = c_to_p[0];
    proxy->fd_w = p_to_c[1];
    return true;
}

static void destroy_proxy(language_t* li)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    language_t*old = proxy->old;

    int status = 0;
    int ret = waitpid(proxy->child_pid, &status, WNOHANG | WUNTRACED | WCONTINUED);

    if(ret == 0) {
#ifdef PROCSTAT
        char buffer[256];
        sprintf(buffer, "procstat /proc/%d/stat", proxy->child_pid);
        system(buffer);
#endif
        log_dbg("killing sandbox process %d\n", proxy->child_pid);
        kill(proxy->child_pid, SIGKILL);
        ret = waitpid(proxy->child_pid, &status, 0);
    }
    if(WIFSIGNALED(status)) {
        log_dbg("%08x %08x signal=%d\n", ret, status, WTERMSIG(status));
    } else if(WIFEXITED(status)) {
        log_dbg("%08x %08x exit=%d\n", ret, status, WEXITSTATUS(status));
    } else {
        log_dbg("%08x %08x unknown exit reason. status=%d\n", ret, status, status);
    }
    free(proxy);
    free(li);

    old->destroy(old);
}

bool initialize_proxy(language_t*li, size_t memsize)
{
    return true;
}

language_t* proxy_new(language_t*old)
{
    language_t * li = calloc(1, sizeof(language_t));
    li->name = "proxy";
    li->initialize = initialize_proxy;
    li->compile_script = compile_script_proxy;
    li->is_function = is_function_proxy;
    li->call_function = call_function_proxy;
    li->define_function = define_function_proxy;
    li->define_constant = define_constant_proxy;
    li->destroy = destroy_proxy;
    li->internal = calloc(1, sizeof(proxy_internal_t));

    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;
    proxy->li = li;
    proxy->old = old;
    proxy->timeout = config_maxtime;

    if(!spawn_child(li)) {
        fprintf(stderr, "Couldn't spawn child process\n");
        free(proxy);
        free(li);
        return NULL;
    }

    proxy->callback_functions = dict_new(&charptr_type);

    return li;
}
