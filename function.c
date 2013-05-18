#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ffi.h>
#include "util.h"
#include "function.h"

value_t empty_array = {
    type: TYPE_ARRAY,
};
value_t void_value = {
    type: TYPE_VOID,
};

typedef struct {
    int size;
} array_internal_t;

int count_function_defs(c_function_def_t*methods) 
{
    int i = 0;
    while(methods[i].call) {
        i++;
    }
    return i;
}

static int _parse_type(const char*s, type_t*type)
{
    const char*start = s;
    switch(*s) {
        case '\0': *type = TYPE_VOID; break;
        case 'b': *type = TYPE_BOOLEAN; break;
        case 'i': *type = TYPE_INT32; break;
        case 'f': *type = TYPE_FLOAT32; break;
        default:        
        case 's': *type = TYPE_STRING; break;
        case '[': *type = TYPE_ARRAY; break;
    }
    s++;
    return s - start;
}

int function_count_args(c_function_def_t*method) 
{
    const char*a;
    int count = 0;
    for(a=method->params; *a; a++) {
        type_t type;
        count += _parse_type(a, &type);
    }
    return count;
}

const char* type_to_string(type_t type)
{
    switch(type) {
        case TYPE_VOID:
            return "void";
        break;
        case TYPE_FLOAT32:
            return "float32";
        break;
        case TYPE_INT32:
            return "int32";
        break;
        case TYPE_BOOLEAN:
            return "boolean";
        break;
        case TYPE_STRING:
            return "string";
        break;
        case TYPE_ARRAY:
            return "array";
        break;
        default:
            return "<unknown>";
        break;
    }
}

value_t* value_clone(const value_t*src)
{
    switch(src->type) {
        case TYPE_VOID:
        break;
        case TYPE_FLOAT32:
            return value_new_float32(src->f32);
        break;
        case TYPE_INT32:
            return value_new_int32(src->i32);
        break;
        case TYPE_BOOLEAN:
            return value_new_boolean(src->b);
        break;
        case TYPE_STRING:
            return value_new_string(src->str);
        break;
        case TYPE_ARRAY: {
            value_t*array = array_new();
            int i;
            for(i=0;i<src->length;i++) {
                array_append(array, value_clone(src->data[i]));
            }
            return array;
        }
        break;
    }
    return NULL; 
}

static ffi_type* _type_to_ffi_type(type_t type)
{
    switch(type) {
        case TYPE_VOID:
            return &ffi_type_void;
        break;
        case TYPE_FLOAT32:
            return &ffi_type_float;
        break;
        case TYPE_INT32:
            return &ffi_type_sint32;
        break;
        case TYPE_BOOLEAN:
            return &ffi_type_uint8;
        break;
        case TYPE_STRING:
        case TYPE_ARRAY:
            return &ffi_type_pointer;
        break;
        default:
            assert(0);
        break;
    }
}

ffi_type * function_ffi_rtype(c_function_def_t*method)
{
    type_t type;
    _parse_type(method->ret, &type);
    log_dbg("[ffi] ret type: \"%s\" %s", method->ret, type_to_string(type));
    return _type_to_ffi_type(type);
}

ffi_type ** function_ffi_args_plus_one(c_function_def_t*method)
{
    int num_params = function_count_args(method) + 1;
    ffi_type **atypes = malloc(sizeof(ffi_type*) * num_params);
    const char*a = method->params;
    atypes[0] = &ffi_type_pointer;
    int i = 1;
    while(*a) {
        type_t type;
        a += _parse_type(a, &type);
        atypes[i] = _type_to_ffi_type(type);
        i++;
    }
    assert(i == num_params);
    return atypes;
}

function_signature_t* function_get_signature(c_function_def_t*f)
{
    function_signature_t*sig = malloc(sizeof(function_signature_t));

    const char*a;
    int i;

    a = f->params;
    sig->num_params = 0;
    while(*a) {
        type_t type;
        a += _parse_type(a, &type);
        sig->num_params++;
    }

    sig->param = malloc(sizeof(type_t)*sig->num_params);
    a = f->params;
    i = 0;
    while(*a) {
        type_t type;
        a += _parse_type(a, &sig->param[i++]);
    }

    _parse_type(f->ret, &sig->ret);

    return sig;
}

