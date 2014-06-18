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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
#include <openssl/sha.h>

struct mappedfile {
	size_t bytes;
	char *contents;
};

static void openmappedfile(
 struct mappedfile *mf,
 const char *path) {
	assert(mf != NULL);

	mf->bytes = 0;
	mf->contents = NULL;

	struct stat s;
	int rc = stat(path, &s);

	if(rc != 0 || !S_ISREG(s.st_mode)) {
		//"Unable to stat file"
		return;
	}

	//by opening /dev/zero for an extra page above filesize
	//it guarantees we will always have a terminating zero
	int fd = open("/dev/zero", O_RDONLY);
	assert(fd >= 0);
	char *contents = mmap(NULL, s.st_size + sysconf(_SC_PAGESIZE),
	 PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if(contents == NULL) {
		//"Unable to mmap /dev/zero"
		return;
	}

	fd = open(path, O_RDONLY);
	if(fd == -1) {
		munmap(contents, s.st_size + sysconf(_SC_PAGESIZE));
		//"Unable to open file"
		return;
	}

	if(mmap(contents, s.st_size, PROT_READ,
	 MAP_PRIVATE|MAP_FIXED, fd, 0) == NULL) {
		assert("Impossible?" == 0x0);
	}
	close(fd);

	mf->contents = contents;
	mf->bytes = s.st_size;
}

static void closemappedfile(struct mappedfile *mf) {
	assert(mf != NULL);
	munmap(mf->contents, mf->bytes + sysconf(_SC_PAGESIZE));
}

static void readfile_plain(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 1);

	struct mappedfile mf;
	openmappedfile(&mf, (const char *)sqlite3_value_text(argv[0]));

	if(mf.contents == NULL) {
		sqlite3_result_error(ctx, "unable to open file", -1);
		return;
	}

	sqlite3_result_blob(ctx, mf.contents, mf.bytes, SQLITE_TRANSIENT);
	closemappedfile(&mf);
}

static void readfile_sha256(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 1);

	struct mappedfile mf;
	openmappedfile(&mf, (const char *)sqlite3_value_text(argv[0]));

	if(mf.contents == NULL) {
		sqlite3_result_error(ctx, "unable to open file", -1);
		return;
	}

	SHA256_CTX hashctx;
	SHA256_Init(&hashctx);
	SHA256_Update(&hashctx, mf.contents, mf.bytes);
	
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_Final(hash, &hashctx);

	char *hashtext =
	 (char *)sqlite3_malloc(2*SHA256_DIGEST_LENGTH + 1);

	int i;
	for(i=0;i<SHA256_DIGEST_LENGTH;++i) {
		sprintf(hashtext + 2*i, "%02x", hash[i]);
	}

	sqlite3_result_text(ctx, hashtext, 2*SHA256_DIGEST_LENGTH,
	 sqlite3_free);
	closemappedfile(&mf);
}

static int compiledchunkwriter(
 lua_State *l,
 const void *p,
 size_t sz,
 void *fh) {
	return fwrite(p, sz, 1, fh) == 0;
}

static void readfile_compilelua(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 1);

	struct mappedfile mf;
	openmappedfile(&mf, (const char *)sqlite3_value_text(argv[0]));

	if(mf.contents == NULL) {
		sqlite3_result_error(ctx, "unable to open file", -1);
		return;
	}

	lua_State *l = luaL_newstate();
	
	if(luaL_loadbufferx(l, mf.contents, mf.bytes,
	 (const char *)sqlite3_value_text(argv[0]), "t") != LUA_OK) {
		sqlite3_result_error(ctx, lua_tostring(l, -1), -1);
		lua_close(l);
		return;
	}

	char *buffer;
	size_t bytes;
	FILE *dumpstream = open_memstream(&buffer, &bytes);
	assert(dumpstream != NULL);

	int rc = lua_dump(l, compiledchunkwriter, dumpstream);
	lua_close(l);
	fclose(dumpstream);
	assert(rc == 0);

	sqlite3_result_blob(ctx, buffer, bytes, free);
}

