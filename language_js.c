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

    dict_t* jsfunction_to_function;
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

    dbg("[js] line %u: %s\n", (unsigned int) report->lineno, message);

    if(js->li->error_file) {
        fprintf(js->li->error_file, "line %u: %s\n",
            (unsigned int) report->lineno, message);
    }
}

static value_t* jsval_to_value(const js_internal_t*js, jsval v)
{
    // Also see JS_ConvertArguments()
    int32 d;
    if(JSVAL_IS_NULL(v)) {
        return value_new_void();
    } else if(JSVAL_IS_VOID(v)) {
        return value_new_void();
    } else if(JSVAL_IS_INT(v)) {
        return value_new_int32(JSVAL_TO_INT(v));
    } else if(JSVAL_IS_NUMBER(v)) {
        return value_new_float32(JSVAL_TO_DOUBLE(v));
    } else if(JSVAL_IS_STRING(v)) {
        JSString*s = JSVAL_TO_STRING(v);
        char*cstr = JS_EncodeString(js->cx, s);
        return value_new_string(cstr);
    } else if(JSVAL_IS_BOOLEAN(v)) {
        return value_new_boolean(JSVAL_TO_BOOLEAN(v));
    } else if(JSVAL_IS_OBJECT(v)) {
        JSObject * obj = JSVAL_TO_OBJECT(v);
        jsuint length;
        bool ret = JS_GetArrayLength(js->cx, obj, &length);
        if(!ret) {
            language_error(js->li, "Can't determine array length\n");
            return NULL;
        }
        value_t*a = array_new();
        int i;
        for(i=0;i<length;i++) {
            jsval entry;
            ret = JS_GetElement(js->cx, obj, i, &entry);
            if(!ret) {
                language_error(js->li, "Can't determine array length\n");
                return NULL;
            }
            array_append(a, jsval_to_value(js, entry));
        }
        return a;
    } else {
        /* TODO: arrays */
        language_error(js->li, "Can't convert javascript type to a value.\n");
        return NULL;
    }
}

static value_t* js_argv_to_args(language_t*li, JSContext *cx, uintN argc, jsval *argv)
{
    js_internal_t*js = (js_internal_t*)li->internal;

    int i;
    value_t*args = array_new();
    for(i=0;i<argc;i++) {
        array_append(args, jsval_to_value(js, argv[i]));
    }
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
                JS_SetElement(cx, array, i, &entry);
            }
            return OBJECT_TO_JSVAL(array);
        }
        break;
        default: {
            return OBJECT_TO_JSVAL(NULL);
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

    function_t*f = dict_lookup(js->jsfunction_to_function, func);
    if(!f) {
        language_error(js->li, "Internal error: Javascript tried to call native function %p (%d args), which we've never seen before.", func, argc);
        return JS_FALSE;
    }

    dbg("[js] native call");
    value_t* args = js_argv_to_args(js->li, cx, argc, argv);
    value_t* value = f->call(f, args);
    value_destroy(args);

    JS_SET_RVAL(cx, vp, value_to_jsval(cx, value));
    value_destroy(value);
    return JS_TRUE;
}

static void define_function_js(language_t*li, const char*name, function_t*f)
{
    js_internal_t*js = (js_internal_t*)li->internal;
    JSFunction*func = JS_DefineFunction(js->cx, js->global, 
                          name, 
                          js_function_proxy,
                          /*FIXME function_count_args(f),*/0,
                          0
                       );
    dict_put(js->jsfunction_to_function, func, f);
}

bool init_js(js_internal_t*js)
{
    int mem_size = 128L * 1024L * 1024L;

    dbg("[js] allocating runtime with %dMB of memory", mem_size / 1048576);

    js->rt = JS_NewRuntime(mem_size);
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
    js->jsfunction_to_function = dict_new(&ptr_type);
    return true;
}

void define_constant_js(language_t*li, const char*name, value_t* value)
{
    js_internal_t*js = (js_internal_t*)li->internal;
    jsval v = value_to_jsval(js->cx, value);
#ifdef DEBUG
    printf("[js] define constant %s=",name);
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
    dbg("[js] compiling script");
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

static value_t* call_function_js(language_t*li, const char*name, value_t* _args)
{
    js_internal_t*js = (js_internal_t*)li->internal;
    dbg("[js] calling function %s", name);
    assert(_args->type == TYPE_ARRAY);

    JSBool ok;
    jsval* args = malloc(sizeof(jsval)*_args->length);
    int i;
    for(i=0;i<_args->length;i++) {
        args[i] = value_to_jsval(js->cx, _args->data[i]);
    }
    jsval rval;
    ok = JS_CallFunctionName(js->cx, js->global, name, _args->length, args, &rval);
    if(!ok) {
        language_error(js->li, "execution of function %s failed\n", name);
        return NULL;
    }

    value_t*val = jsval_to_value(js, rval);
#ifdef DEBUG
    printf("[js] return value: ");
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

