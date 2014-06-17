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
#ifndef __DIRCURSOR_HEADER__
#define __DIRCURSOR_HEADER__

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

struct dcrecord;

struct dircursor {
    struct dcrecord *dirstack;
    DIR *stream;
    
    struct dirent *entry;
    struct stat stat;
    
    size_t fpbytes;
    char *fpath;
};

    
void dircursor_init(struct dircursor *dc, const char *path);
void dircursor_addpath(struct dircursor *dc, const char *path);
void dircursor_close(struct dircursor *dc);

void dircursor_next(struct dircursor *dc);
int dircursor_finished(struct dircursor *dc);

const char *dircursor_filename(struct dircursor *dc);
const struct stat *dircursor_stat(struct dircursor *dc); 

#endif
