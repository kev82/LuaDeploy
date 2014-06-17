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

#include <semaphore.h>
#include <lua.h>

struct ldloader_request
{
	//server posts when request finished
	sem_t sem;

	//Request information
	char *type;
	char *name;

	//Response
	int (*responseHandler)(lua_State *l);
	void *responseData;
};

//The client should
//
//o open the msg queue
//o Allocate the struct
//o Initialize the semaphore
//o strdup the name and type and fill it in
//o post the message
//o wait on the semaphore
//o destroy the semaphore
//o free the type and name
//o push the function
//o push the data
//o free the struct
//o call the function (it frees it's own data)
