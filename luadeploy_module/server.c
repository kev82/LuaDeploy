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
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <sqlite3.h>
#include <mqueue.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

struct ldserver_threaddata
{
	int piperead_fd;
	mqd_t queue_fd;
	sqlite3 *db;
	char *software;
	char *sopath;
};

struct ldserver_userdata
{
	int pipewrite_fd;
	mqd_t queue_fd;
	char *queue_name;
};

static int ldresponse_error(lua_State *l) {
	lua_settop(l, 1);
	char *err = lua_touserdata(l, 1);
	lua_pushstring(l, err);
	return lua_error(l);
}

//Don't remove, useful for debugging
static int ldresponse_dumpstate(lua_State *l) {
	lua_settop(l, 1);
	lua_State *s = (lua_State *)lua_touserdata(l, 1);
	lua_settop(s, 0);

	lua_rawgeti(s, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
	lua_pushnil(s);
	while(lua_next(s, 1) != 0) {
		fprintf(stderr, "searchdef has key %s\n", lua_tostring(s, -2));
		if(lua_type(s, -1) == LUA_TSTRING) {
			fprintf(stderr, "with value %s\n", lua_tostring(s, -1));
		}
		lua_pop(s, 1);
	}

	lua_close(s);

	return luaL_error(l, "Not implemented");
}

static int ldresponse_loadlua(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TLIGHTUSERDATA);

	lua_State *s = (lua_State *)lua_touserdata(l, 1);

	lua_getglobal(s, "code");
	size_t len;
	const char *code = lua_tolstring(s, -1, &len);

	int rc = luaL_loadbufferx(l, code, len, "luadeploy_code", "b");
	if(rc != LUA_OK) {
		lua_close(s);
		return luaL_error(l, "Unable to load luadeploy module");
	}

	lua_settop(s, 0);
	lua_getglobal(s, "entrypoint");
	if(lua_type(s, -1) != LUA_TNIL) {
		lua_close(s);
		return luaL_error(l, "Lua code with entry points not supported");
	}

	lua_settop(s, 0);
	lua_getglobal(s, "args");
	assert(lua_type(s, -1) == LUA_TTABLE);
	int elems = lua_rawlen(s, -1);
	int idx;
	for(idx=1;idx<=elems;++idx) {
		lua_rawgeti(s, 1, idx);
		assert(lua_type(s, -1) == LUA_TSTRING);
		lua_pushstring(l, lua_tostring(s, -1));
		lua_pop(s, 1);
	}

	lua_close(s);

	return 1+elems;
}

static int lddlcloser_mtgc(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TUSERDATA);

	void *hndl = *(void **)lua_touserdata(l, 1);
	fprintf(stderr, "calling dlclose on %p\n", hndl);
	dlclose(hndl);

	return 0;
}

static int lddlcloser_install(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TLIGHTUSERDATA);

	void **phndl = (void **)lua_newuserdata(l, sizeof(void *));
	*phndl = lua_touserdata(l, 1);

	lua_rawgetp(l, LUA_REGISTRYINDEX, (void *)lddlcloser_install);
	if(lua_type(l, -1) == LUA_TNIL) {
		lua_pop(l, 1);

		lua_newtable(l);

		lua_pushcfunction(l, lddlcloser_mtgc);
		lua_setfield(l, -2, "__gc");

		lua_pushvalue(l, -1);
		lua_rawsetp(l, LUA_REGISTRYINDEX, (void *)lddlcloser_install);
	}
	assert(lua_type(l, -1) == LUA_TTABLE);
	lua_setmetatable(l, -2);

	//want it to be collected when the state is closed
	luaL_ref(l, LUA_REGISTRYINDEX);

	return 0;
}

