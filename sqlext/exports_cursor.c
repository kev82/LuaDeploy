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
#include "exports_cursor.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <regex.h>

#ifdef MMPL
#error Duplicate use of define
#endif
//maximum number of matches required for an export line
#define MMPL 5

void expcursor_init(struct expcursor *ec, const char *buffer, int sz) {
	assert(ec != NULL);
	assert(buffer != NULL);
	if(sz == -1) sz = strlen(buffer);

	ec->stream = NULL;
	ec->line = NULL;

	ec->matches = (regmatch_t *)malloc(MMPL * sizeof(regmatch_t));

	ec->stream = fmemopen((void *)buffer, sz, "r");
	assert(ec->stream != NULL);

	expcursor_next(ec);
}

void expcursor_destroy(struct expcursor *ec) {
	if(ec->line != NULL) {
		free(ec->line);
		ec->line = NULL;
	}
	if(ec->stream != NULL) {
		fclose(ec->stream);
		ec->stream = NULL;
	}
	if(ec->matches != NULL) {
		free(ec->matches);
		ec->matches = NULL;
	}
}

static int func0(struct expcursor *ec) {
	return 0;
}
static int func1(struct expcursor *ec) {
	return 1;
}

static const char *funcnull(struct expcursor *ec) {
	return NULL;
}
static const char *funcmatch1(struct expcursor *ec) {
	regmatch_t *r = &ec->matches[1];
	ec->line[r->rm_eo] = 0;
	return ec->line + r->rm_so;
}
static const char *funcmatch2(struct expcursor *ec) {
	regmatch_t *r = &ec->matches[2];
	ec->line[r->rm_eo] = 0;
	return ec->line + r->rm_so;
}
static const char *funcmatch3(struct expcursor *ec) {
	regmatch_t *r = &ec->matches[3];
	ec->line[r->rm_eo] = 0;
	return ec->line + r->rm_so;
}
static const char *funcmatch4(struct expcursor *ec) {
	regmatch_t *r = &ec->matches[4];
	ec->line[r->rm_eo] = 0;
	return ec->line + r->rm_so;
}

static void exports_parsefile(struct expcursor *ec, char sep) {
	char *regex;
	size_t regbytes;
	FILE *regstream = open_memstream(&regex, &regbytes);
	assert(MMPL >= 4);
	fprintf(regstream,
	 "^--export file sep '[%c]'[%c]([^%c]+)[%c]([^%c]+)[%c]([^%c]+)[%c][\n]?$",
	 sep, sep, sep, sep, sep, sep, sep, sep);
	fclose(regstream);

	regex_t r;
	int rc = regcomp(&r, regex, REG_EXTENDED);
	assert(rc == 0);
	free(regex);

	assert(ec->matches != NULL);
	rc = regexec(&r, ec->line, MMPL, ec->matches, 0);
	regfree(&r);
	if(rc != 0) {
		ec->parsefailed = func1;
		ec->expentry = funcnull;
		ec->exptype = funcnull;
		ec->expregex = funcnull;
		ec->exppriority = funcnull;
	} else {
		ec->parsefailed = func0;
		ec->expentry = funcnull;
		ec->exptype = funcmatch1;
		ec->expregex = funcmatch2;
		ec->exppriority = funcmatch3;
	}
}

static void exports_parsefunc(struct expcursor *ec, char sep) {
	char *regex;
	size_t regbytes;
	FILE *regstream = open_memstream(&regex, &regbytes);
	assert(MMPL >= 5);
	fprintf(regstream,
	 "^--export function ([a-zA-Z][a-zA-Z0-9_]+) sep '[%c]'[%c]([^%c]+)[%c]([^%c]+)[%c]([^%c]+)[%c][\n]?$",
	 sep, sep, sep, sep, sep, sep, sep, sep);
	fclose(regstream);

	regex_t r;
	int rc = regcomp(&r, regex, REG_EXTENDED);
	assert(rc == 0);
	free(regex);

	assert(ec->matches != NULL);
	rc = regexec(&r, ec->line, MMPL, ec->matches, 0);
	regfree(&r);
	if(rc != 0) {
		ec->parsefailed = func1;
		ec->expentry = funcnull;
		ec->exptype = funcnull;
		ec->expregex = funcnull;
		ec->exppriority = funcnull;
	} else {
		ec->parsefailed = func0;
		ec->expentry = funcmatch1;
		ec->exptype = funcmatch2;
		ec->expregex = funcmatch3;
		ec->exppriority = funcmatch4;
	}
}

static void exports_parseline(struct expcursor *ec) {
	regex_t rfile, rfunc;

	int rc = regcomp(&rfile, "^--export file sep '(.)'", REG_EXTENDED);
	assert(rc == 0);
	rc = regcomp(&rfunc,
	 "^--export function [a-zA-Z][a-zA-Z0-9_]+ sep '(.)'", REG_EXTENDED);
	assert(rc == 0);

	regmatch_t sepmatch[2];

	if(regexec(&rfile, ec->line, 2, sepmatch, 0) == 0) {
		assert(sepmatch[1].rm_so > 0 &&
		 sepmatch[1].rm_eo - sepmatch[1].rm_so == 1);
		exports_parsefile(ec, ec->line[sepmatch[1].rm_so]);
	} else if(regexec(&rfunc, ec->line, 2, sepmatch, 0) == 0) {
		assert(sepmatch[1].rm_so > 0 &&
		 sepmatch[1].rm_eo - sepmatch[1].rm_so == 1);
		exports_parsefunc(ec, ec->line[sepmatch[1].rm_so]);
	} else {
		ec->parsefailed = func1;
		ec->exptype = funcnull;
	}

	regfree(&rfunc);
	regfree(&rfile);
}

void expcursor_next(struct expcursor *ec) {
	if(ec->stream == NULL) return;

	if(ec->line != NULL) {
		free(ec->line);
		ec->line = NULL;
	}

	size_t bytes;
	if(getline(&ec->line, &bytes, ec->stream) == -1) {
		free(ec->line);
		ec->line = NULL;
		fclose(ec->stream);
		ec->stream = NULL;
		return;
	}

	const char *exphdr = "--begin exports";
	if(strncmp(ec->line, exphdr, strlen(exphdr)) == 0) {
		return expcursor_next(ec);
	}

	const char *expftr = "--end exports";
	if(strncmp(ec->line, expftr, strlen(expftr)) == 0) {
		return expcursor_next(ec);
	}

	return exports_parseline(ec);
}

int expcursor_finished(struct expcursor *ec) {
	return ec->stream == NULL;
}

const char *expcursor_inputline(struct expcursor *ec) {
	return ec->line;
}

