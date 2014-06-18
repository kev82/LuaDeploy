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
#include <lauxlib.h>
#include <sqlite3.h>

int ldext_init(
 sqlite3 *db,
 const char **errmsg,
 const void *api);

struct lddb_userdata
{
	sqlite3 *db;
};

static int lddb_mtgc(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TUSERDATA);

	struct lddb_userdata *ud =
	 (struct lddb_userdata *)lua_touserdata(l, 1);

	if(ud->db != NULL) {
		sqlite3_close(ud->db);
		ud->db = NULL;
	}

	return 0;
}

static int lddb_mtexport(lua_State *l) {
	lua_settop(l, 3);
	luaL_checktype(l, 1, LUA_TUSERDATA);
	luaL_checktype(l, 2, LUA_TSTRING);
	luaL_checktype(l, 3, LUA_TSTRING);

	const char *src = luaL_checkstring(l, 2);
	const char *dest = luaL_checkstring(l, 3);

	struct lddb_userdata *ud = (struct lddb_userdata *)lua_touserdata(l, 1);
	assert(ud != NULL);
	
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(ud->db,
	 "select ld_deploy_softwaresql(?, ?)", -1, &stmt, NULL);
	assert(rc == SQLITE_OK && stmt != NULL);

	rc |= sqlite3_bind_text(stmt, 1, src, -1, SQLITE_STATIC);
	rc |= sqlite3_bind_text(stmt, 2, dest, -1, SQLITE_STATIC);

	assert(rc == SQLITE_OK);

	rc = sqlite3_step(stmt);
	while(rc == SQLITE_ROW) {
		fprintf(stdout, "%s", sqlite3_column_text(stmt, 0));
		fflush(stdout);

		rc = sqlite3_step(stmt);
	}
	sqlite3_finalize(stmt);

	if(rc != SQLITE_DONE) {
		return luaL_error(l, "writesql sql error");
	}

	return 0;
}
	
static int lddb_mtwriteso(lua_State *l) {
	lua_settop(l, 3);
	luaL_checktype(l, 1, LUA_TUSERDATA);
	luaL_checktype(l, 2, LUA_TSTRING);

	luaL_tolstring(l, 3, NULL);
	lua_replace(l, 3);

	struct lddb_userdata *ud = (struct lddb_userdata *)lua_touserdata(l, 1);
	assert(ud != NULL);

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(ud->db,
	 "select ld_deploy_writeso(?, ?)", -1, &stmt, NULL);
	assert(rc == SQLITE_OK && stmt != NULL);

	rc |= sqlite3_bind_text(stmt, 1, lua_tostring(l, 2), -1, SQLITE_STATIC);
	rc |= sqlite3_bind_text(stmt, 2, lua_tostring(l, 3), -1, SQLITE_STATIC);

	assert(rc == SQLITE_OK);

	rc = sqlite3_step(stmt);
	while(rc == SQLITE_ROW) {
		rc = sqlite3_step(stmt);
	}
	sqlite3_finalize(stmt);

	if(rc != SQLITE_DONE) {
		return luaL_error(l, "writeso sql error");
	}

	return 0;
}

static int lddb_setMetatable(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TUSERDATA);

	lua_rawgetp(l, LUA_REGISTRYINDEX, (void *)lddb_setMetatable);
	if(lua_type(l, -1) == LUA_TNIL) {
		lua_pop(l, 1);
		lua_newtable(l);

		lua_pushvalue(l, -1);
		lua_setfield(l, -2, "__index");

		lua_pushcfunction(l, lddb_mtgc);
		lua_setfield(l, -2, "__gc");

		lua_pushcfunction(l, lddb_mtexport);
		lua_setfield(l, -2, "exportSoftware");

		lua_pushcfunction(l, lddb_mtwriteso);
		lua_setfield(l, -2, "writeSharedObjs");

		lua_pushvalue(l, -1);
		lua_rawsetp(l, LUA_REGISTRYINDEX, (void *)lddb_setMetatable);
	}
	assert(lua_type(l, -1) == LUA_TTABLE);

	lua_setmetatable(l, -2);
	return 1;
}

