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
int ldext_init(
 sqlite3 *db,
 const char **errmsg,
 const sqlite3_api_routines *api) {
	SQLITE_EXTENSION_INIT2(api);

	int rc = register_scandir(db);
	if(rc != SQLITE_OK) return rc;

	rc = register_readfile(db);
	if(rc != SQLITE_OK) return rc;

	rc = register_exports(db);
	if(rc != SQLITE_OK) return rc;

	rc = register_unpackexports(db);
	if(rc != SQLITE_OK) return rc;

	rc = register_loader(db);
	if(rc != SQLITE_OK) return rc;

	rc = register_deploy(db);
	if(rc != SQLITE_OK) return rc;

	return SQLITE_OK;
}
