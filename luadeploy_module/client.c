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
#include <semaphore.h>
#include <mqueue.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int ldclient_request(lua_State *l) {
	lua_settop(l, 3);
	luaL_checktype(l, 1, LUA_TSTRING);
	luaL_checktype(l, 2, LUA_TSTRING);
	luaL_checktype(l, 3, LUA_TSTRING);

	char buffer[1024];
	snprintf(buffer, 1023, "/lds-%d-%s", getpid(), lua_tostring(l, 1));
	buffer[1023] = 0;

	mqd_t q = mq_open(buffer, O_WRONLY);
	if(q == -1) return luaL_error(l, "Unable to open msg queue");

	struct ldloader_request *req =
	 (struct ldloader_request *)malloc(sizeof(struct ldloader_request));
	sem_init(&req->sem, 0, 0);
	req->type = strdup(lua_tostring(l, 2));
	req->name = strdup(lua_tostring(l, 3));

	lua_settop(l, 0);

	mq_send(q, (char *)&req, sizeof(struct ldloader_request *), 0);
	sem_wait(&req->sem);

	mq_close(q);
	sem_destroy(&req->sem);
	free(req->type);
	free(req->name);

	lua_pushcfunction(l, req->responseHandler);
	lua_pushlightuserdata(l, req->responseData);
	free(req);

	lua_call(l, 1, LUA_MULTRET);
	return lua_gettop(l);
}

static int ldclient_moduleloader(lua_State *l) {
	lua_newtable(l);
	lua_pushcfunction(l, ldclient_request);
	lua_setfield(l, -2, "search");
	return 1;
}