static int ldresponse_loadso(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TLIGHTUSERDATA);

	lua_State *s = (lua_State *)lua_touserdata(l, 1);

	lua_settop(s, 0);
	lua_getglobal(s, "fullsopath");
	lua_getglobal(s, "entrypoint");

	void *hndl = dlopen(lua_tostring(s, 1), RTLD_NOW | RTLD_LOCAL);
	if(hndl == NULL) {
		lua_close(s);
		return luaL_error(l, "Unable to open shared obj");
	}

	void *func = dlsym(hndl, lua_tostring(s, 2));
	if(func == NULL) {
		lua_close(s);
		return luaL_error(l, "Unable to find symbol");
	}
	
	lua_pushcfunction(l, lddlcloser_install);
	lua_pushlightuserdata(l, hndl);
	lua_call(l, 1, 0);

	lua_pushcfunction(l, (int (*)(lua_State *))func);

	lua_settop(s, 0);
	lua_getglobal(s, "args");
	assert(lua_type(s, -1) == LUA_TTABLE);
	int elems = lua_rawlen(s, -1);
	int idx;
	for(idx=1;idx<=elems;++idx) {
		lua_rawgeti(s, 1, idx);
		assert(lua_type(s, -1) == LUA_TSTRING);
		lua_pushstring(l, lua_tostring(s, -1));
		lua_pop(s, 1);
	}

	lua_close(s);

	return 1+elems;
}
	
static void ldserver_loadlua(
 struct ldserver_threaddata *td,
 struct ldloader_request *req,
 lua_State *searchResult) {
	lua_settop(searchResult, 0);

	lua_getglobal(searchResult, "software");
	lua_getglobal(searchResult, "objref");

	sqlite3_stmt *stmt = NULL;
	int rc = sqlite3_prepare_v2(td->db, "select ld_loader_getobj(?,?)",
	 -1, &stmt, NULL);
	assert(rc == SQLITE_OK && stmt != NULL);

	rc |= sqlite3_bind_text(stmt, 1, lua_tostring(searchResult, 1),
	 -1, SQLITE_STATIC);
	rc |= sqlite3_bind_text(stmt, 2, lua_tostring(searchResult, 2),
	 -1, SQLITE_STATIC);

	assert(rc == SQLITE_OK);

	rc = sqlite3_step(stmt);
	assert(rc == SQLITE_ROW);

	lua_pushlstring(searchResult, sqlite3_column_blob(stmt, 0),
	 sqlite3_column_bytes(stmt, 0));
	lua_setglobal(searchResult, "code");
	sqlite3_finalize(stmt);

	req->responseHandler = ldresponse_loadlua;
	req->responseData = searchResult;
}

static void ldserver_loadso(
 struct ldserver_threaddata *td,
 struct ldloader_request *req,
 lua_State *searchResult) {
	lua_settop(searchResult, 0);

	lua_pushstring(searchResult, td->sopath);
	lua_pushstring(searchResult, "/");
	lua_getglobal(searchResult, "objref");
	lua_pushstring(searchResult, ".so");
	lua_concat(searchResult, 4);
	lua_setglobal(searchResult, "fullsopath");
	
	req->responseHandler = ldresponse_loadso;
	req->responseData = searchResult;
}

static void ldserver_handleRequest(struct ldserver_threaddata *td,
 struct ldloader_request *req) {
	char *query =
	 sqlite3_mprintf("select ld_loader_search('%s', ?, ?)", td->software);

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(td->db, query, -1, &stmt, NULL);
	sqlite3_free(query);
	assert(rc == SQLITE_OK);

	sqlite3_bind_text(stmt, 1, req->type, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, req->name, -1, SQLITE_STATIC);
	
	rc = sqlite3_step(stmt);
	assert(rc == SQLITE_ROW);
	
	if(sqlite3_column_type(stmt, 0) == SQLITE_NULL) {
		sqlite3_finalize(stmt);
		char buffer[1024];
		snprintf(buffer, 1023, "Unable to find %s in %s/%s",
		 req->name, td->software, req->type);

		req->responseHandler = ldresponse_error;
		req->responseData = strdup(buffer);
		return;
	}

	assert(sqlite3_column_type(stmt, 0) == SQLITE_TEXT);

	lua_State *responseState = luaL_newstate();
	rc = luaL_loadbufferx(responseState, sqlite3_column_text(stmt, 0),
	 sqlite3_column_bytes(stmt, 0), "searchdef", "t");
	assert(rc == LUA_OK);
	sqlite3_finalize(stmt);
	rc = lua_pcall(responseState, 0, 0, 0);
	assert(rc == LUA_OK);

	lua_getglobal(responseState, "loader");
	if(strncmp(lua_tostring(responseState, 1), "so", 3) == 0) {
		lua_settop(responseState, 0);
		ldserver_loadso(td, req, responseState);
		//responseState is sent in the response, so we
		//can't close it here
	} else if(strncmp(lua_tostring(responseState, 1), "lua", 4) == 0) {
		lua_settop(responseState, 0);
		ldserver_loadlua(td, req, responseState);
		//responseState is sent in the response, so we
		//can't close it here
	} else {
		req->responseHandler = ldresponse_error;
		req->responseData = strdup("Unknown Loader");
		lua_close(responseState);
	}
}

