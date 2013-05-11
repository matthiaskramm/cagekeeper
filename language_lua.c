#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <errno.h>
#include "language.h"

typedef struct _lua_internal {
    language_t*li;
    lua_State* state;
    int method_count;
} lua_internal_t;

static const luaL_reg lualibs[] =
{
    {"base", luaopen_base}, // dofile etc.
    {"table", luaopen_table},
    {"math", luaopen_math},
    {"string", luaopen_string},
    {NULL,   NULL}
};

static void openlualibs(lua_State *l)
{
    const luaL_reg *lib;
    for(lib = lualibs; lib->func != NULL; lib++)
    {
        lib->func(l);
        lua_settop(l, 0);
    }
}

static void dump_stack(lua_State *l) {
    int i;
    int len = lua_gettop(l);
    for(i=1;i<=len;i++) {
        printf("[%d] ", i);
        switch(lua_type(l, i)) {
            case LUA_TNONE:
                printf("none");
            break;
            case LUA_TNIL:
                printf("nil");
            break;
            case LUA_TBOOLEAN:
                printf("%s", lua_toboolean(l, i) ? "true" : "false");
            break;
            case LUA_TLIGHTUSERDATA:
                printf("<lightuserdata>");
            break;
            case LUA_TNUMBER:
                printf("%.1f", lua_tonumber(l, i));
            break;
            case LUA_TSTRING:
                printf("\"%s\"", lua_tostring(l, i));
            break;
            case LUA_TTABLE:
                printf("<table>");
            break;
            case LUA_TFUNCTION:
                printf("<function>");
            break;
            case LUA_TUSERDATA:
                printf("<userdata>");
            break;
            case LUA_TTHREAD:
                printf("<thread>");
            break;
            default:
                printf("?");
            break;
        }
        printf("\n");
    }
}


static void show_error(language_t*li, lua_State *l)
{
    const char *s = lua_tolstring(l, -1, NULL);
    printf("%s\n", s);

    if(li->error_file) {
        fprintf(li->error_file, "%s\n", s);
    }
}

static bool initialize_lua(language_t*li, size_t mem_size)
{
    if(li->internal)
        return true; // already initialized

    li->internal = calloc(1, sizeof(lua_internal_t));
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua->li = li;

    lua_State*l = lua->state = lua_open();
    openlualibs(l);

    return true;
}

static bool compile_script_lua(language_t*li, const char*script)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_State*l = lua->state;

    int error = luaL_loadbuffer(l, script, strlen(script), "@file.lua");
    if(!error) {
        error = lua_pcall(l, 0, LUA_MULTRET, 0);
    }
    if(error) {
        show_error(li, l);
        language_error(li, "Couldn't compile: %d\n", error);
        return false;
    }
    return true;
}

static void push_value(lua_State*l, value_t*value)
{
    int i;
    switch(value->type) {
        case TYPE_VOID:
            lua_pushnil(l);
        break;
        case TYPE_FLOAT32:
            lua_pushnumber(l, value->f32);
        break;
        case TYPE_INT32:
            lua_pushinteger(l, value->i32);
        break;
        case TYPE_BOOLEAN:
            lua_pushboolean(l, value->b);
        break;
        case TYPE_STRING: {
            lua_pushstring(l, value->str);
        }
        break;
        case TYPE_ARRAY: {
            lua_newtable(l);
            for(i=0;i<value->length;i++) {
                lua_pushinteger(l, i+1);
                push_value(l, value->data[i]);
                lua_settable(l, -3);
            }
        }
        break;
        default: {
            lua_pushnil(l);
        }
    }
}

