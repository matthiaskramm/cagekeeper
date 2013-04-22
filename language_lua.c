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
} lua_internal_t;

static const luaL_reg lualibs[] =
{
    //{"base", luaopen_base}, // dofile etc.
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

static void show_error(language_t*li, lua_State *l)
{
    const char *s = lua_tolstring(l, -1, NULL);

    if(li->verbosity > 0) {
        printf("%s\n", s);
    }
    if(li->error_file) {
        fprintf(li->error_file, "%s\n", s);
    }
}

bool init_lua(lua_internal_t*lua)
{
    lua_State*l = lua->state = lua_open();
    openlualibs(l);
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

static bool is_function_lua(language_t*li, const char*name)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_State*l = lua->state;

    lua_getfield(l, LUA_GLOBALSINDEX, name);
    bool ret = !lua_isnil(l, -1);
    lua_pop(l, 1);
    return ret;
}

static value_t* lua_to_value(lua_State*l)
    if(lua_isnoneornil(l, -1)) {
        return value_new_void();
    } else if(lua_isboolean(l, -1)) {
        return value_new_boolean(lua_toboolean(l, -1));
    } else if(lua_isnumber(l, -1)) {
        return value_new_float32(lua_tonumber(l, -1));
    } else if(lua_isnumber(l, -1)) {
        return value_new_int32(lua_tointeger(l, -1));
    } else if(lua_isstring(l, -1)) {
        return value_new_string(lua_tostring(l, -1));
    } else if(lua_istable(l, -1)) {
        /* FIXME */
    }
    return NULL;
}

static value_t* call_function_lua(language_t*li, const char*name, value_t*args)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_State*l = lua->state;
    lua_getfield(l, LUA_GLOBALSINDEX, name);
    if(lua_isnil(l, -1)) {
        language_error(li, "%s is not a function", name);
        return NULL;
    }

    int error = lua_pcall(l, /*nargs*/args->length, /*nresults*/1, 0);
    if(error) {
        show_error(li, l);
        language_error(li, "Couldn't run: %d\n", error);
        return NULL;
    }

    value_t*ret = lua_to_value(l);
    lua_pop(l, 1);
    return ret;
}

static void destroy_lua(language_t* li)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_close(lua->state);
    free(lua);
    free(li);
}

language_t* lua_interpreter_new()
{
    language_t * li = calloc(1, sizeof(language_t));
#ifdef DEBUG
    li->magic = LANG_MAGIC;
#endif
    li->name = "lua";
    li->compile_script = compile_script_lua;
    li->is_function = is_function_lua;
    li->call_function = call_function_lua;
    li->destroy = destroy_lua;
    li->internal = calloc(1, sizeof(lua_internal_t));
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua->li = li;
    init_lua(lua);
    return li;
}
