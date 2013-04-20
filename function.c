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

int count_function_defs(function_def_t*methods) 
{
    int i = 0;
    while(methods[i].name) {
        i++;
    }
    return i;
}

int function_count_args(function_def_t*method) 
{
    const char*a;
    int count = 0;
    for(a=method->params; *a; a++) {
        if(strchr("][", *a))
            continue;
        count++;
    }
    return count;
}

static const char* _type_to_string(type_t type)
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
    while(strchr("[]", *s)) {
        s++;
    }
    s++;
    return s - start;
}


ffi_type * function_ffi_rtype(function_def_t*method)
{
    type_t type;
    _parse_type(method->ret, &type);
    dbg("[ffi] ret type: \"%s\" %s\n", method->ret, _type_to_string(type));
    return _type_to_ffi_type(type);
}

ffi_type ** function_ffi_args_plus_one(function_def_t*method)
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

function_signature_t* function_get_signature(function_def_t*f)
{
    function_signature_t*sig = malloc(sizeof(function_signature_t));

    const char*a;
    int i;

    a = f->params;
    sig->name = f->name;
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
    printf("sig: %s(", sig->name);
    int i;
    for(i=0;i<sig->num_params;i++) {
        if(i>0)
            printf(", ");
        printf("%s", _type_to_string(sig->param[i]));
    }
    printf("):%s\n", _type_to_string(sig->ret));
}

void function_signature_destroy(function_signature_t*sig)
{
    free(sig->param);
    free(sig);
}

function_signature_t* function_signature_verify(function_signature_t*sig, value_t*args)
{
    assert(args->type == TYPE_ARRAY);
    assert(sig->num_params == args->length);
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
static void dump_ffi_call(const char*name, const ffi_cif*cif)
{
    printf("%s(", name);
    int i;
    for(i=0;i<cif->nargs;i++) {
        if(i>0)
            printf(", ");
        printf("%s", _ffi_arg_type(cif->arg_types[i]));
    }
    printf("):%s\n", _ffi_arg_type(cif->rtype));
}

value_t* function_call(void*context, function_def_t*f, value_t*args)
{
    function_signature_t*sig = function_get_signature(f);

    ffi_cif cif;
    ffi_type **atypes = function_ffi_args_plus_one(f);
    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, sig->num_params + 1, 
                                     function_ffi_rtype(f), 
                                     atypes);

    if(status != FFI_OK) {
        fprintf(stderr, "ffi_prep_cif failed\n");
        exit(1);
    }

#ifdef DEBUG
    printf("[ffi] ");function_signature_dump(sig);
    printf("[ffi] args: ");value_dump(args);printf("\n");
#endif

    function_signature_verify(sig, args);

    void**ffi_args = malloc(sizeof(void*) * (args->length + 1));
    int i;
    ffi_args[0] = &context;
    for(i=0;i<args->length;i++) {
        switch(args->data[i]->type) {
            case TYPE_FLOAT32:
                ffi_args[i+1] = &args->data[i]->f32;
            break;
            case TYPE_INT32:
                ffi_args[i+1] = &args->data[i]->i32;
            break;
            case TYPE_BOOLEAN:
                ffi_args[i+1] = &args->data[i]->b;
            break;
            case TYPE_STRING:
                ffi_args[i+1] = &args->data[i]->str;
            break;
            case TYPE_VOID:
                ffi_args[i+1] = NULL;
            break;
            default:
                fprintf(stderr, "Can't convert type %d\n", args->data[i]->type);
                assert(0);
            break;
        }
    }

    union {
        int32_t i32;
        float f32;
        bool b;
        void*ptr;
    } ret_raw;

#ifdef DEBUG
    printf("[ffi] call: "); dump_ffi_call(f->name, &cif);
#endif
    ffi_call(&cif, f->call, &ret_raw, ffi_args);

    free(atypes);
    free(ffi_args);
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
    function_signature_destroy(sig);
    return ret;
}

typedef struct {
    int size;
} array_internal_t;

value_t* array_new()
{
    value_t*v = calloc(sizeof(value_t),1);
    v->type = TYPE_ARRAY;
    v->internal = calloc(sizeof(array_internal_t),1);
    return v;
}

void value_dump(value_t*v)
{
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
    switch(v->type) {
        case TYPE_STRING:
            free(v->str);
        break;
        case TYPE_ARRAY: {
            int i;
            for(i=0;i<v->length;i++) {
               value_destroy(v->data[i]);
               v->data[i] = NULL;
            }
            free(v->data);
            free(v->internal);
        }
        break;
    }
    free(v);
}

value_t* value_new_int32(int32_t i32)
{
    value_t*v = calloc(sizeof(value_t),1);
    v->type = TYPE_INT32;
    v->i32 = i32;
    return v;
}

value_t* value_new_float32(float f32)
{
    value_t*v = calloc(sizeof(value_t),1);
    v->type = TYPE_FLOAT32;
    v->f32 = f32;
    return v;
}

value_t* value_new_boolean(bool b)
{
    value_t*v = calloc(sizeof(value_t),1);
    v->type = TYPE_BOOLEAN;
    v->b = b;
    return v;
}

value_t* value_new_string(const char* s)
{
    value_t*v = calloc(sizeof(value_t),1);
    v->type = TYPE_STRING;
    v->str = strdup(s);
    return v;
}

value_t* value_new_void()
{
    value_t*v = calloc(sizeof(value_t),1);
    v->type = TYPE_VOID;
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
        default:
            return -1;
    }
}