void function_signature_dump(function_signature_t*sig)
{
    printf("sig: (");
    int i;
    for(i=0;i<sig->num_params;i++) {
        if(i>0)
            printf(", ");
        printf("%s", type_to_string(sig->param[i]));
    }
    printf("):%s\n", type_to_string(sig->ret));
}

void function_signature_destroy(function_signature_t*sig)
{
    free(sig->param);
    free(sig);
}

static const char* _ffi_arg_type(const ffi_type*t)
{
    if(t == &ffi_type_void)
        return "void";
    else if(t == &ffi_type_void)
        return "void";
    else if(t == &ffi_type_uint8)
        return "uint8";
    else if(t == &ffi_type_sint8)
        return "sint8";
    else if(t == &ffi_type_uint16)
        return "uint16";
    else if(t == &ffi_type_sint16)
        return "sint16";
    else if(t == &ffi_type_uint32)
        return "uint32";
    else if(t == &ffi_type_sint32)
        return "sint32";
    else if(t == &ffi_type_uint64)
        return "uint64";
    else if(t == &ffi_type_sint64)
        return "sint64";
    else if(t == &ffi_type_float)
        return "float";
    else if(t == &ffi_type_double)
        return "double";
    else if(t == &ffi_type_pointer)
        return "pointer";
    else
        return "<unknown>";
}
static void dump_ffi_call(const ffi_cif*cif)
{
    printf("(");
    int i;
    for(i=0;i<cif->nargs;i++) {
        if(i>0)
            printf(", ");
        printf("%s", _ffi_arg_type(cif->arg_types[i]));
    }
    printf("):%s\n", _ffi_arg_type(cif->rtype));
}

value_t* cfunction_call(value_t*self, value_t*_args)
{
    c_function_def_t*f = self->internal;

    function_signature_t*sig = function_get_signature(f);

    ffi_cif cif;
    ffi_type **atypes = function_ffi_args_plus_one(f);
    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, sig->num_params + 1, 
                                     function_ffi_rtype(f), 
                                     atypes);

    if(status != FFI_OK) {
        fprintf(stderr, "ffi_prep_cif failed\n");
        return NULL;
    }

    if(_args->type != TYPE_ARRAY) {
        fprintf(stderr, "function parameters must be an array\n");
        return NULL;
    }
    if(sig->num_params != _args->length) {
        fprintf(stderr, "wrong number of arguments: expected %d, got %d\n", sig->num_params, _args->length);
        return NULL;
    }

#ifdef DEBUG
    printf("[ffi] ");function_signature_dump(sig);
    printf("[ffi] args: ");value_dump(_args);printf("\n");
