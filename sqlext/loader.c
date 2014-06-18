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

/*
 * These are support functions that the actual loader can call
 * in order get the information necessary to load and call the
 * appropriate object/entrypoint
 *
 * The ld_loader_regmatch function takes a regex and some data
 * If there is no match it returns null, else it returns a lua
 * table with all the submatches
 *
 * The ld_loader_getobj function simply takes a software name
 * along with an object reference and returns the appropriate object
 * essentially running select obj from software_obj where objref = ?
 *
 * The third function ld_loader_search takes the request type and the
 * request for a particular piece of software and returns either null
 * or the loading instructions
 *
 * ld_loader_search(
 *		software
 *		reqtype (cmd/module/etc)
 *		request)
 *
 * This should return either NULL if there are no matches in the manifest
 * or the following lua code:
 *
 * software="name of the software" --used to figure out obj table
 * loader="the type of loader to use"
 * objref="ref of the object to load" --pass to ld_loader_getobj
 * entrypoint="The entrypoint in the object" (null if should just exec the file)
 * args = {
 * 		"The list of arguments parsed from the regular expression"
 * }
 *
 * The function should run the following query to get the results
 * 
 * select
 *		loader, objref, entrypoint,
 *		ld_loader_regmatch(regex, $(request)) as params
 * from
 *		$(software)_tbl
 * where
 *		type = $(objtype) and
 *		ld_loader_regmatch(regex, $(request)) not null
 * order by
 *		priority desc
 * limit
 *		1;
 *
 * The loader needs to get the full list of shared objects in
 * a piece of software. The function ld_loader_objrefs takes
 * two arguments the software, and the loader. It returns a lua table
 * where each key is an objref of the specified loader type
 * It returns a empty table if there are no matches
 */
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <regex.h>
#include <stdio.h>

static void loader_regmatch(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 2);

	regex_t r;
	int rc = regcomp(&r, (const char *)sqlite3_value_text(argv[0]),
	 REG_EXTENDED);
	if(rc != 0) {
		sqlite3_result_error(ctx, "Unable to compile regex", -1);
		return;
	}

	regmatch_t matches[20];
	rc = regexec(&r, (const char *)sqlite3_value_text(argv[1]),
	 20, matches, 0);
	regfree(&r);

	if(rc != 0) {
		sqlite3_result_null(ctx);
		return;
	}

	if(matches[19].rm_so != -1) {
		sqlite3_result_error(ctx, "Too many captures", -1);
		return;
	}

	if(matches[1].rm_so == -1) {
		sqlite3_result_text(ctx, "{}", -1, SQLITE_STATIC);
		return;
	}

	const char *src = (const char *)sqlite3_value_text(argv[1]);

	size_t bytes;
	char *buffer;
	FILE *output = open_memstream(&buffer, &bytes);
	assert(output != NULL);

	fprintf(output, "{ \"");
	fwrite(src+matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so,
	 1, output);
	fprintf(output, "\"");

	int i=2;
	while(matches[i].rm_so != -1) {
		fprintf(output, ", \"");
		fwrite(src+matches[i].rm_so, matches[i].rm_eo - matches[i].rm_so,
		 1, output);
		fprintf(output, "\"");
		++i;
	}

	fprintf(output, "}");

	fclose(output);
	sqlite3_result_text(ctx, buffer, -1, free);
} 

static void loader_getobj(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 2);

	sqlite3 *db = sqlite3_context_db_handle(ctx);
	const char *swname = (const char *)sqlite3_value_text(argv[0]);
	const char *objref = (const char *)sqlite3_value_text(argv[1]);

	char *query = sqlite3_mprintf(
	 "select obj from \"%s_obj\" where objref=?", swname);

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
	sqlite3_free(query);
	if(rc != SQLITE_OK || stmt == NULL) {
		sqlite3_result_error(ctx, "Unable to prepare statement", -1);
		return;
	}

	rc |= sqlite3_bind_text(stmt, 1, objref, -1, SQLITE_STATIC);

	if(rc != SQLITE_OK) {
		sqlite3_result_error(ctx, "Failed to bind parameters", -1);
		return;
	}

	rc = sqlite3_step(stmt);
	if(rc == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_result_null(ctx);
		return;
	}

	if(rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		sqlite3_result_error(ctx, "Error in underlying query", -1);
		return;
	}

	sqlite3_result_value(ctx, sqlite3_column_value(stmt, 0));
	sqlite3_finalize(stmt);
}

