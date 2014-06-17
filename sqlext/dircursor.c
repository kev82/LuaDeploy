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
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <regex.h>

#include "dircursor.h"

struct dcrecord {
    struct dcrecord *next;
    size_t len;
    char s[1];
};

static void record_pushfront(struct dcrecord **cur, const char *data, int len) {
    //len should not include terminating 0
    if(len == -1) len = strlen(data);

    struct dcrecord *rv = (struct dcrecord *)malloc(sizeof(struct dcrecord) + len);
    assert(rv != NULL);
    rv->next = *cur;
    rv->len = len;
    strncpy(rv->s, data, len);
    rv->s[len] = 0;
    
    *cur = rv;
}

static void record_popfront(struct dcrecord **r) {
    assert(r != NULL && *r != NULL);
    struct dcrecord *f = *r;
    *r = (*r)->next;
    free(f);
}
    
static const char *record_front(struct dcrecord *r, size_t *plen) {
    if(plen !=NULL) {
        *plen = r->len;
    }  
    return r->s;
}

static void record_pushback(struct dcrecord **r, const char *data, size_t len) {
    if(*r == NULL) return record_pushfront(r,data, len);
    
    //len should not include terminating 0
    if(len == -1) len = strlen(data);

    struct dcrecord *tail = *r;
    while(tail->next != NULL) {
        tail = tail->next;
    }
    
    tail->next = (struct dcrecord *)malloc(sizeof(struct dcrecord) + len);
    assert(tail->next != NULL);
    tail->next->next = NULL;
    tail->next->len = len;
    strncpy(tail->next->s, data, len);
    tail->next->s[len] = 0;
}

void dircursor_next(struct dircursor *dc) {
    if(dc->stream == NULL) {
        assert(dc->dirstack == NULL);
        return;
    }
        
    dc->entry = readdir(dc->stream);
    if(dc->entry == NULL) {
        closedir(dc->stream);
        dc->stream = NULL;
        record_popfront(&dc->dirstack);
        
        if(dc->dirstack == NULL) {
            return;
        }
        
        dc->stream = opendir(record_front(dc->dirstack, NULL));
        assert(dc->stream != NULL);
        dc->entry = readdir(dc->stream);
    }
    
    if(memcmp(dc->entry->d_name, ".", 2) == 0 ||
     memcmp(dc->entry->d_name, "..", 3) == 0) {
        dircursor_next(dc);
        return;
    }
    
    size_t fpbytes = 0;
    record_front(dc->dirstack, &fpbytes);
    fpbytes += 1 + strlen(dc->entry->d_name) + 1;
    
    if(fpbytes > dc->fpbytes) {
        while(fpbytes > dc->fpbytes) dc->fpbytes *= 2;
        dc->fpath = (char *)realloc(dc->fpath, dc->fpbytes);
        assert(dc->fpath != NULL);
    }
    snprintf(dc->fpath, fpbytes, "%s/%s",
     record_front(dc->dirstack, NULL),
     dc->entry->d_name);
     
    if(lstat(dc->fpath, &dc->stat) != 0) {
        assert(0);
        return dircursor_next(dc);
    }
    
    if(S_ISDIR(dc->stat.st_mode)) {
        record_pushback(&dc->dirstack, dc->fpath, -1);
        dircursor_next(dc);
        return;
    }
}
    
void dircursor_init(struct dircursor *dc, const char *path) {
    dc->dirstack = NULL;
    dc->stream = NULL;
	dc->fpbytes = 0;
    
    if(lstat(path, &dc->stat) != 0) {
        return;
    }
    
    if(!S_ISDIR(dc->stat.st_mode)) {
        return;
    }
    
    dc->fpbytes = 50;
    dc->fpath = (char *)malloc(dc->fpbytes);
    assert(dc->fpath != NULL);
    
    dc->stream = opendir(path);
    record_pushback(&dc->dirstack, path, -1);
    
    dircursor_next(dc);
}

void dircursor_addpath(struct dircursor *dc, const char *path) {
	assert(dc->stream != NULL);
	record_pushback(&dc->dirstack, path, -1);
}

void dircursor_close(struct dircursor *dc) {
	assert(dc != NULL);
	if(dc->fpath != NULL) {
		free(dc->fpath);
		dc->fpath = NULL;
		dc->fpbytes = 0;
	}

	if(dc->stream != NULL) {
		closedir(dc->stream);
		dc->stream = NULL;
	}

	while(dc->dirstack != NULL) {
		record_popfront(&dc->dirstack);
	}
}

int dircursor_finished(struct dircursor *dc) {
    return dc->dirstack == NULL && dc->stream == NULL;
}

const char *dircursor_filename(struct dircursor *dc) {
    return dc->fpath;
}

const struct stat *dircursor_stat(struct dircursor *dc) {
	return &dc->stat;
}