#endif
    union {
        int32_t i32;
        float f32;
        bool b;
        void*ptr;
#define TMP_STR_SIZE 32
        char tmp_str[TMP_STR_SIZE];
    } args_data[_args->length+1], ret_raw;

    void**ffi_args = alloca(sizeof(void*) * (_args->length + 1));

    int i;

    ffi_args[0] = &f->context;

    for(i=0;i<_args->length;i++) {
        value_t*o = _args->data[i];

        type_t t = sig->param[i];
        ffi_args[i+1] = &args_data[i+1];

        bool error = false;
        switch(_args->data[i]->type) {
            case TYPE_FLOAT32: {
                float v = o->f32;
                if(t == TYPE_FLOAT32) {
                    args_data[i+1].f32 = v;
                } else if(t == TYPE_INT32) {
                    args_data[i+1].i32 = (int)v;
                } else if(t == TYPE_BOOLEAN) {
                    args_data[i+1].b = (int)v;
                } else if(t == TYPE_STRING) {
                    char*str = args_data[i+i].tmp_str;
                    snprintf(str, TMP_STR_SIZE, "%f", v);
                    args_data[i+1].ptr = str;
                } else {
                    error = true;
                }
            }
            break;
            case TYPE_INT32: {
                int v = o->i32;
                if(t == TYPE_FLOAT32) {
                    args_data[i+1].f32 = v;
                } else if(t == TYPE_INT32) {
                    args_data[i+1].i32 = v;
                } else if(t == TYPE_BOOLEAN) {
                    args_data[i+1].b = v;
                } else if(t == TYPE_STRING) {
                    char*str = args_data[i+i].tmp_str;
                    snprintf(str, TMP_STR_SIZE, "%d", v);
                    args_data[i+1].ptr = str;
                } else {
                    error = true;
                }
            }
            break;
            case TYPE_BOOLEAN: {
                bool v = o->b;
                if(t == TYPE_FLOAT32) {
                    args_data[i+1].f32 = v;
                } else if(t == TYPE_INT32) {
                    args_data[i+1].i32 = v;
                } else if(t == TYPE_BOOLEAN) {
                    args_data[i+1].b = v;
                } else if(t == TYPE_STRING) {
                    args_data[i+1].ptr = v?"true":"false";
                } else {
                    error = true;
                }
            }
            break;
            case TYPE_STRING: {
                char* v = o->str;
                if(t == TYPE_STRING) {
                    args_data[i+1].ptr = v;
                } else {
                    error = true;
                }
            }
            break;
            case TYPE_VOID: {
                if(t == TYPE_VOID) {
                    args_data[i+1].ptr = NULL;
                } else {
                    error = true;
                }
            }
            break;
            case TYPE_ARRAY: {
                value_t* v = o;
                if(t == TYPE_ARRAY) {
                    args_data[i+1].ptr = v;
                } else if(t == TYPE_STRING) {
                    char*str = args_data[i+i].tmp_str;
                    snprintf(str, TMP_STR_SIZE, "<array, %d items>", v->length);
                    args_data[i+1].ptr = str;
                } else {
                    error = true;
                }
            }
            break;
            default: {
                error = true;
            }
            break;
        }
        if(error) {
            fprintf(stderr, "Can't convert parameter %d from %s to %s\n",
                    i+1, 
                    type_to_string(o->type), 
                    type_to_string(t));
            function_signature_destroy(sig);
            free(atypes);
            return NULL;
        }
    }

#ifdef DEBUG
    printf("[ffi] call: "); dump_ffi_call(&cif);
#endif
    ffi_call(&cif, f->call, &ret_raw, ffi_args);

    free(atypes);
    type_t ret_type = sig->ret;
    
    value_t* ret = NULL;
    switch(ret_type) {
        case TYPE_VOID:
            ret = value_new_void();
        break;
        case TYPE_FLOAT32:
            ret = value_new_float32(ret_raw.f32);
        break;
        case TYPE_INT32:
            ret = value_new_int32(ret_raw.i32);
        break;
        case TYPE_BOOLEAN:
            ret = value_new_boolean(ret_raw.b);
        break;
        case TYPE_STRING:
            ret = value_new_string(ret_raw.ptr);
        break;
        case TYPE_ARRAY:
            ret = (value_t*)ret_raw.ptr;
        break;
        default:
            assert(0);
        break;
    }
#ifdef DEBUG
    printf("[ffi] call returning: "); value_dump(ret);
    printf("\n");
#endif
    function_signature_destroy(sig);
    return ret;
}

void value_dump(value_t*v)
{
    if(v == NULL) {
        printf("NULL value (error)");
        return;
    }

    switch(v->type) {
        case TYPE_VOID:
            printf("void");
        break;
        case TYPE_FLOAT32:
            printf("(f32)%f", v->f32);
        break;
        case TYPE_INT32:
            printf("(i32)%d", v->i32);
        break;
        case TYPE_BOOLEAN:
            printf("(bool)%d", v->b);
        break;
        case TYPE_STRING:
            printf("\"%s\"", v->str);
        break;
        case TYPE_ARRAY: {
            int i;
            printf("[");
            for(i=0;i<v->length;i++) {
                if(i>0)
                    printf(", ");
                value_dump(v->data[i]);
            }
            printf("]");
        }
        break;
        default: {
            printf("type<%d>", v->type);
        }
        break;
    }
}
void array_append(value_t*array, value_t* value)
{
    assert(array->type == TYPE_ARRAY);
    array_internal_t*internal = array->internal;
    if(internal->size <= array->length) {
        internal->size |= 3;
        internal->size <<= 1;
        internal->size += 1;
        if(!array->data) {
            array->data = malloc(internal->size * sizeof(void*));
        } else {
            array->data = realloc(array->data, internal->size * sizeof(void*));
        }
    }
    array->data[array->length++] = value;
}
void array_append_int32(value_t*array, int32_t i32)
{
    array_append(array, value_new_int32(i32));
}
void array_append_float32(value_t*array, float f32)
{
    array_append(array, value_new_float32(f32));
}
void array_append_string(value_t*array, char* str)
{
    array_append(array, value_new_string(str));
}
void array_append_boolean(value_t*array, bool b)
{
    array_append(array, value_new_boolean(b));
}
void array_destroy(value_t*array)
{
    assert(array->type == TYPE_ARRAY);
    value_destroy(array);
}

