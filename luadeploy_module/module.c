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

static int ldsodir_getpid(lua_State *l) {
	lua_pushinteger(l, getpid());
	return 1;
}

int luaopen_luadeploy(lua_State *l) {
	int rc = luaL_loadbufferx(l, luadeploy_code, sizeof(luadeploy_code),
	 "LuaDeploy Code", "b");
	if(rc != LUA_OK) {
		return lua_error(l);
	}

	lua_newtable(l);

	lua_pushcfunction(l, ldserver_createThreadObj);
	lua_setfield(l, -2, "createServer");

	lua_pushcfunction(l, ldclient_request);
	lua_setfield(l, -2, "sendRequest");

	lua_pushcfunction(l, lddb_createFromSQLString);
	lua_setfield(l, -2, "openSQLString");

	lua_pushcfunction(l, lddb_createFromDBFile);
	lua_setfield(l, -2, "openDBFile");

	lua_pushcfunction(l, ldsodir_getpid);
	lua_setfield(l, -2, "getpid");

	lua_pushcfunction(l, ldstate_create);
	lua_setfield(l, -2, "newState");

	lua_call(l, 1, 1);
	return 1;
}
