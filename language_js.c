#include <stdbool.h>
#include <string.h>
#include <assert.h>
#ifdef _MSC_VER
# define XP_WIN
#else
# define XP_UNIX
#endif
#include <jsapi.h>
#include <ffi.h>

#include "language.h"
#include "util.h"
#include "dict.h"
#include "function.h"

typedef struct _js_internal {
    language_t*li;
    JSRuntime *rt;
    JSContext *cx;
    JSObject *global;
    char*buffer;
    char noerrors;

    dict_t* jsfunction_to_functiondef;
} js_internal_t;

static JSClass global_class = {
    "global",
    JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, JS_FinalizeStub,
    JSCLASS_NO_OPTIONAL_MEMBERS };

static void error_callback(JSContext *cx, const char *message, JSErrorReport *report) {

    js_internal_t*js = JS_GetContextPrivate(cx);
    if(js->noerrors)
        return;
    if(js->li->verbosity > 0) {
        printf("line %u: %s\n",
            (unsigned int) report->lineno, message);
    }
    if(js->li->error_file) {
        fprintf(js->li->error_file, "line %u: %s\n",
            (unsigned int) report->lineno, message);
    }
}

value_t* js_argv_to_args(language_t*li, JSContext *cx, uintN argc, jsval *argv, function_def_t*f)
{
    int i;
    jsval*sp = argv;

    value_t*args = array_new();

    function_signature_t*sig = function_get_signature(f);

    // Also see JS_ConvertArguments()
    for(i=0;i<sig->num_params;i++) {
        switch(sig->param[i]) {
            case TYPE_FLOAT32: {
                jsdouble d;
                JS_ValueToNumber(cx, *sp, &d);
                array_append_float32(args, d);
            }
            break;
            case TYPE_INT32: {
                int32 i;
                JS_ValueToInt32(cx, *sp, &i);
                array_append_int32(args, i);
            }
            break;
            case TYPE_BOOLEAN: {
                JSBool b;
                JS_ValueToBoolean(cx, *sp, &b);
                array_append_boolean(args, b);
            }
            break;
            case TYPE_STRING: {
                JSString*s = JS_ValueToString(cx, *sp);
                char*cstr = JS_EncodeString(cx, s);
                array_append_string(args, cstr);
            }
            break;
            case TYPE_ARRAY: {
                language_error(li, "Passing arrays out of javascript not yet supported.");
                array_append_boolean(args, false);
            }
            break;
            case TYPE_VOID:
            default: {
                fprintf(stderr, "Internal error: can't convert type %d", sig->param[i]);
                assert(0);
            }
            break;
        }
        sp++;
    }
    function_signature_destroy(sig);

    return args;
}

static jsval value_to_jsval(JSContext*cx, value_t*value)
{
    switch(value->type) {
        case TYPE_VOID:
            return OBJECT_TO_JSVAL(NULL);
        break;
        case TYPE_FLOAT32:
            return DOUBLE_TO_JSVAL(value->f32);
        break;
        case TYPE_INT32:
            return INT_TO_JSVAL(value->i32);
        break;
        case TYPE_BOOLEAN:
            return BOOLEAN_TO_JSVAL(value->b);
        break;
        case TYPE_STRING: {
            JSString *s = JS_InternString(cx, value->str);
            return STRING_TO_JSVAL(s);
        }
        break;
        case TYPE_ARRAY: {
            JSObject *array = JS_NewArrayObject(cx, 0, NULL);
            if (array == NULL)
                return OBJECT_TO_JSVAL(NULL);
            int i;
            for(i=0;i<value->length;i++) {
                jsval entry = value_to_jsval(cx, value->data[i]);
                char nr[16];
                sprintf(nr, "%d", i);
                JS_SetProperty(cx, array, nr, &entry);
            }
            return OBJECT_TO_JSVAL(array);
        }
        break;
        default: {
            assert(0);
        }
    }
}

static JSBool js_function_proxy(JSContext *cx, uintN argc, jsval *vp)
{
    js_internal_t*js = JS_GetContextPrivate(cx);

    jsval* argv = JS_ARGV(cx, vp);
    JSFunction*func = JS_ValueToFunction(cx, argv[-2]);
    if(!func) {
        language_error(js->li, "Internal error: Couldn't determine function pointer for %p (%d arguments)", argv[-2], argc);
        return JS_FALSE;
    }

    function_def_t*f = dict_lookup(js->jsfunction_to_functiondef, func);
    if(!f) {
        language_error(js->li, "Internal error: Javascript tried to call native function %p (%d args), which we've never seen before.", func, argc);
        return JS_FALSE;
    }

    dbg("js native call to %s\n", f->name);
    value_t* args = js_argv_to_args(js->li, cx, argc, argv, f);
    value_t* value = function_call(js->li, f, args);
    value_destroy(args);

    JS_SET_RVAL(cx, vp, value_to_jsval(cx, value));
    value_destroy(value);
    return JS_TRUE;
}