void value_destroy(value_t*v)
{
    if(v->destroy) {
        v->destroy(v);
    } else {
        free(v);
    }
}

static void value_destroy_simple(value_t*v)
{
    free(v);
}

static void value_destroy_string(value_t*v)
{
    free(v->str);
    free(v);
}

static void value_destroy_array(value_t*v)
{
    array_internal_t*internal = v->internal;
    int i;
    for(i=0;i<v->length;i++) {
       value_destroy(v->data[i]);
       v->data[i] = NULL;
    }
    free(v->data);
    free(v->internal);
    free(v);
}

static void value_destroy_cfunction(value_t*v)
{
    c_function_def_t*f = (c_function_def_t*)v->internal;
    free(f->params);
    free(f->ret);
    free(v->internal);
    free(v);
}

value_t* value_new_int32(int32_t i32)
{
    value_t*v = calloc(sizeof(value_t),1);
    v->destroy = value_destroy_simple;
    v->type = TYPE_INT32;
    v->i32 = i32;
    return v;
}

value_t* value_new_float32(float f32)
{
    value_t*v = calloc(sizeof(value_t),1);
    v->destroy = value_destroy_simple;
    v->type = TYPE_FLOAT32;
    v->f32 = f32;
    return v;
}

value_t* value_new_boolean(bool b)
{
    value_t*v = calloc(sizeof(value_t),1);
    v->destroy = value_destroy_simple;
    v->type = TYPE_BOOLEAN;
    v->b = b;
    return v;
}

value_t* value_new_string(const char* s)
{
    value_t*v = calloc(sizeof(value_t),1);
    v->destroy = value_destroy_string;
    v->type = TYPE_STRING;
    v->str = strdup(s);
    return v;
}

value_t* value_new_void()
{
    value_t*v = calloc(sizeof(value_t),1);
    v->destroy = value_destroy_simple;
    v->type = TYPE_VOID;
    return v;
}

value_t* value_new_array()
{
    value_t*v = calloc(sizeof(value_t),1);
    v->type = TYPE_ARRAY;
    v->internal = calloc(sizeof(array_internal_t),1);
    v->destroy = value_destroy_array;
    return v;
}

value_t* array_new()
{
    return value_new_array();
}

value_t* value_new_cfunction(void (*call)(), void*context, char*params, char*ret)
{
    c_function_def_t*f = calloc(sizeof(c_function_def_t), 1);
    f->name = NULL;
    f->call = call;
    f->context = context;
    f->params = strdup(params);
    f->ret = strdup(ret);

    value_t*v = calloc(sizeof(value_t),1);
    v->destroy = value_destroy_cfunction;
    v->type = TYPE_FUNCTION;
    v->internal = f;
    v->call = cfunction_call;
    v->num_params = function_count_args(f);
    return v;
}

int value_to_int(value_t*v)
{
    switch(v->type) {
        case TYPE_VOID:
            return 0;
        break;
        case TYPE_FLOAT32:
            return (int)v->f32;
        break;
        case TYPE_INT32:
            return v->i32;
        break;
        case TYPE_BOOLEAN:
            return v->b;
        break;
        case TYPE_STRING:
            return atoi(v->str);
        break;
        default:
            return -1;
    }
}

