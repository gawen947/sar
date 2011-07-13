/* File: sar.c
   Time-stamp: <2011-07-13 16:19:47 gawen>

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
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <utime.h>
#include <err.h>

#include "common.h"
#include "sar.h"

/* create a new archive from scratch and doesn't
   bother if it was already created we trunc it */
struct sar_file * sar_creat(const char *path,
                            bool use_32id,
                            bool use_64time,
                            bool use_crc)
{
  uint32_t magik = MAGIK;

  struct sar_file *out = xmalloc(sizeof(struct sar_file));

  out->version = MAGIK_VERSION;

  if(use_32id)
    out->flags |= A_I32ID;
  if(use_64time)
    out->flags |= A_I64TIME;
  if(use_crc)
    out->flags |= A_ICRC;

  if(!path)
    out->fd = open(path, O_CREAT | O_RDWR | O_TRUNC,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  else
    out->fd = STDIO_FILENO;

  /* write magik number and flags */
  xwrite(out->fd, &magik, sizeof(magik));
  xwrite(out->fd, &out->flags, sizeof(out->flags));

  /* for debugging purpose */
  UNPTR(out->wp);
}

void sar_add(struct sar_file *out, const char *path)
{
  assert(out);
  assert(out->fd);
  assert(!out->wp);
  assert(path);

  char *s;

  /* now we may create the working path
     this is the one we will use in the future */
  out->wp     = xmalloc(WP_SZ);
  out->wp_idx = n_strncpy(out->wp, path, WP_SZ);

  if(out->wp_idx >= WP_SZ)
    errx(EXIT_FAILURE, "path too long");

  /* avoid root slash */
  if(*path == '/')
    path++;

NEXT_NODE:
  s = out->wp;

  if(*s == '/') {
    const char *node;

    *s = '\0';

    node = strdup(out->wp);

    if(*(s + 1) == '\0') {
      sar_rec_add(out, node);
      return;
    }

    sar_add_node(out, node);

    *s = '/';

    out->wp = ++s;

    goto NEXT_NODE;
  }
  else if(*s == '\0') {
    const char *node = strdup(out->wp);

    sar_rec_add(out, node);
    return;
  }

  /* we never go here */
  assert(0);
}

static void sar_add_node(struct sar_file *out, const char *name)
{
  assert(out);
  assert(out->wp);
  assert(name);

  struct stat buf;

  /* stat the file */
  if(stat(out->wp, &buf) < 0) {
    warn("could not stat \"%s\"", out->wp);
    return;
  }

  /* convert buf to node */
}

