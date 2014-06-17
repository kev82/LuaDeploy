/******************************************************************************
* Copyright (C) 2013-2014, Kevin Martin (kev82@khn.org.uk)
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
#ifndef __EXPORTS_CURSOR_HEADER__
#define __EXPORTS_CURSOR_HEADER__

#include <stdio.h>
#include <regex.h>

struct expcursor {
	FILE *stream;
	char *line;

	regmatch_t *matches;

	int (*parsefailed)(struct expcursor *);
	const char *(*expentry)(struct expcursor *);
	const char *(*exptype)(struct expcursor *);
	const char *(*expregex)(struct expcursor *);
	const char *(*exppriority)(struct expcursor *);
};

#define expcursor_failedtoparse(x) ((x)->parsefailed(x)) 
#define expcursor_entrypoint(x) ((x)->expentry(x)) 
#define expcursor_type(x) ((x)->exptype(x)) 
#define expcursor_regex(x) ((x)->expregex(x)) 
#define expcursor_priority(x) ((x)->exppriority(x)) 
	
void expcursor_init(struct expcursor *ec, const char *buffer, int sz);
void expcursor_destroy(struct expcursor *ec);

void expcursor_next(struct expcursor *ec);
int expcursor_finished(struct expcursor *ec);

const char *expcursor_inputline(struct expcursor *ec);

#endif
