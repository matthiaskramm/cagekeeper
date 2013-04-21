#ifndef __function_h__
#define  __function_h__

#include <stdbool.h>
#include <sys/types.h>

typedef enum _type {
    TYPE_VOID,
    TYPE_FLOAT32,
    TYPE_INT32,
    TYPE_BOOLEAN,
    TYPE_STRING,
    TYPE_ARRAY,
    TYPE_FUNCTION,
} type_t;

typedef struct _value value_t;
typedef struct _value function_t;

struct _value {
    type_t type;
    void*internal;
    union {
        int32_t i32;
        float f32;
        bool b;
        char* str;
        value_t* (*call)(value_t*v, value_t*params);
        struct {
            int length;
            struct _value**data;
        };
    };
    void (*destroy)(value_t*destroy);
};

typedef void(*fptr_t)();

value_t* value_new_void();
value_t* value_new_string(const char* s);
value_t* value_new_boolean(bool b);
value_t* value_new_float32(float f32);
value_t* value_new_int32(int32_t i32);
value_t* value_new_cfunction(fptr_t call, void*context, char*args, char*ret);

void value_dump(value_t*v);
void value_destroy(value_t*v);

int value_to_int(value_t*v);

value_t* array_new();
void array_append(value_t*array, value_t* value);
void array_append_int32(value_t*array, int32_t i32);
void array_append_float32(value_t*array, float f32);
void array_append_string(value_t*array, char* string);
void array_append_boolean(value_t*array, bool string);
void array_destroy(value_t*array);

#define array_append_value array_append 
#define cfunction_new value_new_cfunction

extern value_t empty_array;
extern value_t void_value;
#define NO_ARGS (&empty_array)
#define VOID_VALUE (&void_value)

typedef struct _c_function_def {
    char*name;
    fptr_t call;
    void*context;
    char*params;
    char*ret;
} c_function_def_t;

typedef struct _function_signature {
    const char* name;
    int num_params;
    type_t*param;
    type_t ret;
} function_signature_t;

function_signature_t* function_get_signature(c_function_def_t*f);
void function_signature_destroy(function_signature_t*sig);

int count_function_defs(c_function_def_t*f);
int function_count_args(c_function_def_t*f);

#endif
