#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "language.h"

typedef struct _proxy_internal {
    language_t*li;
    language_t*old;
    pid_t child_pid;
    int fd_w;
    int fd_r;
    int timeout;
} proxy_internal_t;

#define DEFINE_CONSTANT 1
#define DEFINE_FUNCTION 2
#define COMPILE_SCRIPT 3
#define IS_FUNCTION 4
#define CALL_FUNCTION 5

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

static char* read_string(int fd, struct timeval* timeout)
{
    int l = 0;
    if(!read_with_timeout(fd, &l, sizeof(l), timeout))
        return NULL;
    if(l<0 || l>=MAX_STRING_SIZE)
        return NULL;
    char* s = malloc(l+1);
    if(!s)
        return NULL;
    if(!read_with_timeout(fd, s, l+1, timeout))
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

static value_t* _read_value(int fd, int*count, struct timeval* timeout)
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
            char*s = read_string(fd, timeout);
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
            if(dummy.length >= MAX_ARRAY_SIZE)
                return NULL;
            if(dummy.length >= INT_MAX - *count)
                return NULL;

            if(dummy.length + *count >= MAX_ARRAY_SIZE)
                return NULL;

            value_t*array = array_new();
            int i;
            for(i=0;i<dummy.length;i++) {
                value_t*entry = _read_value(fd, count, timeout);
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
    return _read_value(fd, &count, timeout);
}

static void define_constant_proxy(language_t*li, const char*name, value_t*value)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    write_byte(proxy->fd_w, DEFINE_CONSTANT);
    write_string(proxy->fd_w, name);
    write_value(proxy->fd_w, value);
}

static void define_function_proxy(language_t*li, function_def_t*f)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    write_byte(proxy->fd_w, DEFINE_FUNCTION);
    write_string(proxy->fd_w, f->name);
    write_string(proxy->fd_w, f->params);
    write_string(proxy->fd_w, f->ret);
}

static bool compile_script_proxy(language_t*li, const char*script)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    write_byte(proxy->fd_w, COMPILE_SCRIPT);
    write_string(proxy->fd_w, script);

    struct timeval timeout;
    timeout.tv_sec = proxy->timeout;
    timeout.tv_usec = 0;

    bool ret = false;
    if(!read_with_timeout(proxy->fd_r, &ret, 1, &timeout))
        return false;
    return !!ret;
}

static bool is_function_proxy(language_t*li, const char*name)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    write_byte(proxy->fd_w, IS_FUNCTION);
    write_string(proxy->fd_w, name);

    struct timeval timeout;
    timeout.tv_sec = proxy->timeout;
    timeout.tv_usec = 0;

    bool ret = false;
    if(!read_with_timeout(proxy->fd_r, &ret, 1, &timeout))
        return false;
    return !!ret;
}

static value_t* call_function_proxy(language_t*li, const char*name, value_t*args)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;

    write_byte(proxy->fd_w, CALL_FUNCTION);
    write_string(proxy->fd_w, name);

    struct timeval timeout;
    timeout.tv_sec = proxy->timeout;
    timeout.tv_usec = 0;

    return read_value(proxy->fd_r, &timeout);
}

static void destroy_proxy(language_t* li)
{
    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;
    free(proxy);
    free(li);
}

language_t* proxy_new(language_t*old)
{
    language_t * li = calloc(1, sizeof(language_t));
#ifdef DEBUG
    li->magic = LANG_MAGIC;
#endif
    li->name = "proxy";
    li->compile_script = compile_script_proxy;
    li->is_function = is_function_proxy;
    li->call_function = call_function_proxy;
    li->define_constant = define_constant_proxy;
    li->destroy = destroy_proxy;
    li->internal = calloc(1, sizeof(proxy_internal_t));

    proxy_internal_t*proxy = (proxy_internal_t*)li->internal;
    proxy->li = li;
    proxy->old = old;
    proxy->timeout = 10;
    return li;
}