static void *ldserver_thread(void *p) {
	struct ldserver_threaddata *td = (struct ldserver_threaddata *)p;
	sem_t *notify = NULL;

	fd_set readfds;

	int maxfd = td->piperead_fd;
	if(td->queue_fd > maxfd) maxfd = td->queue_fd;

	while(1) {
		FD_ZERO(&readfds);
		FD_SET(td->piperead_fd, &readfds);
		FD_SET(td->queue_fd, &readfds);

		if(select(maxfd+1, &readfds, NULL, NULL, NULL) == -1) continue;

		if(FD_ISSET(td->piperead_fd, &readfds)) {
			read(td->piperead_fd, &notify, sizeof(sem_t *));
			break;
		}

		if(FD_ISSET(td->queue_fd, &readfds)) {
			void *msg;
			mq_receive(td->queue_fd, (char *)&msg, sizeof(void *), NULL);
			ldserver_handleRequest(td, (struct ldloader_request *)msg);
			sem_post(&((struct ldloader_request *)msg)->sem);
		}
	}

	close(td->piperead_fd);
	free(td->software);
	free(td->sopath);
	free(p);
	sem_post(notify);
	return NULL;
}

static int ldserver_mtStart(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TUSERDATA);

	struct ldserver_userdata *ud =
	 (struct ldserver_userdata *)lua_touserdata(l, 1);

	if(ud->pipewrite_fd != -1) {
		lua_pushboolean(l, 0);
		lua_pushstring(l, "Thread already started");
		return 2;
	}

	int pipefd[2];
	pipe2(pipefd, O_CLOEXEC);

	struct ldserver_threaddata *td =
	 (struct ldserver_threaddata *)malloc(sizeof(struct ldserver_threaddata));
	assert(td != NULL);
	td->piperead_fd = pipefd[0];
	td->queue_fd = ud->queue_fd;

	lua_getuservalue(l, 1);
	lua_getfield(l, -1, "sopath");
	lua_getfield(l, -2, "dbud");
	lua_getfield(l, -3, "software");
	td->software = strdup(lua_tostring(l, -1));
	td->db = *(sqlite3 **)lua_touserdata(l, -2);
	td->sopath = strdup(lua_tostring(l, -3));
	lua_pop(l, 4);

	pthread_t thread;
	int rc = pthread_create(&thread, NULL, ldserver_thread, td);
	if(rc != 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		free(td);
		return luaL_error(l, "Unable to create thread");
	}
	pthread_detach(thread);

	ud->pipewrite_fd = pipefd[1];

	lua_pushboolean(l, 1);
	return 1;
}

static int ldserver_mtStop(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TUSERDATA);

	struct ldserver_userdata *ud =
	 (struct ldserver_userdata *)lua_touserdata(l, 1);

	if(ud->pipewrite_fd == -1) {
		lua_pushboolean(l, 0);
		lua_pushstring(l, "Thread not running");
		return 2;
	}

	sem_t sem;
	sem_init(&sem, 0, 0);
	sem_t *psem = &sem;
	write(ud->pipewrite_fd, &psem, sizeof(sem_t *));
	sem_wait(&sem);
	sem_destroy(&sem);

	close(ud->pipewrite_fd);
	ud->pipewrite_fd = -1;

	lua_pushboolean(l, 1);
	return 1;
}

