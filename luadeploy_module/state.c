/******************************************************************************
* Copyright (C) 2014, Kevin Martin (kev82@khn.org.uk)
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/

#include <lua.h>
#include <lualib.h>

struct ldstate_userdata
{
	lua_State *state;
};

static int ldstate_mtgc(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TUSERDATA);

	struct ldstate_userdata *ud =
	 (struct ldstate_userdata *)lua_touserdata(l, 1);

	if(ud->state != NULL) {
		lua_close(ud->state);
		ud->state = NULL;
	}

	return 0;
}

static int ldstate_mtpushcode(lua_State *l) {
	lua_settop(l, 2);
	luaL_checktype(l, 1, LUA_TUSERDATA);
	luaL_checktype(l, 2, LUA_TSTRING);

	struct ldstate_userdata *ud =
	 (struct ldstate_userdata *)lua_touserdata(l, 1);

	if(lua_gettop(ud->state) != 0) {
		return luaL_error(l, "Stack must be empty to push code");
	}

	int rc = luaL_loadbufferx(ud->state, lua_tostring(l, 2),
	 lua_rawlen(l, 2), "pushcode_func", "t");
	if(rc != LUA_OK) {
		lua_pushstring(l, lua_tostring(ud->state, 1));
		lua_settop(ud->state, 0);
		return lua_error(l);
	}

	return 0;
}

static int ldstate_mtpushSearch(lua_State *l) {
	lua_settop(l, 4);
	luaL_checktype(l, 1, LUA_TUSERDATA);
	luaL_checktype(l, 2, LUA_TSTRING);
	luaL_checktype(l, 3, LUA_TSTRING);
	luaL_checktype(l, 4, LUA_TSTRING);

	struct ldstate_userdata *ud =
	 (struct ldstate_userdata *)lua_touserdata(l, 1);

	if(lua_gettop(ud->state) != 0) {
		return luaL_error(l, "Stack must be empty to push code");
	}

	lua_pushcfunction(ud->state, ldclient_request);
	lua_pushstring(ud->state, lua_tostring(l, 2));
	lua_pushstring(ud->state, lua_tostring(l, 3));
	lua_pushstring(ud->state, lua_tostring(l, 4));

	int rc = lua_pcall(ud->state, 3, LUA_MULTRET, 0);
	if(rc != LUA_OK) {
		lua_pushstring(l, lua_tostring(ud->state, 1));
		lua_settop(ud->state, 0);
		return lua_error(l);
	}

	if(lua_type(ud->state, 1) != LUA_TFUNCTION) {
		return luaL_error(l, "search didn't return a function");
	}

	return 0;
}

static int ldstate_mtrun(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TUSERDATA);

	struct ldstate_userdata *ud =
	 (struct ldstate_userdata *)lua_touserdata(l, 1);
	
	if(lua_gettop(ud->state) < 1 ||
	 lua_type(ud->state, 1) != LUA_TFUNCTION) {
		return luaL_error(l, "No function to run");
	}

	int rc = lua_pcall(ud->state, lua_gettop(ud->state) - 1, 0, 0);
	if(rc != LUA_OK) {
		lua_pushstring(l, lua_tostring(ud->state, 1));
		lua_settop(ud->state, 0);
		return lua_error(l);
	}

	assert(lua_gettop(ud->state) == 0);
	return 0;
}

static int ldstate_setMetatable(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TUSERDATA);

	lua_rawgetp(l, LUA_REGISTRYINDEX, (void *)ldstate_setMetatable);
	if(lua_type(l, -1) == LUA_TNIL) {
		lua_pop(l, 1);
		lua_newtable(l);

		lua_pushvalue(l, -1);
		lua_setfield(l, -2, "__index");

		lua_pushcfunction(l, ldstate_mtgc);
		lua_setfield(l, -2, "__gc");

		lua_pushcfunction(l, ldstate_mtpushcode);
		lua_setfield(l, -2, "pushCode");

		lua_pushcfunction(l, ldstate_mtpushSearch);
		lua_setfield(l, -2, "pushSearch");

		lua_pushcfunction(l, ldstate_mtrun);
		lua_setfield(l, -2, "run");

		lua_pushvalue(l, -1);
		lua_rawsetp(l, LUA_REGISTRYINDEX, (void *)ldstate_setMetatable);
	}
	assert(lua_type(l, -1) == LUA_TTABLE);
	lua_setmetatable(l, -2);

	return 1;
}

static const luaL_Reg ldstate_requirefdata[] = {
 {"_G", luaopen_base},
 {LUA_LOADLIBNAME, luaopen_package},
 {LUA_COLIBNAME, luaopen_coroutine},
 {LUA_TABLIBNAME, luaopen_table},
 {LUA_IOLIBNAME, luaopen_io},
 {LUA_OSLIBNAME, luaopen_os},
 {LUA_STRLIBNAME, luaopen_string},
 {LUA_BITLIBNAME, luaopen_bit32},
 {LUA_MATHLIBNAME, luaopen_math},
 {LUA_DBLIBNAME, luaopen_debug},
 {"ldclient", ldclient_moduleloader},
 {NULL, NULL}
};

static int ldstate_lookupmodule(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TSTRING);

	lua_newtable(l);
	luaL_setfuncs(l, ldstate_requirefdata, 0);
	lua_insert(l, 1);

	if(strncmp(lua_tostring(l, -1), "base", 5) == 0) {
		lua_pop(l, 1);
		lua_pushstring(l, "_G");
	}

	lua_pushvalue(l, -1);

	lua_gettable(l, 1);
	if(lua_type(l, -1) != LUA_TFUNCTION) {
		return luaL_error(l, "No such module");
	}

	return 2;
}

static int ldstate_create(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TTABLE);

	struct ldstate_userdata *ud = (struct ldstate_userdata *)
	 lua_newuserdata(l, sizeof(struct ldstate_userdata));
	assert(ud != NULL);
	ud->state = NULL;

	lua_pushcfunction(l, ldstate_setMetatable);
	lua_insert(l, 2);
	lua_call(l, 1, 1);

	ud->state = luaL_newstate();

	int elems = lua_rawlen(l, 1);
	int idx;
	for(idx=1;idx<=elems;++idx) {
		lua_pushcfunction(l, ldstate_lookupmodule);
		lua_rawgeti(l, 1, idx);
		lua_call(l, 1, 2);

		luaL_requiref(ud->state, lua_tostring(l, -2),
		 lua_tocfunction(l, -1), 1);
		assert(lua_type(ud->state, -1) == LUA_TTABLE);
		lua_pop(ud->state, 1);

		lua_pop(l, 2);
	}

	return 1;
}
		