static value_t* lua_to_value(language_t*li, int idx)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_State*l = lua->state;

    if(lua_gettop(l)+idx < 0) {
        language_error(li, "[lua] Stack overflow: idx=%d, top=%d\n", idx, lua_gettop(l));
        return NULL;
    } else if(lua_isnoneornil(l, idx)) {
        return value_new_void();
    } else if(lua_isboolean(l, idx)) {
        return value_new_boolean(lua_toboolean(l, idx));
    } else if(lua_isnumber(l, idx)) {
        return value_new_float32(lua_tonumber(l, idx));
    } else if(lua_isnumber(l, idx)) {
        return value_new_int32(lua_tointeger(l, idx));
    } else if(lua_isstring(l, idx)) {
        value_t*v = value_new_string(lua_tostring(l, idx));
        return v;
    } else if(lua_istable(l, idx)) {
        value_t*array = array_new();
        int i;
        for(i=0;;i++) {
            lua_pushinteger(l, i+1);
            lua_gettable(l, idx<0?idx-1:idx);
            if(lua_isnil(l, -1)) {
                lua_pop(l, 1);
                break;
            }
            array_append(array, lua_to_value(li, -1));
            lua_pop(l, 1);
        }
        return array;
    }
    language_error(li, "Don't know how to process lua type: %d\n");
    return NULL;
}

static void define_constant_lua(struct _language*li, const char*name, value_t*value)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_State*l = lua->state;

    push_value(l, value);
    lua_setglobal(l, name);

}

typedef struct {
    language_t*li;
    value_t*f;
    const char*name;
} function_data_t;

static int lua_function_proxy(lua_State*l)
{
    assert(lua_islightuserdata(l, lua_upvalueindex(1)));
    function_data_t*data = (function_data_t*)lua_touserdata(l, lua_upvalueindex(1));
    value_t*f = data->f;
    int i;
    dbg("[lua] lua calls function %s (%d parameters)", data->name, f->num_params);

    value_t*args = array_new();
    int j = -f->num_params;
    for(i=0;i<f->num_params;i++) {
        value_t*a = lua_to_value(data->li, j++);
        if(a == NULL) {
            luaL_argerror(l, i+1, "invalid or missing value");
        }
        array_append(args, a);
    }
    value_t*ret = f->call(f, args);
    value_destroy(args);

    push_value(l, ret);
    value_destroy(ret);
    return 1;
}

static void define_function_lua(struct _language*li, const char*name, function_t*f)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_State*l = lua->state;
    printf("[lua] defining function %s (%d parameters)\n", name, f->num_params);

    function_data_t*data = calloc(sizeof(function_data_t),1);
    data->f = f;
    data->name = name;
    data->li = li;

    lua_pushlightuserdata(l, (void*)data);
    lua_pushcclosure(l, lua_function_proxy, 1);
    lua_setglobal(l, name);
}

static bool is_function_lua(language_t*li, const char*name)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_State*l = lua->state;

    printf("[lua] is_function(%s)\n", name);

    lua_getfield(l, LUA_GLOBALSINDEX, name);
    bool ret = lua_isfunction(l, -1);
    lua_pop(l, 1);

    return ret;
}

static value_t* call_function_lua(language_t*li, const char*name, value_t*args)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_State*l = lua->state;

    lua_getfield(l, LUA_GLOBALSINDEX, name);

    if(!lua_isfunction(l, -1)) {
        language_error(li, "%s is not a function", name);
        return NULL;
    }

    int i;
    for(i=0;i<args->length;i++) {
        push_value(l, args->data[i]);
    }

    int error = lua_pcall(l, /*nargs*/args->length, /*nresults*/1, 0);
    if(error) {
        show_error(li, l);
        language_error(li, "Error calling function %s: %d\n", name, error);
        return NULL;
    }

    value_t*ret = lua_to_value(li, -1);
    lua_pop(l, 1);

    return ret;
}

static void destroy_lua(language_t* li)
{
    if(li->internal) {
        lua_internal_t*lua = (lua_internal_t*)li->internal;
        lua_close(lua->state);
        free(lua);
    }
    free(li);
}

language_t* lua_interpreter_new()
{
    language_t * li = calloc(1, sizeof(language_t));
    li->name = "lua";
    li->initialize = initialize_lua;
    li->compile_script = compile_script_lua;
    li->is_function = is_function_lua;
    li->call_function = call_function_lua;
    li->define_function = define_function_lua;
    li->define_constant = define_constant_lua;
    li->destroy = destroy_lua;
    return li;
}
