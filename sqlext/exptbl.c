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
 * We should create the virtual table as follows
 *
 * create virtual table
 * 		manifest
 * using
 * 		ldtbl_exports(tblname, objrefcol, expdefcol, any_other_columns);
 *
 * The first arg is the table that we should query
 * The second is the name of the column that identifies the file
 * The final column is the name of the column containing the export data
 * The remaining args are columns from tblname that we should pull to
 * be readily available.
 *
 * Internally, each cursor should run select srccol, exdefcol,
 *  any_other_columns from tblname
 * For each response we should then iterate over the exports making the
 * following columns available
 *
 * type
 * regex
 * priority
 * entrypoint
 * objref
 * any_other_columns
 *
 * The intention is run the following query on the table
 *
 * select
 *		entrypoint, objref, regex, any_other_columns
 * from
 * 		manifest
 * where
 * 		type=$1 and
 * 		ld_regmatch(regex, $2)==1
 * order by
 *		priority desc
 * limit
 *		1
 *
 * The regex is necessary so we can run it again to get the arguments objref
 * lets us look up the object to load in the obj table
 */

struct exptbl_vtab
{
	sqlite3_vtab vtab;

	sqlite3 *db;
	const char *query;
};

struct exptbl_vtab_cursor
{
	sqlite3_vtab_cursor cur;

	sqlite3_stmt *stmt;
	int done;
	struct expcursor ec;
};

static int exptbl_open(
 sqlite3_vtab *vtab,
 sqlite3_vtab_cursor **cur) {
	struct exptbl_vtab *v = (struct exptbl_vtab *)vtab;
	*cur = NULL;

	sqlite3_stmt *stmt;
	if(sqlite3_prepare_v2(v->db, v->query, -1, &stmt, NULL) != SQLITE_OK) {
		return SQLITE_ERROR;
	}

	struct exptbl_vtab_cursor *c =
	 sqlite3_malloc(sizeof(struct exptbl_vtab_cursor));
	if(c == NULL) return SQLITE_NOMEM;

	c->stmt = stmt;
	//this is necessary so we can call ec_destroy in filter callback
	expcursor_init(&c->ec, "--begin exports\n--end exports\n", -1);
	
	*cur = (sqlite3_vtab_cursor *)c;
	return SQLITE_OK;
}

static int exptbl_close(
 sqlite3_vtab_cursor *cur) {
	struct exptbl_vtab_cursor *c = (struct exptbl_vtab_cursor *)cur;
	
	expcursor_destroy(&c->ec);

	int rc = SQLITE_OK;
	if(c->stmt != NULL) {
		if(sqlite3_finalize(c->stmt) != SQLITE_OK) {
			rc = SQLITE_ERROR;
		}
		c->stmt = NULL;
	}

	return rc;
}
		
static int exptbl_next(
 sqlite3_vtab_cursor *cur);

static int exptbl_filter(
 sqlite3_vtab_cursor *cur,
 int idxnum,
 const char *idxstr,
 int argc,
 sqlite3_value **argv) {
	struct exptbl_vtab_cursor *c = (struct exptbl_vtab_cursor *)cur;

	if(sqlite3_reset(c->stmt) != SQLITE_OK) return SQLITE_ERROR;
	c->done = 0;
	expcursor_destroy(&c->ec);

	return exptbl_next(cur);
}

static int exptbl_next(
 sqlite3_vtab_cursor *cur) {
	struct exptbl_vtab_cursor *c = (struct exptbl_vtab_cursor *)cur;

	assert(c->done == 0);

	if(!expcursor_finished(&c->ec)) {
		expcursor_next(&c->ec);
	}

	if(expcursor_finished(&c->ec)) {
		//find a new set of exports
		while(expcursor_finished(&c->ec)) {
			expcursor_destroy(&c->ec);
			
			int rc = sqlite3_step(c->stmt);
			if(rc == SQLITE_DONE) {
				c->done = 1;
				return SQLITE_OK;
			}

			assert(rc == SQLITE_ROW);
			assert(sqlite3_column_count(c->stmt) >= 2);
			if(sqlite3_column_type(c->stmt, 1) == SQLITE_NULL) {
				sqlite3_free(cur->pVtab->zErrMsg);
				cur->pVtab->zErrMsg =
				 sqlite3_mprintf("objref '%s' has NULL exports",
				 sqlite3_column_text(c->stmt, 0));
				return SQLITE_MISUSE;
			}

			expcursor_init(&c->ec, sqlite3_column_text(c->stmt, 1), -1);
		}
	}

	return SQLITE_OK;
}

static int exptbl_eof(
 sqlite3_vtab_cursor *cur) {
	struct exptbl_vtab_cursor *c = (struct exptbl_vtab_cursor *)cur;

	return c->done;
}

static int exptbl_rowid(
 sqlite3_vtab_cursor *cur,
 sqlite3_int64 *rowid) {
	return SQLITE_ERROR;
}

