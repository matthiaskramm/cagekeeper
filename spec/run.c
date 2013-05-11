#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>
#include "../language.h"

static void trace(void*context, char*s)
{
    printf("%s\n", s);
}
static value_t* get_array(void*context, int width, int height)
{
    value_t*columns = array_new();
    int x;
    for(x=0;x<width;x++) {
        int y;
        value_t*column = array_new();
        for(y=0;y<height;y++) {
            array_append_int32(column, x*10+y);
        }
        array_append(columns, column);
    }
    return columns;
}
static int add2(void*context, int x, int y)
{
    return x+y;
}
static int add3(void*context, int x, int y, int z)
{
    return x+y+z;
}
static float fadd2(void*context, float x, float y)
{
    return x+y;
}
static float fadd3(void*context, float x, float y, float z)
{
    return x+y+z;
}
static char* concat_strings(void*context, char*s1, char*s2)
{
    int l1 = strlen(s1);
    int l2 = strlen(s2);
    char*tmp = malloc(l1+l2+1);
    memcpy(tmp, s1, l1);
    memcpy(tmp+l1, s2, l2);
    tmp[l1+l2] = 0;
    return tmp;
}
static value_t* concat_arrays(void*context, value_t*array1, value_t*array2)
{
    int i;
    value_t*a = array_new();
    for(i=0;i<array1->length;i++) {
        array_append(a, value_clone(array1->data[i]));
    }
    for(i=0;i<array2->length;i++) {
        array_append(a, value_clone(array2->data[i]));
    }
    return a;
}
static bool negate(void*context, bool b)
{
    return !b;
}

c_function_def_t functions[] = {
    {"trace", (fptr_t)trace, NULL, "s",""},
    {"get_array", (fptr_t)get_array, NULL, "ii","["},
    {"add2", (fptr_t)add2, NULL, "ii", "i"},
    {"add3", (fptr_t)add3, NULL, "iii", "i"},
    {"fadd2", (fptr_t)fadd2, NULL, "ff", "f"},
    {"fadd3", (fptr_t)fadd3, NULL, "fff", "f"},
    {"concat_strings", (fptr_t)concat_strings, NULL, "ss", "s"},
    {"concat_arrays", (fptr_t)concat_arrays, NULL, "[[", "["},
    {"negate", (fptr_t)negate, NULL, "b", "b"},
    {NULL,NULL,NULL,NULL}
};

int main(int argn, char*argv[])
{
    char*program = argv[0];
    bool sandbox = true;

    int i,j=0;
    for(i=1;i<argn;i++) {
        if(argv[i][0]=='-') {
            switch(argv[i][1]) {
                case 'u':
                    sandbox = false;
                break;
            }
        } else {
            argv[j++] = argv[i];
        }
    }
    argn = j;

    if(argn < 1) {
        printf("Usage:\n\t%s <program>\n", program);
        exit(1);
    }

    char*filename = argv[0];

    language_t*l;
    if(sandbox) {
        l = interpreter_by_extension(filename);
    } else {
        l = unsafe_interpreter_by_extension(filename);
    }
    if(!l) {
        fprintf(stderr, "Couldn't initialize %sinterpreter for %s\n", l?"sandboxed":"", filename);
        return 1;
    }

    define_c_functions(l, functions);
    l->define_constant(l, "global_int", value_new_int32(3));
    l->define_constant(l, "global_array", value_new_array());
    l->define_constant(l, "global_boolean", value_new_boolean(true));
    l->define_constant(l, "global_float", value_new_float32(3.0));
    l->define_constant(l, "global_string", value_new_string("foobar"));

    char* script = read_file(filename);
    if(!script) {
        fprintf(stderr, "Error reading script %s\n", filename);
        return 1;
    }

    bool compiled = l->compile_script(l, script);
    if(!compiled) {
        fprintf(stderr, "Error compiling script\n");
        l->destroy(l);
        return 1;
    }
    
    value_t*ret = NULL;
    if(l->is_function(l, "call_noargs")) {
        value_t*args = value_new_array();
        ret = l->call_function(l, "call_noargs", args);
        value_destroy(args);
    }
    if(l->is_function(l, "call_int")) {
        value_t*args = value_new_array();
        array_append_int32(args, 0);
        ret = l->call_function(l, "call_int", args);
        value_destroy(args);
    }
    if(l->is_function(l, "call_float")) {
        value_t*args = value_new_array();
        array_append_float32(args, 0);
        ret = l->call_function(l, "call_float", args);
        value_destroy(args);
    }
    if(l->is_function(l, "call_string")) {
        value_t*args = value_new_array();
        array_append_string(args, "foobar");
        ret = l->call_function(l, "call_string", args);
        value_destroy(args);
    }
    if(l->is_function(l, "call_boolean")) {
        value_t*args = value_new_array();
        array_append_boolean(args, false);
        ret = l->call_function(l, "call_boolean", args);
        value_destroy(args);
    }
    if(l->is_function(l, "call_array")) {
        value_t*args = value_new_array();
        value_t*a = value_new_array();
        array_append(a, value_new_int32(1));
        array_append(a, value_new_int32(2));
        array_append(a, value_new_int32(3));
        array_append(args, a);
        ret = l->call_function(l, "call_array", args);
        value_destroy(args);
    }
    if(l->is_function(l, "call_int_and_float_and_string")) {
        value_t*args = value_new_array();
        array_append(args, value_new_int32(1));
        array_append(args, value_new_float32(2));
        array_append(args, value_new_string("foobar"));
        ret = l->call_function(l, "call_int_and_float_and_string", args);
        value_destroy(args);
    }
    if(l->is_function(l, "call_boolean_and_array")) {
        value_t*args = value_new_array();
        array_append(args, value_new_boolean(true));
        array_append(args, value_new_array());
        ret = l->call_function(l, "call_boolean_and_array", args);
        value_destroy(args);
    }

    if(l->is_function(l, "test")) {
        ret = l->call_function(l, "test", NO_ARGS);
    }

    l->destroy(l);

    if(ret && ret->type == TYPE_STRING) {
        fputs(ret->str, stdout);
        fputc('\n', stdout);
        if(!strcmp(ret->str, "ok"))
            return 0;
        else
            return 1;
    } else {
        value_dump(ret);
        fputc('\n', stdout);
        return 1;
    }
}