static void loader_search(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 3);

	sqlite3 *db = sqlite3_context_db_handle(ctx);
	const char *swname = (const char *)sqlite3_value_text(argv[0]);
	const char *exptype = (const char *)sqlite3_value_text(argv[1]);
	const char *request = (const char *)sqlite3_value_text(argv[2]);

	char *query = sqlite3_mprintf(
	 "select loader, objref, entrypoint,"
	 " ld_loader_regmatch(regex, ?) as params"
	 " from \"%s_manifest\" where type=? and"
	 " ld_loader_regmatch(regex, ?) not null"
	 " order by priority desc limit 1", swname);

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
	sqlite3_free(query);
	if(rc != SQLITE_OK || stmt == NULL) {
		sqlite3_result_error(ctx, "Unable to prepare statement", -1);
		return;
	}

	rc |= sqlite3_bind_text(stmt, 1, request, -1, SQLITE_STATIC);
	rc |= sqlite3_bind_text(stmt, 2, exptype, -1, SQLITE_STATIC);
	rc |= sqlite3_bind_text(stmt, 3, request, -1, SQLITE_STATIC);

	if(rc != SQLITE_OK) {
		sqlite3_result_error(ctx, "Failed to bind parameters", -1);
		return;
	}

	rc = sqlite3_step(stmt);
	if(rc == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_result_null(ctx);
		return;
	}

	if(rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		sqlite3_result_error(ctx, "Error in underlying query", -1);
		return;
	}

	size_t bytes;
	char *result;
	FILE *rstream = open_memstream(&result, &bytes);
	
	fprintf(rstream, "software = [=[%s]=]\n", swname);		
	fprintf(rstream, "loader = [=[%s]=]\n", sqlite3_column_text(stmt, 0));
	fprintf(rstream, "objref = [=[%s]=]\n", sqlite3_column_text(stmt, 1));
	if(sqlite3_column_type(stmt, 2) == SQLITE_NULL) {
		fprintf(rstream, "entrypoint = nil\n");
	} else {
		fprintf(rstream, "entrypoint = [=[%s]=]\n",
		 sqlite3_column_text(stmt, 2));
	}
	fprintf(rstream, "args = %s\n", sqlite3_column_text(stmt, 3));

	sqlite3_finalize(stmt);

	fclose(rstream);
	sqlite3_result_text(ctx, result, -1, free);
}

static void loader_objrefs(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 2);

	sqlite3 *db = sqlite3_context_db_handle(ctx);
	const char *swname = (const char *)sqlite3_value_text(argv[0]);
	const char *loader = (const char *)sqlite3_value_text(argv[1]);

	char *query = sqlite3_mprintf(
	 "select objref from \"%s_obj\" where loader=?", swname);

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
	sqlite3_free(query);
	if(rc != SQLITE_OK || stmt == NULL) {
		sqlite3_result_error(ctx, "Unable to prepare statement", -1);
		return;
	}

	rc |= sqlite3_bind_text(stmt, 1, loader, -1, SQLITE_STATIC);

	if(rc != SQLITE_OK) {
		sqlite3_result_error(ctx, "Failed to bind parameters", -1);
		return;
	}

	char *result;
	size_t bytes;
	FILE *rstream = open_memstream(&result, &bytes);
	assert(rstream != NULL);

	fprintf(rstream, "{");

	rc = sqlite3_step(stmt);
	while(rc != SQLITE_DONE) {
		if(rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			fclose(rstream);
			free(result);

			sqlite3_result_error(ctx, "Underlying query failed", -1);
			return;
		}

		fprintf(rstream, " [\"%s\"] = true; ", sqlite3_column_text(stmt, 0));
		rc = sqlite3_step(stmt);
	}

	fprintf(rstream, "}");
	fclose(rstream);
	sqlite3_finalize(stmt);

	sqlite3_result_text(ctx, result, -1, free);
}

static int register_loader(
 sqlite3 *db) {
	int rc = sqlite3_create_function_v2(db, "ld_loader_regmatch", 2,
	 SQLITE_ANY, NULL, loader_regmatch, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	rc = sqlite3_create_function_v2(db, "ld_loader_getobj", 2,
	 SQLITE_ANY, NULL, loader_getobj, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	rc = sqlite3_create_function_v2(db, "ld_loader_search", 3,
	 SQLITE_ANY, NULL, loader_search, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	rc = sqlite3_create_function_v2(db, "ld_loader_objrefs", 2,
	 SQLITE_ANY, NULL, loader_objrefs, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	return SQLITE_OK;
}
