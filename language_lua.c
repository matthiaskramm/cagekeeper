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

static bool define_function_lua(language_t*li, const char*script)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_State*l = lua->state;

    int error = luaL_loadbuffer(l, script, strlen(script), "@file.lua");
    if(!error) {
        error = lua_pcall(l, 0, LUA_MULTRET, 0);
    }
    if(error) {
        show_error(li, l);
        printf("Couldn't compile: %d\n", error);
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

static int call_function_lua(language_t*li, const char*name)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_State*l = lua->state;
    lua_getfield(l, LUA_GLOBALSINDEX, name);
    int error = lua_pcall(l, /*nargs*/0, /*nresults*/1, 0);
    if(error) {
        show_error(li, l);
        printf("Couldn't run: %d\n", error);
        return -1;
    }
    int ret = lua_tointeger(l, -1);
    lua_pop(l, 1);
    return ret;
}

static bool call_void_function_lua(language_t*li, const char*name)
{
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua_State*l = lua->state;
    lua_getfield(l, LUA_GLOBALSINDEX, name);
    int error = lua_pcall(l, /*nargs*/0, /*nresults*/0, 0);
    if(error) {
        show_error(li, l);
        printf("Couldn't run: %d\n", error);
        return false;
    }
    return true;
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
    li->define_function = define_function_lua;
    li->is_function = is_function_lua;
    li->call_function = call_function_lua;
    li->call_void_function = call_void_function_lua;
    li->destroy = destroy_lua;
    li->internal = calloc(1, sizeof(lua_internal_t));
    lua_internal_t*lua = (lua_internal_t*)li->internal;
    lua->li = li;
    init_lua(lua);
    return li;
}
