#include <ruby.h>
#include <stdbool.h>
#include <string.h>
#include "language.h"

typedef struct _rb_internal {
} rb_internal_t;

static int rb_reference_count = 0;

static bool init_rb(rb_internal_t*rb)
{
    if(rb_reference_count==0) {
        ruby_init();
    }
    rb_reference_count++;
}

static void rb_report_error()
{
    VALUE lasterr = rb_gv_get("$!");
    VALUE message = rb_obj_as_string(lasterr);
    char*msg = RSTRING(message)->ptr;
    if(msg && *msg) {
        printf("Ruby Error:\n");
        printf("%s\n", msg);
    }
}

typedef struct _ruby_dfunc {
    language_t*li;
    const char*script;
    bool fail;
} ruby_dfunc_t;

static VALUE define_function_internal(VALUE _dfunc)
{
    ruby_dfunc_t*dfunc = (ruby_dfunc_t*)_dfunc;
    language_t*li = dfunc->li;
    rb_internal_t*rb = (rb_internal_t*)li->internal;

    VALUE ret = rb_eval_string(dfunc->script);

    return Qtrue;
}

static VALUE define_function_exception(VALUE _dfunc)
{
    ruby_dfunc_t*dfunc = (ruby_dfunc_t*)_dfunc;
    dfunc->fail = true;
    return Qfalse;
}

static bool define_function_rb(language_t*li, const char*script)
{
    ruby_dfunc_t dfunc;
    dfunc.li = li;
    dfunc.script = script;
    dfunc.fail = false;
    VALUE ret = rb_rescue(define_function_internal, (VALUE)&dfunc, define_function_exception, (VALUE)&dfunc);
    return !dfunc.fail;
}

typedef struct _ruby_fcall {
    language_t*li;
    const char*function_name;
    bool fail;
} ruby_fcall_t;

static VALUE call_function_internal(VALUE _fcall)
{
    ruby_fcall_t*fcall = (ruby_fcall_t*)_fcall;
    language_t*li = fcall->li;

    rb_internal_t*rb = (rb_internal_t*)li->internal;

    volatile VALUE array = rb_ary_new2(0);

    volatile ID fname = rb_intern(fcall->function_name);
    volatile VALUE object = rb_eval_string("Object");
    return rb_funcall(object, fname, 1, array);
}

static VALUE call_function_exception(VALUE _fcall)
{
    ruby_fcall_t*fcall = (ruby_fcall_t*)_fcall;
    if(fcall->li->verbosity > 0)
        rb_report_error();
    fcall->fail = true;
}

static int call_function_rb(language_t*li, const char*function_name)
{
    ruby_fcall_t fcall;
    fcall.li = li;
    fcall.fail = false;
    fcall.function_name = function_name;

    VALUE ret = rb_rescue(call_function_internal, (VALUE)&fcall, call_function_exception, (VALUE)&fcall);

    if(fcall.fail) {
        return -1;
    }

    int result = NUM2INT(ret);
    return result;
}

static void destroy_rb(language_t* li)
{
    rb_internal_t*rb = (rb_internal_t*)li->internal;
    if(--rb_reference_count == 0) {
        ruby_finalize();
    }
}

language_t* ruby_interpreter_new()
{
    language_t * li = calloc(1, sizeof(language_t));
#ifdef DEBUG
    li->magic = LANG_MAGIC;
#endif
    li->name = "rb";
    li->define_function = define_function_rb;
    li->call_function = call_function_rb;
    li->destroy = destroy_rb;
    li->internal = calloc(1, sizeof(rb_internal_t));
    rb_internal_t*rb = (rb_internal_t*)li->internal;
    init_rb(rb);
    return li;
}

