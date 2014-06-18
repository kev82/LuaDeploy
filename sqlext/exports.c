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
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "exports_cursor.h"

static void exports_countexports(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 1);
	
	struct expcursor ec;

	expcursor_init(&ec, (const char *)sqlite3_value_text(argv[0]), -1);

	int ne = 0;
	while(!expcursor_finished(&ec)) {
		if(expcursor_failedtoparse(&ec)) {
			char *errmsg = sqlite3_mprintf(
			 "Failed to parse export definition: '%s'\n",
			 expcursor_inputline(&ec));
			sqlite3_result_error(ctx, errmsg, -1);
			sqlite3_free(errmsg);
			return;
		} else {
/*
			printf("Entrypoint: %s, Type: %s, Pri: %s, Regex: %s\n",
			 expcursor_entrypoint(&ec), expcursor_type(&ec),
			 expcursor_priority(&ec), expcursor_regex(&ec));
*/
		}

		++ne;
		expcursor_next(&ec);
	}

	expcursor_destroy(&ec);

	sqlite3_result_int(ctx, ne);
} 

struct exports_aggregate_data {
	int init;
	
	char *data;
	size_t bytes;

	FILE *stream;
};

static struct exports_aggregate_data *
 exports_generate_aggsetup(sqlite3_context *ctx) {
	struct exports_aggregate_data *d =
	 (struct exports_aggregate_data *)sqlite3_aggregate_context(ctx,
	 sizeof(struct exports_aggregate_data));

	if(d->init == 0) {
		d->init = 1;
		d->stream = open_memstream(&d->data, &d->bytes);
		assert(d->stream != NULL);

		fprintf(d->stream, "--begin exports\n");
	}
	return d;
}

static void exports_generate3_aggstep(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 3);

	struct exports_aggregate_data *d = exports_generate_aggsetup(ctx);

	const char *type = (const char *)sqlite3_value_text(argv[0]);
	const char *regex = (const char *)sqlite3_value_text(argv[1]);
	const char *priority = (const char *)sqlite3_value_text(argv[2]);

	if(strchr(type, '%') != NULL ||
	 strchr(regex, '%') != NULL ||
	 strchr(priority, '%') != NULL) {
		sqlite3_result_error(ctx, "Unable to find suitable separator", -1);
	}

	fprintf(d->stream, "--export file sep '%%'%%%s%%%s%%%s%%\n",
	 type, regex, priority);
}

static void exports_generate4_aggstep(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 4);

	struct exports_aggregate_data *d = exports_generate_aggsetup(ctx);

	const char *entrypoint = (const char *)sqlite3_value_text(argv[0]);
	const char *type = (const char *)sqlite3_value_text(argv[1]);
	const char *regex = (const char *)sqlite3_value_text(argv[2]);
	const char *priority = (const char *)sqlite3_value_text(argv[3]);

	if(strchr(type, '%') != NULL ||
	 strchr(regex, '%') != NULL ||
	 strchr(priority, '%') != NULL) {
		sqlite3_result_error(ctx, "Unable to find suitable separator", -1);
	}

	fprintf(d->stream, "--export function %s sep '%%'%%%s%%%s%%%s%%\n",
	 entrypoint, type, regex, priority);
}

static void exports_generate_aggfinal(
 sqlite3_context *ctx) {
	struct exports_aggregate_data *d = exports_generate_aggsetup(ctx);

	fprintf(d->stream, "--end exports\n");
	fclose(d->stream);

	sqlite3_result_text(ctx, d->data, d->bytes, free);
}

static int register_exports(
 sqlite3 *db) {
	int rc = sqlite3_create_function_v2(db, "ld_exports_count", 1,
	 SQLITE_ANY, NULL, exports_countexports, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	rc = sqlite3_create_function_v2(db, "ld_exports_generate", 3,
	 SQLITE_ANY, NULL, NULL, exports_generate3_aggstep,
	 exports_generate_aggfinal, NULL);
	if(rc != SQLITE_OK) return rc;

	rc = sqlite3_create_function_v2(db, "ld_exports_generate", 4,
	 SQLITE_ANY, NULL, NULL, exports_generate4_aggstep,
	 exports_generate_aggfinal, NULL);
	if(rc != SQLITE_OK) return rc;

	return SQLITE_OK;
}