static int ldserver_mtgc(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TUSERDATA);

	luaL_callmeta(l, 1, "stop");

	struct ldserver_userdata *ud =
	 (struct ldserver_userdata *)lua_touserdata(l, 1);

	if(ud->queue_fd != -1) {
		mq_unlink(ud->queue_name);
		free(ud->queue_name);

		mq_close(ud->queue_fd);
		ud->queue_fd = -1;
	}
	
	return 0;
}

static int ldserver_openmqueue(lua_State *l) {
	lua_settop(l, 2);
	luaL_checktype(l, 1, LUA_TUSERDATA);
	luaL_checktype(l, 2, LUA_TSTRING);

	char buffer[1024];
	snprintf(buffer, 1024, "/lds-%d-%s", getpid(), lua_tostring(l, 2));
	if(strlen(buffer) > 255) {
		return luaL_error(l, "message queue name unsuitable");
	}

	struct mq_attr attr;
	attr.mq_flags = 0;
	attr.mq_maxmsg = 5;
	attr.mq_msgsize = sizeof(struct ldloader_request *);
	attr.mq_curmsgs = 0;

	struct ldserver_userdata *ud =
	 (struct ldserver_userdata *)lua_touserdata(l, 1);
	ud->queue_name = strdup(buffer);

	ud->queue_fd = mq_open(ud->queue_name,
	 O_RDONLY | O_CREAT | O_EXCL,
	 S_IRUSR | S_IWUSR, &attr);
	if(ud->queue_fd == -1) {
		return luaL_error(l, "Unable to open msgqueue");
	}

	return 0;
}

static int ldserver_setMetatable(lua_State *l) {
	lua_settop(l, 1);
	luaL_checktype(l, 1, LUA_TUSERDATA);

	lua_rawgetp(l, LUA_REGISTRYINDEX, (void *)ldserver_setMetatable);
	if(lua_type(l, -1) == LUA_TNIL) {
		lua_pop(l, 1);
		lua_newtable(l);

		lua_pushvalue(l, -1);
		lua_setfield(l, -2, "__index");

		lua_pushcfunction(l, ldserver_mtStop);
		lua_setfield(l, -2, "stop");

		lua_pushcfunction(l, ldserver_mtStart);
		lua_setfield(l, -2, "start");

		lua_pushcfunction(l, ldserver_mtgc);
		lua_setfield(l, -2, "__gc");

		lua_pushvalue(l, -1);
		lua_rawsetp(l, LUA_REGISTRYINDEX, (void *)ldserver_setMetatable);
	}
	assert(lua_type(l, -1) == LUA_TTABLE);
	lua_setmetatable(l, 1);

	return 1;
}

static int ldserver_createThreadObj(lua_State *l) {
	lua_settop(l, 4);	//[ssus]
	luaL_checktype(l, 1, LUA_TSTRING);
	luaL_checktype(l, 2, LUA_TSTRING);
	luaL_checktype(l, 3, LUA_TUSERDATA);
	luaL_checktype(l, 4, LUA_TSTRING);

	lua_newtable(l);	//[ssust]
	lua_insert(l, 2);	//[stsus]
	lua_setfield(l, 2, "sopath");
	lua_setfield(l, 2, "dbud");
	lua_setfield(l, 2, "software");
	//[st]
	
	struct ldserver_userdata *ud = (struct ldserver_userdata *)
	 lua_newuserdata(l, sizeof(struct ldserver_userdata));
	//[stu]
	lua_insert(l, 2);	//[sut]
	lua_setuservalue(l, -2);	//[su]

	ud->pipewrite_fd = -1;
	ud->queue_fd = -1;
	ud->queue_name = NULL;

	lua_pushcfunction(l, ldserver_setMetatable);
	lua_pushvalue(l, -2);
	lua_call(l, 1, 0);

	lua_pushcfunction(l, ldserver_openmqueue);
	lua_pushvalue(l, -2);
	lua_pushvalue(l, 1);
	lua_call(l, 2, 0);

	return 1;
}
