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

static int deploy_dumpobjtbl(
 FILE *stream,
 const char *src,
 const char *dest,
 sqlite3 *db) {
	fprintf(stream,
	 "create table "
	 "	\"%s_obj\"( "
	 "	 loader text, "
	 "	 objref text, "
	 "	 obj blob, "
	 "	 exports text);\n",
	 dest);

	const char *sqltmpl =
	 "select "
	 "	quote(loader), "
	 "	quote(objref), "
	 "	quote(obj), "
	 "	quote(exports) "
	 "from "
	 "	\"%s_obj\"";

	char *sql = sqlite3_mprintf(sqltmpl, src);
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	sqlite3_free(sql);
	if(stmt == NULL || rc != SQLITE_OK) {
		return 1;
	}

	rc = sqlite3_step(stmt);
	while(rc == SQLITE_ROW) {
		fprintf(stream, "insert into \"%s_obj\" values(%s, %s, %s, %s);\n",
		 dest,
		 sqlite3_column_text(stmt, 0),
		 sqlite3_column_text(stmt, 1),
		 sqlite3_column_text(stmt, 2),
		 sqlite3_column_text(stmt, 3));

		rc = sqlite3_step(stmt);
	}

	sqlite3_finalize(stmt);

	if(rc != SQLITE_DONE) {
		return 1;
	}

	return 0;
}

static int deploy_dumpexptbl(
 FILE *stream,
 const char *src,
 const char *dest,
 sqlite3 *db) {
	fprintf(stream,
	 "create table "
	 "	\"%s_manifest\"( "
	 "	 type text, "
	 "	 regex text, "
	 "	 priority int, "
	 "	 entrypoint text, "
	 "	 objref text, "
	 "	 loader text);\n",
	 dest);

	const char *sqltmpl =
	 "select "
	 "	quote(type), "
	 "	quote(regex), "
	 "	priority, "
	 "	quote(entrypoint), "
	 "	quote(objref), "
	 "	quote(loader) "
	 "from "
	 "	\"%s_manifest\"";

	char *sql = sqlite3_mprintf(sqltmpl, src);
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	sqlite3_free(sql);
	if(stmt == NULL || rc != SQLITE_OK) {
		return 1;
	}

	rc = sqlite3_step(stmt);
	while(rc == SQLITE_ROW) {
		fprintf(stream,
		 "insert into \"%s_manifest\" values(%s, %s, %s, %s, %s, %s);\n",
		 dest,
		 sqlite3_column_text(stmt, 0),
		 sqlite3_column_text(stmt, 1),
		 sqlite3_column_text(stmt, 2),
		 sqlite3_column_text(stmt, 3),
		 sqlite3_column_text(stmt, 4),
		 sqlite3_column_text(stmt, 5));

		rc = sqlite3_step(stmt);
	}

	sqlite3_finalize(stmt);

	if(rc != SQLITE_DONE) {
		return 1;
	}

	return 0;
}
	
static void deploy_softwaresql2(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 2);	//thisname, targetname

	const char *src = sqlite3_value_text(argv[0]);
	const char *dest = sqlite3_value_text(argv[1]);

	char *result;
	size_t bytes;
	FILE *stream = open_memstream(&result, &bytes);
	assert(stream != NULL);

	if(deploy_dumpobjtbl(stream, src, dest,
	 sqlite3_context_db_handle(ctx)) != 0) {
		fclose(stream);
		free(result);

		sqlite3_result_error(ctx, "Unable to write object table", -1);
		return;
	}

	if(deploy_dumpexptbl(stream, src, dest,
	 sqlite3_context_db_handle(ctx)) != 0) {
		fclose(stream);
		free(result);

		sqlite3_result_error(ctx, "Unable to write manifest table", -1);
		return;
	}

	fclose(stream);

	sqlite3_result_text(ctx, result, -1, free);
}

static void deploy_softwaresql3(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 3);	//thisname, targetname, sqlfile

	const char *src = sqlite3_value_text(argv[0]);
	const char *dest = sqlite3_value_text(argv[1]);
	const char *sqlfname = sqlite3_value_text(argv[2]);

	FILE *stream = fopen(sqlfname, "w");
	if(stream == NULL) {
		sqlite3_result_error(ctx, "Unable to open file for writing", -1);
		return;
	}

	if(deploy_dumpobjtbl(stream, src, dest,
	 sqlite3_context_db_handle(ctx)) != 0) {
		fclose(stream);

		sqlite3_result_error(ctx, "Unable to write object table", -1);
		return;
	}

	if(deploy_dumpexptbl(stream, src, dest,
	 sqlite3_context_db_handle(ctx)) != 0) {
		fclose(stream);

		sqlite3_result_error(ctx, "Unable to write manifest table", -1);
		return;
	}

	fclose(stream);

	sqlite3_result_int(ctx, 1);
}

static void deploy_writeso(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 2);	//software, dir

	const char *software = sqlite3_value_text(argv[0]);
	const char *dir = sqlite3_value_text(argv[1]);

	char *sql = sqlite3_mprintf(
	 "select objref, obj from \"%s_obj\" where loader='so'", software);
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(sqlite3_context_db_handle(ctx),
	 sql, -1, &stmt, NULL);
	sqlite3_free(sql);
	if(rc != SQLITE_OK || stmt == NULL) {
		sqlite3_result_error(ctx, "Unable to prepare statement", -1);
		return;
	}

	size_t pathbuflen = strlen(dir) + 1 + 64 + 3 + 1;
	char *pathbuffer = (char *)malloc(pathbuflen);

	rc = sqlite3_step(stmt);
	while(rc == SQLITE_ROW) {
		int bytes = snprintf(pathbuffer, pathbuflen,
		 "%s/%s.so", dir, sqlite3_column_text(stmt, 0));
		assert(bytes < pathbuflen);

		const char *blob = sqlite3_column_blob(stmt, 1);
		int blobbytes = sqlite3_column_bytes(stmt, 1);

		FILE *f = fopen(pathbuffer, "w");
		assert(f != NULL);
		fwrite(blob, blobbytes, 1, f);
		fclose(f);

		rc = sqlite3_step(stmt);
	}
	free(pathbuffer);
	pathbuffer = NULL;
	sqlite3_finalize(stmt);

	if(rc != SQLITE_DONE) {
		sqlite3_result_error(ctx, "Failed to run underlying query", -1);
		return;
	}

	sqlite3_result_int(ctx, 1);
}

static int register_deploy(
 sqlite3 *db) {
	int rc = sqlite3_create_function_v2(db, "ld_deploy_softwaresql", 2,
	 SQLITE_ANY, NULL, deploy_softwaresql2, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	rc = sqlite3_create_function_v2(db, "ld_deploy_softwaresql", 3,
	 SQLITE_ANY, NULL, deploy_softwaresql3, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	rc = sqlite3_create_function_v2(db, "ld_deploy_writeso", 2,
	 SQLITE_ANY, NULL, deploy_writeso, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	return SQLITE_OK;
}