static void readfile_exportstext(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 1);

	struct mappedfile mf;
	openmappedfile(&mf, (const char *)sqlite3_value_text(argv[0]));

	if(mf.contents == NULL) {
		sqlite3_result_error(ctx, "unable to open file", -1);
		return;
	}

	const char *begin = mf.contents;
	const char *next = mf.contents;
	int count = 0;
	while(1) {
		next = strstr(begin, "--begin exports\n");
		if(next == NULL) break;
		++count;
		begin = next+1;
	}
	--begin;

	if(count == 0) {
		closemappedfile(&mf);
		sqlite3_result_null(ctx);
		return;
	}
	if(count > 1) {
		closemappedfile(&mf);
		sqlite3_result_error(ctx, "Multiple '--begin exports' found", -1);
		return;
	}

	const char *start = begin;

	begin = mf.contents;
	next = mf.contents;
	count = 0;
	while(1) {
		next = strstr(begin, "\n--end exports\n");
		if(next == NULL) break;
		++count;
		begin = next+1;
	}
	--begin;

	if(count == 0) {
		closemappedfile(&mf);
		sqlite3_result_error(ctx, "No '--end exports' found", -1);
		return;
	}
	if(count > 1) {
		closemappedfile(&mf);
		sqlite3_result_error(ctx, "Multiple '--end exports' found", -1);
		return;
	}

	if(start > begin) {
		closemappedfile(&mf);
		sqlite3_result_error(ctx,
		 "'--end exports' found before '--begin exports'", -1);
		return;
	}

	sqlite3_result_text(ctx, start, begin - start + strlen("\n--end exports"),
	 SQLITE_TRANSIENT);

	closemappedfile(&mf);
}

static void readfile_exportsso(
 sqlite3_context *ctx,
 int argc,
 sqlite3_value **argv) {
	assert(argc == 1);

	void *lib = dlopen((const char *)sqlite3_value_text(argv[0]),
	 RTLD_LAZY | RTLD_LOCAL);
	if(lib == NULL) {
		sqlite3_result_null(ctx);
		return;
	}

	const char *export = (const char *)dlsym(lib, "ld_exports");
	if(export == NULL) {
		dlclose(lib);
		sqlite3_result_error(ctx, "unable to find ld_exports", -1);
		return;
	}

	const char *expected = "--begin exports\n";
	if(strncmp(export, expected, strlen(expected)) != 0) {
		dlclose(lib);
		sqlite3_result_error(ctx, "no '--begin exports'", -1);
		return;
	}

	//;;;
	//check for a terminating zero.

	if(strstr(export, "\n--end exports\n") == NULL) {
		dlclose(lib);
		sqlite3_result_error(ctx, "no '--end exports'", -1);
		return;
	}

	sqlite3_result_text(ctx, export, -1, SQLITE_TRANSIENT);
	dlclose(lib);
}
	
static int register_readfile(
 sqlite3 *db) {
	int rc = sqlite3_create_function_v2(db, "ld_getfile_contents", 1,
	 SQLITE_ANY, NULL, readfile_plain, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	rc = sqlite3_create_function_v2(db, "ld_getfile_compiledlua" ,1,
	 SQLITE_ANY, NULL, readfile_compilelua, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	rc = sqlite3_create_function_v2(db, "ld_getfile_sha256" ,1,
	 SQLITE_ANY, NULL, readfile_sha256, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	rc = sqlite3_create_function_v2(db, "ld_getfile_exportstext" ,1,
	 SQLITE_ANY, NULL, readfile_exportstext, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	rc = sqlite3_create_function_v2(db, "ld_getfile_exportssymbol" ,1,
	 SQLITE_ANY, NULL, readfile_exportsso, NULL, NULL, NULL);
	if(rc != SQLITE_OK) return rc;

	return SQLITE_OK;
}
