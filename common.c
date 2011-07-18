/* File: common.c
   Time-stamp: <2011-07-18 18:16:36 gawen>

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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <utime.h>
#include <err.h>

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

SAFE_CALL2(realloc, == NULL, "out of memory", void *, void *, size_t)
SAFE_CALL2(stat, < 0, "IO stat error", int, const char *, struct stat *)
SAFE_CALL2(dup2, < 0, "cannot duplicate file descriptors", int, int, int)
SAFE_CALL2(readlink_malloc_n, == NULL, "IO readlink error", char *,
           const char *, ssize_t *)
SAFE_CALL2(utime, < 0, "IO chattr error", int, const char *,
           const struct utimbuf *)

SAFE_CALL3(read, < 0, "IO read error", ssize_t, int, void *, size_t)
SAFE_CALL3(write, <= 0, "IO write error", ssize_t, int, const void *, size_t)
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

ssize_t xxread(int fd, void *buf, size_t count)
{
  size_t index = 0;

  /* read until we have the desired size */
  while(count) {
    ssize_t n = read(fd, buf + index, count);

    if(n <= 0)
      err(EXIT_FAILURE, "IO read error or inconsistent archive");

    index += n;
    count -= n;
  }

  /* count less than 0 means we've read too much
     and it should not happend */
  assert(count == 0);

  return index;
}

char * strndup(const char *s, size_t n)
{
  char *r = xmalloc(n);

  return strncpy(r, s, n);
}
