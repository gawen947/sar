/* File: sar.c
   Time-stamp: <2011-07-13 23:57:53 gawen>

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
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <utime.h>
#include <time.h>
#include <err.h>

#include "translation.h"
#include "common.h"
#include "crc32.h"
#include "sar.h"

static void crc_write(struct sar_file *out, const void *buf, size_t count);
static void free_hardlinks(struct sar_file *out);
static int add_node(struct sar_file *out, mode_t *mode, const char *name);
static void rec_add(struct sar_file *out, const char *node);
static void write_regular(struct sar_file *out, const struct stat *buf);
static void write_link(struct sar_file *out, const struct stat *buf);
static void write_dev(struct sar_file *out, const struct stat *buf);
static const char * watch_inode(struct sar_file *out, ino_t inode, dev_t device,
                                nlink_t links);


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

#ifndef DISABLE_TIME_WIDTH_CHECK
  /* avoid 1901/2038 bug */
  time_t now = time(NULL);
  if(now > INT32_MAX || now < INT32_MIN)
    out->flags |= A_I64TIME;
#endif /* TIME_WIDTH_CHECK */

  if(!path)
    out->fd = open(path, O_CREAT | O_RDWR | O_TRUNC,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  else
    out->fd = STDOUT_FILENO;

  /* write magik number and flags */
  xwrite(out->fd, &magik, sizeof(magik));
  xwrite(out->fd, &out->flags, sizeof(out->flags));

  /* for debugging purpose */
  UNPTR(out->wp);
  UNPTR(out->hl_tbl);

  return out;
}

void sar_close(struct sar_file *file)
{
  assert(file);
  assert(file->fd);
  assert(!file->wp);

  close(file->fd);
}

void sar_add(struct sar_file *out, const char *path)
{
  assert(out);
  assert(out->fd);
  assert(!out->wp);
  assert(path);

  char *s;

  /* create hard link table */
  out->hl_tbl    = xmalloc(HL_TBL_SZ * sizeof(struct sar_hardlink));
  out->hl_tbl_sz = HL_TBL_SZ;

  /* now we may create the working path
     this is the one we will use in the future */
  out->wp     = xmalloc(WP_MAX);
  out->wp_idx = n_strncpy(out->wp, path, WP_MAX);

  if(out->wp_idx >= WP_MAX)
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
      rec_add(out, node);

      goto CLEAN;
    }

    add_node(out, NULL, node);

    *s = '/';

    out->wp = ++s;

    goto NEXT_NODE;
  }
  else if(*s == '\0') {
    const char *node = strdup(out->wp);

    rec_add(out, node);

    goto CLEAN;
  }

CLEAN:
  free_hardlinks(out);
  free(out->hl_tbl);
  free(out->wp);

  UNPTR(out->hl_tbl);
  UNPTR(out->wp);
}

static void crc_write(struct sar_file *out, const void *buf, size_t count)
{
  if(A_HAS_CRC(out))
    out->crc = crc32(buf, out->crc, count);
  xwrite(out->fd, buf, count);
}

static void free_hardlinks(struct sar_file *out)
{
  size_t i;

  for(i = 0 ; i <= out->hl_tbl_sz ; i++)
    if(out->hl_tbl[i].path)
      free(out->hl_tbl[i].path);
}

static const char * watch_inode(struct sar_file *out, ino_t inode, dev_t device,
                                nlink_t links)
{
  assert(out);
  assert(out->hl_tbl);

  size_t i        = out->hl_tbl_sz;
  size_t null_idx = -1;

  /* search for hardlink */
  while(i--) {
    if(!out->hl_tbl[i].path)
      null_idx = i;
    else if(out->hl_tbl[i].inode == inode && out->hl_tbl[i].device == device) {
      const char *path = strdup(out->hl_tbl[i].path);

      out->hl_tbl[i].links--;

      if(out->hl_tbl[i].links <= 0) {
        free(out->hl_tbl[i].path);
        out->hl_tbl[i].path = NULL;
      }

      return path;
    }
  }

  /* search for a empty index  */
  if(null_idx < 0) {
    out->hl_tbl_sz += HL_TBL_SZ;
    xrealloc(out->hl_tbl, out->hl_tbl_sz * sizeof(struct sar_hardlink));
    null_idx = out->hl_tbl_sz + 1;
  }

  out->hl_tbl[null_idx].inode  = inode;
  out->hl_tbl[null_idx].device = device;
  out->hl_tbl[null_idx].links  = links;
  out->hl_tbl[null_idx].path   = strdup(out->wp);
}

static void write_regular(struct sar_file *out, const struct stat *buf)
{
  char iobuf[IO_SZ];
  uint64_t size = buf->st_size;
  ssize_t n;
  int fd = open(out->wp, O_RDONLY);

  if(fd < 0)
    err(EXIT_FAILURE, "cannot open \"%s\"", out->wp);

  /* size of the file first */
  crc_write(out, &size, sizeof(size));

  while(n = xread(fd, iobuf, IO_SZ))
    crc_write(out, iobuf, n);

  close(fd);
}

static void write_link(struct sar_file *out, const struct stat *buf)
{
  ssize_t n;
  uint32_t size;
  const char *ln = xreadlink_malloc_n(out->wp, &n);

  if(!ln)
    err(EXIT_FAILURE, "cannot read \"%s\"", out->wp);

  size = n;

  crc_write(out, &size, sizeof(size));
  crc_write(out, ln, n);
}

