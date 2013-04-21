#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>
#include "../language.h"

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

c_function_def_t functions[] = {
    {"get_array", (fptr_t)get_array, NULL, "ii","["},
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

    language_t*l = interpreter_by_extension(filename);
    if(!l) {
        fprintf(stderr, "Couldn't initialize interpreter for %s\n", filename);
        return 1;
    }

    if(sandbox) {
        l = wrap_sandbox(l);
        if(!l) {
            fprintf(stderr, "Couldn't initialize sandbox\n");
            return 1;
        }
    }

    define_c_functions(l, functions);

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
    
    value_t*args = value_new_array();

    value_t*ret = NULL;
    if(l->is_function(l, "test")) {
        ret = l->call_function(l, "test", args);
    } else if(l->is_function(l, "test_int")) {
        array_append_int32(args, 0);
        ret = l->call_function(l, "test_int", args);
    } else if(l->is_function(l, "test_float")) {
        array_append_float32(args, 0);
        ret = l->call_function(l, "test_float", args);
    } else if(l->is_function(l, "test_string")) {
        array_append_string(args, "foobar");
        ret = l->call_function(l, "test_string", args);
    } else if(l->is_function(l, "test_boolean")) {
        array_append_boolean(args, false);
        ret = l->call_function(l, "test_boolean", args);
    } else if(l->is_function(l, "test_array")) {
        value_t*a = value_new_array();
        array_append(a, value_new_int32(1));
        array_append(a, value_new_int32(2));
        array_append(a, value_new_int32(3));
        array_append(args, a);
        ret = l->call_function(l, "test_array", args);
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
