/* File: common.h
   Time-stamp: <2011-07-15 08:50:50 gawen>

   Copyright (c) 2011 David Hauweele <david@hauweele.net>
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the University nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
   OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
   OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
   SUCH DAMAGE. */

#ifndef _COMMON_H_
#define _COMMON_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#define IO_SIZE   32768
#define DNAME_MAX 1024
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define S_BOOLEAN(a) (a ? "yes" : "no")

#define verbose(t, r, ...) if(r > t) fprintf (stderr, __VA_ARGS__)

#ifndef NDEBUG
# define UNPTR(a) a = NULL
#else
# define UNPTR(a) void
#endif

char * readlink_malloc_n(const char *filename, ssize_t *n);
size_t n_strncpy(char *dest, const char *src, size_t n);
char * strndup(const char *s, size_t n);

int xfork();
int xpipe(int pipefd[2]);
int xdup2(int oldfd, int newfd);
void * xmalloc(size_t size);
void * xrealloc(void *ptr, size_t size);
ssize_t xwrite(int fd, const void *buf, size_t count);
ssize_t xread(int fd, void *buf, size_t count);
ssize_t xxread(int fd, void *buf, size_t count);
int xstat(const char *path, struct stat *buf);
int xchown(const char *path, uid_t owner, gid_t group);
char * xreadlink_malloc_n(const char *filename, ssize_t *n);
int xutime(const char *filename, const struct utimbuf *times);

#endif /* _COMMON_H_ */