static void write_dev(struct sar_file *out, const struct stat *buf)
{
  uint64_t dev = buf->st_rdev;

  crc_write(out, &dev, sizeof(dev));
}

static int add_node(struct sar_file *out, mode_t *rmode, const char *name)
{
  assert(out);
  assert(out->wp);
  assert(name);

  struct stat buf;
  size_t size;
  uint16_t mode;

#ifndef DISABLE_PERMISSION_CHECK
  int fd = open(out->wp, O_RDONLY);
  if(fd < 0) {
    warn("could not open \"%s\"", out->wp);
    return -1;
  }
  close(fd);
#endif /* PERMISSION_CHECK */

  /* setup crc and we don't care if we will compute it or not */
  out->crc = 0;

  /* stat the file */
  if(stat(out->wp, &buf) < 0) {
    warn("could not stat \"%s\"", out->wp);
    return -1;
  }

  /* watch for hard link */
  if(buf.st_nlink >= 2) {
    const char *link = watch_inode(out, buf.st_ino, buf.st_dev, buf.st_nlink);

    if(link) {
      mode = M_IHARD;

      crc_write(out, &mode, sizeof(mode));
      crc_write(out, link, strlen(link));

      goto CRC;
    }
  }

  /* store file attributes and extended information */
  mode = mode2uint16(buf.st_mode);
  crc_write(out, &mode, sizeof(mode));

  if(A_HAS_32ID(out)) {
    uint32_t uid = buf.st_uid;
    uint32_t gid = buf.st_gid;

    crc_write(out, &uid, sizeof(uid));
    crc_write(out, &gid, sizeof(gid));
  }
  else {
    uint16_t uid = buf.st_uid;
    uint16_t gid = buf.st_gid;

#ifndef DISABLE_ID_WIDTH_CHECK
    /* check for large uid/gid */
    if(buf.st_uid > UINT16_MAX) {
      uid = 0;
      warnx("uid too large for \"%s\", 0 was used instead", out->wp);
    }
    if(buf.st_gid > UINT16_MAX) {
      gid = 0;
      warnx("gid too large for \"%s\", 0 was used instead", out->wp);
    }
#endif /* ID_WIDTH_CHECK */

    crc_write(out, &uid, sizeof(uid));
    crc_write(out, &gid, sizeof(gid));
  }

  if(A_HAS_64TIME(out)) {
    int64_t atime = buf.st_atime;
    int64_t mtime = buf.st_mtime;

    crc_write(out, &atime, sizeof(atime));
    crc_write(out, &mtime, sizeof(mtime));
  }
  else {
    int32_t atime = buf.st_atime;
    int32_t mtime = buf.st_mtime;

    crc_write(out, &atime, sizeof(atime));
    crc_write(out, &mtime, sizeof(mtime));
  }

  size = strlen(name);
#ifndef DISABLE_NAME_WIDTH_CHECK
  /* check for large node name */
  if(size > NAME_MAX) {
    char *short_name = strndup(name, NAME_MAX);
    size = NAME_MAX;

    short_name[NAME_MAX - 1] = '~';
    short_name[NAME_MAX]     = '\0';

    crc_write(out, short_name, size);

    warnx("name too long for \"%s\" reduced to \"%s\"", out->wp, short_name);

    free(short_name);
  }
  else
    crc_write(out, name, size);
#else
  crc_write(out, name, size);
#endif /* NAME_WIDTH_CHECK */

  switch(buf.st_mode & S_IFMT) {
  case(S_IFREG):
    write_regular(out, &buf);
    break;
  case(S_IFLNK):
    write_link(out, &buf);
    break;
  case(S_IFCHR):
  case(S_IFBLK):
    write_dev(out, &buf);
    break;
  default:
    break;
  }

CRC:
  if(A_HAS_CRC(out))
    xwrite(out->fd, &out->crc, sizeof(out->crc));

  if(rmode)
    *rmode = buf.st_mode;

  return 0;
}

static void rec_add(struct sar_file *out, const char *node)
{
  assert(out);
  assert(out->wp);

  mode_t mode;
  uint16_t control = M_ICTRL | M_C_CHILD;

  if(add_node(out, &mode, node) < 0)
    return;

  if(S_ISDIR(mode)) {
    struct dirent *e;
    size_t idx = out->wp_idx;
    DIR *dp = opendir(out->wp);

    if(!dp) {
      warn("cannot open \"%s\"", out->wp);
      return;
    }

    /* check working path size */
    if(out->wp_sz - out->wp_idx < 264) {
      out->wp_sz += 4096;
      out->wp     = xrealloc(out->wp, out->wp_sz);
    }

    while(e = readdir(dp)) {
      size_t i;
      const char *s;

      /* skip special entity name */
      if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
        continue;

      /* append child node name */
      out->wp[out->wp_idx++] = '/';

      for(s = e->d_name ; *s != '\0' ; s++, out->wp_idx++)
        out->wp[out->wp_idx] = *s;
      out->wp[out->wp_idx] = '\0';

      /* recurse into it */
      rec_add(out, e->d_name);

      /* restore working path */
      out->wp_idx  = idx;
      out->wp[idx] = '\0';
    }

    closedir(dp);

    /* append control information */
    xwrite(out->fd, &control, sizeof(control));
  }
}
