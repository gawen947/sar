/* File: common.c

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

#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <utime.h>
#include <errno.h>
#include <err.h>

#include "sar.h"
#include "iobuf.h"
#include "common.h"

#define SAFE_CALL0(name, erron, msg, ret)       \
  ret x ## name () {                            \
    register ret t = name ();                   \
    if(t erron)                                 \
      err(EXIT_FAILURE, msg);                   \
    return t; }

#define SAFE_CALL1(name, erron, msg, ret, type) \
  ret x ## name (type arg) {                    \
    register ret t = name (arg);                \
    if(t erron)                                 \
      err(EXIT_FAILURE, msg);                   \
    return t; }

#define SAFE_CALL2(name, erron, msg, ret, type1, type2) \
  ret x ## name (type1 arg1, type2 arg2) {              \
    register ret t = name (arg1, arg2);                 \
    if(t erron)                                         \
      err(EXIT_FAILURE, msg);                           \
    return t; }

#define SAFE_CALL3(name, erron, msg, ret, type1, type2, type3) \
  ret x ## name (type1 arg1, type2 arg2, type3 arg3) {         \
    register ret t = name (arg1, arg2, arg3);                  \
    if(t erron)                                                \
      err(EXIT_FAILURE, msg);                                  \
    return t; }

SAFE_CALL0(fork, < 0, "cannot fork", int)

SAFE_CALL1(pipe, < 0, "cannot create pipe", int, int *)
SAFE_CALL1(malloc, == NULL, "out of memory", void *, size_t)
SAFE_CALL1(chdir, < 0, "cannot change directory", int, const char *)

SAFE_CALL2(realloc, == NULL, "out of memory", void *, void *, size_t)
SAFE_CALL2(stat, < 0, "IO stat error", int, const char *, struct stat *)
SAFE_CALL2(dup2, < 0, "cannot duplicate file descriptors", int, int, int)
SAFE_CALL2(getcwd, == NULL, "cannot get current working directory", char *,
           char *, size_t)
SAFE_CALL2(iobuf_skip, < 0, "cannot seek", int, iofile_t, off_t)
SAFE_CALL2(readlink_malloc_n, == NULL, "IO readlink error", char *,
           const char *, ssize_t *)
SAFE_CALL2(utime, < 0, "IO chattr error", int, const char *,
           const struct utimbuf *)

SAFE_CALL3(read, < 0, "IO read error", ssize_t, int, void *, size_t)
SAFE_CALL3(write, < 0, "IO read error", ssize_t, int, const void *, size_t)
SAFE_CALL3(iobuf_read, < 0, "iobuf read error", ssize_t, iofile_t, void *,
           size_t)
SAFE_CALL3(iobuf_write, <= 0, "iobuf write error", ssize_t, iofile_t,
           const void *, size_t)
SAFE_CALL3(chown, < 0, "IO chown error", int, const char *, uid_t, gid_t)

char * readlink_malloc_n(const char *filename, ssize_t *n)
{
  assert(filename);

  size_t size = 256;
  char *buffer = NULL;

  while (1) {
    buffer = (char *)xrealloc(buffer, size);
    ssize_t nchars = readlink(filename, buffer, size - 1);

    if(nchars < 0) {
      free (buffer);
      *n = 0;
      return NULL;
    }
    else if(nchars < size) {
      *n = nchars;
      buffer[nchars] = '\0';
      return buffer;
    }

    size *= 2;
  }
}

size_t n_strncpy(char *dest, const char *src, size_t n)
{
  size_t i;

  for(i = 0 ; i < n && src[i] != '\0' ; i++)
    dest[i] = src[i];
  dest[i] = '\0';

  return i;
}

ssize_t xxiobuf_read(iofile_t file, void *buf, size_t count)
{
  size_t index = 0;

  /* read until we have the desired size */
  while(count) {
    ssize_t n = iobuf_read(file, buf + index, count);

    if(n <= 0)
      err(EXIT_FAILURE, "IO read error or inconsistent archive");

    index += n;
    count -= n;
  }

  return index;
}

/* exact comparison between two strings */
bool strtest(const char *a, const char *b)
{
  /* We save much time by using the builtin version of strcmp */
  return strcmp(a, b) == 0;
}

char * strndup(const char *s, size_t n)
{
  char *r = xmalloc(n);

  return strncpy(r, s, n);
}

/* skip 'size' bytes in a file
   return  0 on success,
          -1 on error    */
int iobuf_skip(iofile_t file, off_t size)
{
  /* first we try the usual way with lseek */
#ifdef __FreeBSD__
  if(iobuf_lseek(file, size, SEEK_CUR) >= 0)
#else
  if(iobuf_lseek64(file, size, SEEK_CUR) >= 0)
#endif /* __FreeBSD__ */
    return 0;

  /* seek failed but it may be because
     fd is associated with a pipe, socket or FIFO
     in such a case we have to make dummy reads
     until we read enough ignored byte */
  if(errno == ESPIPE) {
    while(size) {
      /* although this is a dummy read
         we afford a full sized io buffer
         to avoid switching context uselessly */
      char dummy[IO_SZ];
      ssize_t n;

      n = iobuf_read(file, dummy, MIN(size, IO_SZ));

      if(n <= 0)
        err(EXIT_FAILURE, "IO read error or inconsistent archive");


      size -= n;
    }

    return 0;
  }

  /* anything else is a real error */
  return -1;
}