static int exptbl_column(
 sqlite3_vtab_cursor *cur,
 sqlite3_context *ctx,
 int cidx) {
	struct exptbl_vtab_cursor *c = (struct exptbl_vtab_cursor *)cur;
	int extracols = sqlite3_column_count(c->stmt) - 2;
	assert(cidx >= 0 && cidx < 5+extracols);

	if(expcursor_failedtoparse(&c->ec)) {
/* Don;t know how to error in column function
		char buffer[1001];
		snprintf(buffer, 1000, "Failed to parse input line '%s'",
		 expcursor_inputline(&c->ec)); 
		sqlite3_result_text(ctx, buffer, -1, SQLITE_TRANSIENT);
*/
		sqlite3_result_null(ctx);
		return;
	}

	switch(cidx) {
		case 0:
			sqlite3_result_text(ctx, expcursor_type(&c->ec),
			 -1, SQLITE_TRANSIENT);
			return SQLITE_OK;
		case 1:
			sqlite3_result_text(ctx, expcursor_regex(&c->ec),
			 -1, SQLITE_TRANSIENT);
			return SQLITE_OK;
		case 2:
		{
			int p = 0;
			if(sscanf(expcursor_priority(&c->ec), "%d", &p) != 1) {
				sqlite3_result_null(ctx);
			} else {
				sqlite3_result_int(ctx, p);
			}
			return SQLITE_OK;
		}
		case 3:
			sqlite3_result_text(ctx, expcursor_entrypoint(&c->ec),
			 -1, SQLITE_TRANSIENT);
			return SQLITE_OK;
		case 4:
			sqlite3_result_value(ctx, sqlite3_column_value(c->stmt, 0));
			return SQLITE_OK;
		default:
			sqlite3_result_value(ctx, sqlite3_column_value(c->stmt, cidx-5+2));
			return SQLITE_OK;
	}

	assert("Unreachable code" == 0x0);
	return SQLITE_OK;
}

static int exptbl_rename(
 sqlite3_vtab *vtab,
 const char *name) {
	return SQLITE_OK;
}

static int exptbl_bestindex(
 sqlite3_vtab *vtab,
 sqlite3_index_info *info) {
	return SQLITE_OK;
}

static int exptbl_cmdparser(
 int argc,
 const char *const *argv,
 char **query,
 char **vtab,
 char **errmsg) {
	if(argc < 6) {
		*errmsg = sqlite3_mprintf("Wrong number of arguments");
		return SQLITE_ERROR;
	}

	int i;
	for(i=3;i!=argc;++i) {
		if(strchr(argv[i], '"') != NULL) {
			*errmsg = sqlite3_mprintf(
			 "Table/Col names can't contain double quote");
			return SQLITE_ERROR;
		}
	}

	const char *tbl = argv[3];
	const char *objref = argv[4];
	const char *exportdata = argv[5];

	size_t bytes;
	FILE *f = open_memstream(query, &bytes);
	assert(f != NULL);
	fprintf(f, "select \"%s\", \"%s\"", objref, exportdata);
	for(i=6;i!=argc;++i) {
		fprintf(f, ", \"%s\"", argv[i]);
	}
	fprintf(f, " from \"%s\"", tbl);
	fclose(f);
	f = NULL;

	f = open_memstream(vtab, &bytes);
	assert(f != NULL);	
	fprintf(f,
	 "create table t(type text, regex text, priority int, entrypoint text,"
	 " objref text");
	for(i=6;i!=argc;++i) {
		fprintf(f, ", \"%s\" text", argv[i]);
	}
	fprintf(f, ")");
	fclose(f);
	f = NULL;

	return SQLITE_OK;
}

static exptbl_connect(
 sqlite3 *db,
 void *udp,
 int argc,
 const char *const *argv,
 sqlite3_vtab **vtab,
 char **errmsg) {
	*vtab = NULL;
	*errmsg = NULL;

	char *query, *vtabsql;
	if(exptbl_cmdparser(argc, argv, &query, &vtabsql, errmsg) != SQLITE_OK) {
		return SQLITE_ERROR;
	}

	int rc = sqlite3_declare_vtab(db, vtabsql);
	free(vtabsql);
	vtabsql = NULL;
	if(rc != SQLITE_OK) {
		*errmsg = sqlite3_mprintf(
		 "Unable to declare vtab");
		free(query);
		return SQLITE_ERROR;
	}

	sqlite3_stmt *stmt;
	rc = sqlite3_prepare_v2(db, query, -1, &stmt, 0);
	if(rc != SQLITE_OK || stmt == NULL) {
		*errmsg = sqlite3_mprintf(
		 "Unable to prepare query on underlying table");
		return SQLITE_ERROR;
	}
	sqlite3_finalize(stmt);
	stmt = NULL;

	struct exptbl_vtab *v = sqlite3_malloc(sizeof(struct exptbl_vtab));
	if(v == NULL) return SQLITE_NOMEM;
	v->vtab.zErrMsg = NULL;
	v->db = db;
	v->query = query;

	*vtab = (sqlite3_vtab *)v;

	return SQLITE_OK;
}

static int exptbl_disconnect(
 sqlite3_vtab *vtab) {
	struct exptbl_vtab *v = (struct exptbl_vtab *)vtab;

	v->db = NULL;
	free((void *)v->query);
}

static sqlite3_module exptbl_module = {
 1,
 exptbl_connect,
 exptbl_connect,
 exptbl_bestindex,
 exptbl_disconnect,
 exptbl_disconnect,
 exptbl_open,
 exptbl_close,
 exptbl_filter,
 exptbl_next,
 exptbl_eof,
 exptbl_column,
 exptbl_rowid,
 NULL,
 NULL,
 NULL,
 NULL,
 NULL,
 NULL,
 exptbl_rename
};

static int register_unpackexports(
 sqlite3 *db) {
	int rc = sqlite3_create_module(db, "ldtbl_exports", &exptbl_module, NULL);
	return rc;
}
