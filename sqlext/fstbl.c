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

#include "dircursor.h"

struct fstbl_vtab {
	sqlite3_vtab vtab;

	char *path;
};

struct fstbl_vtab_cursor {
	sqlite3_vtab_cursor cur;

	struct dircursor dc;
};

static int fstbl_open(
 sqlite3_vtab *vtab,
 sqlite3_vtab_cursor **cur) {
	struct fstbl_vtab *v = (struct fstbl_vtab *)vtab;
	*cur = NULL;

	struct fstbl_vtab_cursor *c = 
	 sqlite3_malloc(sizeof(struct fstbl_vtab_cursor));
	if(c == NULL) return SQLITE_NOMEM;

	//we need to do this, so filter can close and re-open it
	//otherwise close will crash on an uninitialised struct
	dircursor_init(&c->dc, v->path);
	
	*cur = (sqlite3_vtab_cursor *)c;
	return SQLITE_OK;
}
 
static int fstbl_close(
 sqlite3_vtab_cursor *cur) {
	struct fstbl_vtab_cursor *c = (struct fstbl_vtab_cursor *)cur;
	dircursor_close(&c->dc);
	return SQLITE_OK;
}

static int fstbl_filter(
 sqlite3_vtab_cursor *cur,
 int idxnum,
 const char *idxstr,
 int argc,
 sqlite3_value **argv) {
	struct fstbl_vtab_cursor *c = (struct fstbl_vtab_cursor *)cur;
	dircursor_close(&c->dc);
	dircursor_init(&c->dc, ((struct fstbl_vtab *)(cur->pVtab))->path);
	return SQLITE_OK;
}

static int fstbl_next(
 sqlite3_vtab_cursor *cur) {
	struct fstbl_vtab_cursor *c = (struct fstbl_vtab_cursor *)cur;
	dircursor_next(&c->dc);
	while(!dircursor_finished(&c->dc) &&
	 !S_ISREG(dircursor_stat(&c->dc)->st_mode)) {
		dircursor_next(&c->dc);
	}
	return SQLITE_OK;
}

static int fstbl_eof(
 sqlite3_vtab_cursor *cur) {
	struct fstbl_vtab_cursor *c = (struct fstbl_vtab_cursor *)cur;
	return dircursor_finished(&c->dc);
}

static int fstbl_rowid(
 sqlite3_vtab_cursor *cur,
 sqlite3_int64 *rowid) {
	struct fstbl_vtab_cursor *c = (struct fstbl_vtab_cursor *)cur;
	//This may not work if we cross filesystem boundaries
	//but not sure what else to do
	*rowid = dircursor_stat(&c->dc)->st_ino;
	return SQLITE_OK;
}

static int fstbl_column(
 sqlite3_vtab_cursor *cur,
 sqlite3_context *ctx,
 int cidx) {
	assert(cidx >= 0 && cidx <= 1);
	struct fstbl_vtab_cursor *c = (struct fstbl_vtab_cursor *)cur;
	
	if(cidx == 0) {
		sqlite3_result_text(ctx,
		 dircursor_filename(&c->dc), -1, SQLITE_TRANSIENT);
		return SQLITE_OK;
	}

	sqlite3_result_int(ctx, dircursor_stat(&c->dc)->st_mtime);
	return SQLITE_OK;
}
	
static int fstbl_rename(
 sqlite3_vtab *vtab,
 const char *name) {
	return SQLITE_OK;
}

static int fstbl_bestindex(
 sqlite3_vtab *vtab,
 sqlite3_index_info *info) {
	return SQLITE_OK;
}

static int isgooddir(const char *path) {
	struct stat s;
	if(stat(path, &s) != 0) {
		return 0;
	}
	return S_ISDIR(s.st_mode);
}

static int fstbl_connect(
 sqlite3 *db,
 void *udp,
 int argc,
 const char *const *argv,
 sqlite3_vtab **vtab,
 char **errmsg) {
	*vtab = NULL;
	*errmsg = NULL;

	if(argc != 4) {
		*errmsg = sqlite3_mprintf("Wrong number of arguments");
		return SQLITE_ERROR;
	}
	if(!isgooddir(argv[3])) {
		*errmsg = sqlite3_mprintf("Bad directory");
		return SQLITE_ERROR;
	}

	struct fstbl_vtab *v = sqlite3_malloc(sizeof(struct fstbl_vtab));
	if(v == NULL) return SQLITE_NOMEM;
	v->vtab.zErrMsg = NULL;
	v->path = sqlite3_mprintf("%s", argv[3]);
	//;;; check v->path isn't null

	*vtab = (sqlite3_vtab *)v;
	
	int rc = sqlite3_declare_vtab(db,
	 "create table t(filename, mtime)");
	if(rc != SQLITE_OK) {
		return SQLITE_ERROR;
	}

	return SQLITE_OK;
}

static int fstbl_disconnect(sqlite3_vtab *vtab) {
	struct fstbl_vtab *v = (struct fstbl_vtab *)vtab;
	sqlite3_free(v->path);
	sqlite3_free(v);
	return SQLITE_OK;
}

static sqlite3_module fstbl_module = {
 1,
 fstbl_connect,
 fstbl_connect,
 fstbl_bestindex,
 fstbl_disconnect,
 fstbl_disconnect,
 fstbl_open,
 fstbl_close,
 fstbl_filter,
 fstbl_next,
 fstbl_eof,
 fstbl_column,
 fstbl_rowid,
 NULL,
 NULL,
 NULL,
 NULL,
 NULL,
 NULL,
 fstbl_rename
};

static int register_scandir(
 sqlite3 *db) {
	int rc = sqlite3_create_module(db, "ldtbl_scandir", &fstbl_module, NULL);
	return rc;
}
