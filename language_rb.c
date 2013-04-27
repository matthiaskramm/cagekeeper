#include <ruby.h>
#include <stdbool.h>
#include <string.h>
#include "language.h"
#include "dict.h"

typedef struct _rb_internal {
    VALUE object;
    dict_t*functions;
} rb_internal_t;

static rb_internal_t*global;
static int rb_reference_count = 0;

static bool init_rb(rb_internal_t*rb)
{
    if(rb_reference_count==0) {
        ruby_init();
        rb->functions = dict_new(&ptr_type);
        global = rb;
    }
    rb_reference_count++;

    rb->object = rb_eval_string("Object");
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

static value_t* ruby_to_value(VALUE v)
{
  switch (TYPE(v)) {
    case T_NIL:
      return value_new_void();
    case T_BIGNUM:
    case T_FIXNUM:
      return value_new_int32(NUM2INT(v));
    case T_TRUE:
      return value_new_boolean(true);
    case T_FALSE:
      return value_new_boolean(false);
    case T_FLOAT:
      return value_new_float32(NUM2DBL(v));
    case T_SYMBOL:
      return value_new_string(rb_id2name(SYM2ID(v)));
    case T_STRING:
      return value_new_string(StringValuePtr(v));
    case T_ARRAY: {
      /* process Array */
      value_t*array = array_new();
      int len = RARRAY(v)->len;
      int i;
      for(i=0;i<len;i++) {
          volatile VALUE item = RARRAY(v)->ptr[i];
          array_append(array, ruby_to_value(item));
      }
      return array;
    }
    default:
      /* raise exception */
      rb_raise(rb_eTypeError, "not valid value");
  }
}

static VALUE value_to_ruby(value_t*v)
{
    switch(v->type) {
        case TYPE_VOID:
            return Qnil;
        break;
        case TYPE_FLOAT32:
            return rb_float_new(v->f32);
        break;
        case TYPE_INT32:
            return INT2FIX(v->i32);
        break;
        case TYPE_BOOLEAN:
            if(v->b) {
                return Qtrue;
            } else {
                return Qfalse;
            }
        break;
        case TYPE_STRING: {
            return rb_str_new2(v->str);
        }
        break;
        case TYPE_ARRAY: {
            VALUE a = rb_ary_new2(v->length);
            int i;
            for(i=0;i<v->length;i++) {
                rb_ary_store(a, i, value_to_ruby(v->data[i]));
            }
            return a;
        }
        break;
        default:
            return Qnil;
    }
}

typedef struct _ruby_dfunc {
    language_t*li;
    const char*script;
    bool fail;
} ruby_dfunc_t;

static VALUE compile_script_internal(VALUE _dfunc)
{
    ruby_dfunc_t*dfunc = (ruby_dfunc_t*)_dfunc;
    language_t*li = dfunc->li;
    rb_internal_t*rb = (rb_internal_t*)li->internal;

    VALUE ret = rb_eval_string(dfunc->script);
    dfunc->fail = false;
    return Qtrue;
}

static VALUE compile_script_exception(VALUE _dfunc)
{
    ruby_dfunc_t*dfunc = (ruby_dfunc_t*)_dfunc;
    dfunc->fail = true;
    return Qfalse;
}

static bool compile_script_rb(language_t*li, const char*script)
{
    dbg("[ruby] compile_script");
    ruby_dfunc_t dfunc;
    dfunc.li = li;
    dfunc.script = script;
    dfunc.fail = false;
    VALUE ret = rb_rescue(compile_script_internal, (VALUE)&dfunc, compile_script_exception, (VALUE)&dfunc);
    return !dfunc.fail;
}

static void define_constant_rb(language_t*li, const char*name, value_t*value)
{
    dbg("[ruby] define constant %s", name);
    rb_define_global_const(name, value_to_ruby(value));
}

static VALUE ruby_function_proxy(VALUE self, VALUE _args)
{
    printf("calling function %p\n", (void*)self);
    ID id = rb_frame_last_func();

    value_t* function = dict_lookup(global->functions, (void*)id);
    if(function) {
        printf("calling function %s\n", rb_id2name(id));
        value_t*args = ruby_to_value(_args);
        value_t*ret = function->call(function, args);
        value_destroy(args);
        VALUE r = value_to_ruby(ret);
        value_destroy(ret);
        return r;
    }
    return Qnil;
}

static void define_function_rb(language_t*li, const char*name, function_t*f)
{
    rb_internal_t*rb = (rb_internal_t*)li->internal;
    dbg("[ruby] define function %s", name);
    rb_define_global_function(name, ruby_function_proxy, -2);

    ID id = rb_intern(name);
    dict_put(global->functions, (void*)id, f);
}

static bool is_function_rb(language_t*li, const char*name)
{
    dbg("[ruby] is_function(%s)", name);
    rb_internal_t*rb = (rb_internal_t*)li->internal;
    ID id = rb_intern(name);
    return rb_respond_to(rb->object, id);
}

typedef struct _ruby_fcall {
    language_t*li;
    const char*function_name;
    value_t*args;
    bool fail;
} ruby_fcall_t;

static VALUE call_function_internal(VALUE _fcall)
{
    ruby_fcall_t*fcall = (ruby_fcall_t*)_fcall;
    language_t*li = fcall->li;

    rb_internal_t*rb = (rb_internal_t*)li->internal;

    int num_args = fcall->args->length;
    volatile ID fname = rb_intern(fcall->function_name);

    VALUE*args = alloca(sizeof(VALUE)*num_args);
    int i;
    for(i=0;i<num_args;i++) {
        args[i] = value_to_ruby(fcall->args->data[i]);
    }
    return rb_funcall2(rb->object, fname, num_args, args);
}
static VALUE call_function_exception(VALUE _fcall)
{
    ruby_fcall_t*fcall = (ruby_fcall_t*)_fcall;
    rb_report_error();
    fcall->fail = true;
}
static value_t* call_function_rb(language_t*li, const char*name, value_t*args)
{
    dbg("[ruby] calling function %s", name);
    ruby_fcall_t fcall;
    fcall.li = li;
    fcall.fail = false;
    fcall.args = args;
    fcall.function_name = name;

    VALUE ret = rb_rescue(call_function_internal, (VALUE)&fcall, call_function_exception, (VALUE)&fcall);

    if(fcall.fail) {
        return NULL;
    } else {
        return ruby_to_value(ret);
    }
}

static void destroy_rb(language_t* li)
{
    rb_internal_t*rb = (rb_internal_t*)li->internal;
    if(--rb_reference_count == 0) {
        ruby_finalize();
    }
    free(rb);
    free(li);
}

language_t* ruby_interpreter_new()
{
    language_t * li = calloc(1, sizeof(language_t));
#ifdef DEBUG
    li->magic = LANG_MAGIC;
#endif
    li->name = "rb";
    li->compile_script = compile_script_rb;
    li->is_function = is_function_rb;
    li->define_constant = define_constant_rb;
    li->define_function = define_function_rb;
    li->call_function = call_function_rb;
    li->destroy = destroy_rb;
    li->internal = calloc(1, sizeof(rb_internal_t));
    rb_internal_t*rb = (rb_internal_t*)li->internal;
    init_rb(rb);
    return li;
}