static void define_function_js(language_t*li, function_def_t*f)
{
    js_internal_t*js = (js_internal_t*)li->internal;
    JSFunction*func = JS_DefineFunction(js->cx, js->global, 
                          f->name, 
                          js_function_proxy,
                          function_count_args(f),
                          0
                       );
    dict_put(js->jsfunction_to_functiondef, func, f);
}

bool init_js(js_internal_t*js)
{
    js->rt = JS_NewRuntime(128L * 1024L * 1024L);
    if (js->rt == NULL)
        return false;
    js->cx = JS_NewContext(js->rt, 8192);
    if (js->cx == NULL)
        return false;

    JS_SetContextPrivate(js->cx, js);

    JS_SetOptions(js->cx, JSOPTION_VAROBJFIX | JSOPTION_JIT);
    JS_SetVersion(js->cx, JSVERSION_LATEST);
    JS_SetErrorReporter(js->cx, error_callback);

    js->global = JS_NewCompartmentAndGlobalObject(js->cx, &global_class, NULL);
    if (js->global == NULL)
        return false;

    /* Populate the global object with the standard globals, like Object and Array. */
    if (!JS_InitStandardClasses(js->cx, js->global))
        return false;

    js->buffer = malloc(65536);
    js->jsfunction_to_functiondef = dict_new(&ptr_type);
    return true;
}

void define_constant_js(language_t*li, const char*name, value_t* value)
{
    js_internal_t*js = (js_internal_t*)li->internal;
    jsval v = value_to_jsval(js->cx, value);
#ifdef DEBUG
    printf("js: define constant %s=",name);
    value_dump(value);
    printf("\n",name);
#endif
    if(!JS_SetProperty(js->cx, js->global, name, &v)) {
        language_error(li, "Couldn't define constant %s", name);
    }
}

static bool compile_script_js(language_t*li, const char*script)
{
    js_internal_t*js = (js_internal_t*)li->internal;
    jsval rval;
    JSBool ok;
    ok = JS_EvaluateScript(js->cx, js->global, script, strlen(script), "__main__", 1, &rval);
    return ok;
}

static bool is_function_js(language_t*li, const char*name)
{
    js_internal_t*js = (js_internal_t*)li->internal;
    jsval rval;
    JSBool ok;
    js->noerrors = 1;
    ok = JS_EvaluateScript(js->cx, js->global, name, strlen(name), "__main__", 1, &rval);
    js->noerrors = 0;
    if(!ok) {
        return false;
    }
    if(JSVAL_IS_OBJECT(rval))
        return true;
    return false;
}

static value_t* jsval_to_value(js_internal_t*js, jsval v)
{
    int32 d;
    if(JSVAL_IS_NULL(v)) {
        return value_new_void();
    } else if(JSVAL_IS_VOID(v)) {
        return value_new_void();
    } else if(JSVAL_IS_INT(v)) {
        return value_new_int32(JSVAL_TO_INT(v));
    } else if(JSVAL_IS_NUMBER(v)) {
        return value_new_float32(JSVAL_TO_DOUBLE(v));
    } else if(JSVAL_IS_STRING(v) || JSVAL_IS_OBJECT(v)) {
        JSString*s = JSVAL_TO_STRING(v);
        char*cstr = JS_EncodeString(js->cx, s);
        return value_new_string(cstr);
    } else if(JSVAL_IS_BOOLEAN(v)) {
        return value_new_boolean(JSVAL_TO_BOOLEAN(v));
    } else {
        language_error(js->li, "Can't convert javascript type to a value.\n");
        return NULL;
    }
}

static value_t* call_function_js(language_t*li, const char*name, value_t* args)
{
    js_internal_t*js = (js_internal_t*)li->internal;
    jsval rval;
    JSBool ok;
    assert(args->type == TYPE_ARRAY);
    if(args->length) {
        language_error(js->li, "calling function with arguments not supported yet in javascript\n");
        return NULL;
    }
    char*script = allocprintf("%s%s", name, "()");
    ok = JS_EvaluateScript(js->cx, js->global, script, strlen(script), "__main__", 1, &rval);
    free(script);
    if(!ok) {
        language_error(js->li, "execution of function %s failed\n", name);
        return NULL;
    }

    value_t*val = jsval_to_value(js, rval);
#ifdef DEBUG
    printf("return value: ");
    value_dump(val);
    printf("\n");
#endif
    return val;
}

void destroy_js(language_t* li)
{
    js_internal_t*js = (js_internal_t*)li->internal;
    JS_DestroyContext(js->cx);
    JS_DestroyRuntime(js->rt);
    JS_ShutDown();
    free(js->buffer);
    free(js);
    free(li);
}

language_t* javascript_interpreter_new()
{
    language_t * li = calloc(1, sizeof(language_t));
#ifdef DEBUG
    li->magic = LANG_MAGIC;
#endif
    li->name = "js";
    li->compile_script = compile_script_js;
    li->is_function = is_function_js;
    li->call_function = call_function_js;
    li->define_function = define_function_js;
    li->define_constant = define_constant_js;
    li->destroy = destroy_js;
    li->internal = calloc(1, sizeof(js_internal_t));
    js_internal_t*js = (js_internal_t*)li->internal;
    js->li = li;
    init_js(js);
    return li;
}