static int lddb_createFromSQLString(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TSTRING);

	struct lddb_userdata *ud =
	 (struct lddb_userdata *)lua_newuserdata(l, sizeof(struct lddb_userdata *));
	ud->db = NULL;

	lua_pushcfunction(l, lddb_setMetatable);
	lua_insert(l, 2);
	lua_call(l, 1, 1);

	int rc = sqlite3_open_v2(":memory:", &ud->db,
	 SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
	 NULL);
	assert(ud->db != NULL);

	ldext_init(ud->db, NULL, NULL);

	const char *sql = lua_tostring(l, 1);
	sqlite3_stmt *stmt;
	while(1) {
		int rc = sqlite3_prepare_v2(ud->db, sql, -1, &stmt, &sql);
		if(rc != SQLITE_OK) {
			return luaL_error(l, "Failed to prepare statement");
		}

		if(stmt == NULL) break;

		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if(rc != SQLITE_DONE) {
			return luaL_error(l, "Failed to execute statement");
		}
	}

	rc = sqlite3_prepare_v2(ud->db,
	 "select name from sqlite_master "
	 "where type='table' and name like '%_manifest'", -1, &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_step(stmt);
	while(rc != SQLITE_DONE) {
		char *query = sqlite3_mprintf("select * from \"%s\"",
		 sqlite3_column_text(stmt, 0));
		sqlite3_stmt *stmt2;
		int rc2 = sqlite3_prepare_v2(ud->db, query, -1, &stmt2, NULL);
		assert(rc2 == SQLITE_OK);
		rc2 = sqlite3_step(stmt2);
		while(rc2 != SQLITE_DONE) {
			if(rc2 != SQLITE_ROW) {
				lua_pushstring(l, "Problem with ");
				lua_pushstring(l, (const char *)sqlite3_column_text(stmt, 0));
				lua_pushstring(l, " manifest");
				lua_concat(l, 3);

				sqlite3_finalize(stmt2);
				sqlite3_finalize(stmt);

				return lua_error(l);
			}

			rc2 = sqlite3_step(stmt2);
		}

		rc = sqlite3_step(stmt);
	}

	return 1;
}

static int lddb_createFromDBFile(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TSTRING);

	struct lddb_userdata *ud =
	 (struct lddb_userdata *)lua_newuserdata(l, sizeof(struct lddb_userdata *));
	ud->db = NULL;

	lua_pushcfunction(l, lddb_setMetatable);
	lua_insert(l, 2);
	lua_call(l, 1, 1);

	int rc = sqlite3_open_v2(lua_tostring(l, 1), &ud->db,
	 SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_READONLY, NULL);
	assert(ud->db != NULL);

	ldext_init(ud->db, NULL, NULL);

	sqlite3_stmt *stmt;

	rc = sqlite3_prepare_v2(ud->db,
	 "select name from sqlite_master "
	 "where type='table' and name like '%_manifest'", -1, &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_step(stmt);
	while(rc != SQLITE_DONE) {
		char *query = sqlite3_mprintf("select * from \"%s\"",
		 sqlite3_column_text(stmt, 0));
		sqlite3_stmt *stmt2;
		int rc2 = sqlite3_prepare_v2(ud->db, query, -1, &stmt2, NULL);
		assert(rc2 == SQLITE_OK);
		rc2 = sqlite3_step(stmt2);
		while(rc2 != SQLITE_DONE) {
			if(rc2 != SQLITE_ROW) {
				lua_pushstring(l, "Problem with ");
				lua_pushstring(l, (const char *)sqlite3_column_text(stmt, 0));
				lua_pushstring(l, " manifest");
				lua_concat(l, 3);

				sqlite3_finalize(stmt2);
				sqlite3_finalize(stmt);

				return lua_error(l);
			}

			rc2 = sqlite3_step(stmt2);
		}

		rc = sqlite3_step(stmt);
	}

	return 1;
}
