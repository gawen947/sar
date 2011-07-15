/* File: sar.c
   Time-stamp: <2011-07-15 17:53:46 gawen>

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
#include <pwd.h>
#include <grp.h>

#include "translation.h"
#include "common.h"
#include "crc32.h"
#include "sar.h"

static void crc_write(struct sar_file *out, const void *buf, size_t count);
static void xcrc_read(struct sar_file *out, void *buf, size_t count);
static void free_hardlinks(struct sar_file *out);
static int add_node(struct sar_file *out, mode_t *mode, const char *name);
static void rec_add(struct sar_file *out, const char *node);
static int rec_extract(struct sar_file *out, size_t idx);
static void write_regular(struct sar_file *out, const struct stat *buf);
static void write_link(struct sar_file *out, const struct stat *buf);
static void write_dev(struct sar_file *out, const struct stat *buf);
static void write_control(struct sar_file *out, uint16_t id);
static void read_regular(struct sar_file *out, mode_t mode);
static void read_dir(struct sar_file *out, mode_t mode);
static void read_link(struct sar_file *out, mode_t mode);
static void read_fifo(struct sar_file *out, mode_t mode);
static void read_device(struct sar_file *out, mode_t mode);
static void read_hardlink(struct sar_file *out, mode_t mode);
static void clean_hardlinks(struct sar_file *out);
static char * watch_inode(struct sar_file *out, ino_t inode, dev_t device,
                          nlink_t links);
static void show_file(const struct sar_file *out, const char *path,
                      const char *link, mode_t mode, uint16_t sar_mode,
                      uid_t uid, gid_t gid, off_t size, time_t atime,
                      time_t mtime, uint32_t crc, bool display_crc);

/* create a new archive from scratch and doesn't
   bother if it was already created we trunc it */
struct sar_file * sar_creat(const char *path,
                            bool use_32id,
                            bool use_64time,
                            bool use_crc,
                            bool use_mtime,
                            unsigned int verbose)
{
  uint32_t magik = MAGIK;

  struct sar_file *out = xmalloc(sizeof(struct sar_file));

  out->verbose = verbose;
  out->version = MAGIK_VERSION;

  if(use_32id)
    out->flags |= A_I32ID;
  if(use_64time)
    out->flags |= A_I64TIME;
  if(use_crc)
    out->flags |= A_ICRC;
  if(use_mtime)
    out->flags |= A_INTIME;

#ifndef DISABLE_TIME_WIDTH_CHECK
  /* avoid 1901/2038 bug */
  time_t now = time(NULL);
  if(now > INT32_MAX || now < INT32_MIN)
    out->flags |= A_I64TIME;
#endif /* TIME_WIDTH_CHECK */

  if(!path)
    out->fd = STDOUT_FILENO;
  else {
    out->fd = open(path, O_CREAT | O_RDWR | O_TRUNC,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if(out->fd < 0)
      err(EXIT_FAILURE, "could not open file \"%s\"", path);
  }

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

  char *s, *npath;
  size_t nb_nodes = 0;

  /* create hard link table */
  out->hl_tbl    = xmalloc(HL_TBL_SZ * sizeof(struct sar_hardlink));
  out->hl_tbl_sz = HL_TBL_SZ;
  clean_hardlinks(out);

  /* now we may create the working path
     this is the one we will use in the future */
  out->wp     = xmalloc(WP_MAX);
  out->wp_idx = n_strncpy(out->wp, path, WP_MAX);
  npath       = out->wp;

  if(out->wp_idx >= WP_MAX)
    errx(EXIT_FAILURE, "path too long");

  /* avoid root slash */
  if(*npath == '/')
    npath++;

NEXT_NODE:
  nb_nodes++;
  s = npath;

  for(;; s++) {
    if(*s == '/') {
      const char *node;

      *s = '\0';

      node = strdup(npath);

      if(*(s + 1) == '\0')  {
        rec_add(out, node);

        goto CLEAN;
      }

      add_node(out, NULL, node);

      *s = '/';

      npath = ++s;

      goto NEXT_NODE;
    }
    else if(*s == '\0') {
      const char *node = strdup(npath);

      rec_add(out, node);

      goto CLEAN;
    }
  }

CLEAN:
  while(nb_nodes--)
    write_control(out, M_C_CHILD);

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

static void xcrc_read(struct sar_file *out, void *buf, size_t count)
{
  xxread(out->fd, buf, count);

  if(A_HAS_CRC(out))
    out->crc = crc32(buf, out->crc, count);
}

static void clean_hardlinks(struct sar_file *out)
{
  size_t i;

  for(i = 0 ; i < out->hl_tbl_sz ; i++)
    out->hl_tbl[i].path = NULL;
}

static void free_hardlinks(struct sar_file *out)
{
  size_t i;

  for(i = 0 ; i < out->hl_tbl_sz ; i++)
    if(out->hl_tbl[i].path)
      free(out->hl_tbl[i].path);
}

static char * watch_inode(struct sar_file *out, ino_t inode, dev_t device,
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
      char *path = strdup(out->hl_tbl[i].path);

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

  return NULL;
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

  while((n = xread(fd, iobuf, IO_SZ)))
    crc_write(out, iobuf, n);

  close(fd);
}

static void write_link(struct sar_file *out, const struct stat *buf)
{
  ssize_t n;
  uint8_t size;
  char *ln = xreadlink_malloc_n(out->wp, &n);

  if(!ln)
    err(EXIT_FAILURE, "cannot read \"%s\"", out->wp);

  out->link = strdup(ln);

  size = n;

  crc_write(out, &size, sizeof(size));
  crc_write(out, ln, n);

  free(ln);
}

static void write_dev(struct sar_file *out, const struct stat *buf)
{
  uint64_t dev = buf->st_rdev;

  crc_write(out, &dev, sizeof(dev));
}

static void write_control(struct sar_file *out, uint16_t id)
{
  uint16_t control = M_ICTRL | id;

  xwrite(out->fd, &control, sizeof(control));
}

static void show_file(const struct sar_file *out, const char *path,
                      const char *link, mode_t mode, uint16_t sar_mode,
                      uid_t uid, gid_t gid, off_t size, time_t atime,
                      time_t mtime, uint32_t crc, bool display_crc)
{
  if(out->verbose >= 2) {
    char date[DATE_MAX];
    struct passwd *p_uid;
    struct group  *g_gid;
    char s_mode[] = "?---------";

    if(M_ISCTRL(sar_mode)) {
      /* special file type */
      switch(sar_mode) {
      case(M_ICTRL | M_C_CHILD):
        s_mode[0] = 'C';
        printf("%s\n", s_mode);
        return;
      case(M_ICTRL | M_C_IGNORE):
        s_mode[0] = 'I';
        if(display_crc && out->verbose >= 3)
          printf("%s\t%s {0x%x}\n", s_mode, path, crc);
        else
          printf("%s\t%s\n", s_mode, path);
        return;
      }
    }
    else {

      /* regular file type */
      switch(mode & S_IFMT) {
      case(S_IFSOCK):
        s_mode[0] = 's';
        break;
      case(S_IFLNK):
        s_mode[0] = 'l';
        break;
      case(S_IFREG):
        s_mode[0] = '-';
        break;
      case(S_IFDIR):
        s_mode[0] = 'd';
        break;
      case(S_IFBLK):
        s_mode[0] = 'b';
        break;
      case(S_IFCHR):
        s_mode[0] = 'c';
        break;
      case(S_IFIFO):
        s_mode[0] = 'p';
        break;
      }

      /* basic permissions */
      if(mode & S_IRUSR)
        s_mode[1] = 'r';
      if(mode & S_IWUSR)
        s_mode[2] = 'w';
      if(mode & S_IXUSR)
        s_mode[3] = 'x';
      if(mode & S_IRGRP)
        s_mode[4] = 'r';
      if(mode & S_IWGRP)
        s_mode[5] = 'w';
      if(mode & S_IXGRP)
        s_mode[6] = 'x';
      if(mode & S_IROTH)
        s_mode[7] = 'r';
      if(mode & S_IWOTH)
        s_mode[8] = 'w';
      if(mode & S_IXOTH)
        s_mode[9] = 'x';

      /* extended permissions */
      if(mode & S_ISUID) {
        if(s_mode[3] == 'x')
          s_mode[3] = 's';
        else
          s_mode[3] = 'S';
      }
      if(mode & S_ISGID) {
        if(s_mode[6] == 'x')
          s_mode[6] = 's';
        else
          s_mode[6] = 'S';
      }
      if(mode & S_ISVTX) {
        if(s_mode[9] == 'x')
          s_mode[9] = 't';
        else
          s_mode[9] = 'T';
      }
    }

    if((sar_mode & S_IFMT) == M_IHARD) {
      s_mode[0] = 'h';

      if(display_crc && out->verbose >= 3)
        printf("%s\t%s -> %s {0x%x}\n", s_mode, path, link, crc);
      else
        printf("%s\t%s -> %s\n", s_mode, path, link);
      return;
    }

    printf("%s\t", s_mode);

    /* user name / group name */
    p_uid = getpwuid(uid);
    g_gid = getgrgid(gid);

    if(p_uid)
      printf("%s/", p_uid->pw_name);
    else
      printf("%d/", uid);

    if(g_gid)
      printf("%s\t", g_gid->gr_name);
    else
      printf("%d\t", gid);

    /* size */
    printf("%ld\t", size);

    /* times */
    if(out->verbose >= 4) {
      strftime(date, DATE_MAX, DATE_FORMAT, localtime(&atime));
      printf("%s\t", date);
    }
    strftime(date, DATE_MAX, DATE_FORMAT, localtime(&atime));
    printf("%s\t", date);


    /* path */
    if(link)
      printf("%s -> %s", path, link);
    else
      printf("%s", path);

    /* crc */
    if(display_crc && out->verbose >= 3)
      printf(" {0x%x}\n", out->crc);
    else
      printf("\n");
  }
  else if(out->verbose >= 1)
    printf("%s\n", path);
}

static int add_node(struct sar_file *out, mode_t *rmode, const char *name)
{
  assert(out);
  assert(out->wp);
  assert(name);

  struct stat buf;
  size_t size;
  uint8_t t_size;
  uint16_t mode;

#ifndef DISABLE_PERMISSION_CHECK
  if(access(out->wp, R_OK) < 0) {
    warn("cannot open \"%s\"", out->wp);
    return -1;
  }
#endif /* PERMISSION_CHECK */

  /* setup crc and fallback variables we don't
     care if we will compute it or not */
  out->crc  = 0;
  out->link = NULL;

  /* stat the file */
  if(stat(out->wp, &buf) < 0) {
    warn("could not stat \"%s\"", out->wp);
    return -1;
  }

  /* watch for hard link */
  if(buf.st_nlink >= 2 && !S_ISDIR(buf.st_mode)) {
    char *link = watch_inode(out, buf.st_ino, buf.st_dev, buf.st_nlink);

    if(link) {
      uint16_t size = strlen(link);

      mode = M_IHARD;

      crc_write(out, &mode, sizeof(mode));
      crc_write(out, &size, sizeof(size));
      crc_write(out, link, size);

      out->link = link;

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
    t_size = size = NAME_MAX;

    short_name[NAME_MAX - 1] = '~';
    short_name[NAME_MAX]     = '\0';

    crc_write(out, &t_size, sizeof(t_size));
    crc_write(out, short_name, size);

    warnx("name too long for \"%s\" reduced to \"%s\"", out->wp, short_name);

    free(short_name);
  }
  else {
    t_size = size;
    crc_write(out, &t_size, sizeof(t_size));
    crc_write(out, name, size);
  }
#else
  t_size = size;
  crc_write(out, &t_size, sizeof(t_size));
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

  show_file(out, out->wp, out->link, buf.st_mode, mode, buf.st_uid, buf.st_gid,
            buf.st_size, buf.st_atime, buf.st_mtime, out->crc,
            A_HAS_CRC(out));
  free(out->link);

  return 0;
}

static void rec_add(struct sar_file *out, const char *node)
{
  assert(out);
  assert(out->wp);

  mode_t mode;

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

    while((e = readdir(dp))) {
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
    write_control(out, M_C_CHILD);
  }
}

/* open an archive for reading */
struct sar_file * sar_read(const char *path, unsigned int verbose)
{
  uint32_t magik;

  struct sar_file *out = xmalloc(sizeof(struct sar_file));

  out->verbose = verbose;

  if(!path)
    out->fd = STDIN_FILENO;
  else {
    out->fd = open(path, O_RDONLY);

    if(out->fd < 0)
      err(EXIT_FAILURE, "could not open file \"%s\"", path);
  }

  /* check magik number */
  xxread(out->fd, &magik, sizeof(magik));

  if(magik != MAGIK)
    errx(EXIT_FAILURE, "incompatible magik number");

  out->version = magik & MAGIK_FMT_VER;

  /* extract flags */
  xxread(out->fd, &out->flags, sizeof(out->flags));

  /* for debugging purpose */
  UNPTR(out->wp);
  UNPTR(out->hl_tbl);

  return out;
}

static void read_regular(struct sar_file *out, mode_t mode)
{
  char iobuf[IO_SIZE];
  uint64_t size;

  if(out->list_only) {
    xxread(out->fd, &size, sizeof(size));
    out->size = size;

    if(A_HAS_CRC(out))
      size += sizeof(out->crc);

    lseek(out->fd, size, SEEK_CUR);
    return;
  }

  /* open output file */
  int fd = open(out->wp, O_CREAT | O_RDWR | O_TRUNC, mode);
  if(fd < 0)
    err(EXIT_FAILURE, "could not open output file \"%s\"", out->wp);

  /* read file size */
  xcrc_read(out, &size, sizeof(size));
  out->size = size;

  /* endianess conversion for size should be done here */

  /* read file */
  while(size > 0) {
    size_t n = MIN(size, IO_SIZE);

    xcrc_read(out, iobuf, n);

    /* copy buffer */
    xwrite(fd, iobuf, n);

    size -= n;
  }

  if(size < 0)
    errx(EXIT_FAILURE, "inconsistent archive");

  close(fd);
}

static void read_dir(struct sar_file *out, mode_t mode)
{
  if(out->list_only)  {
    if(A_HAS_CRC(out))
      lseek(out->fd, sizeof(out->crc), SEEK_CUR);
    return;
  }

  if(mkdir(out->wp, mode) < 0)
    warn("cannot create directory \"%s\"", out->wp);
}

static void read_link(struct sar_file *out, mode_t mode)
{
  char path[WP_MAX];
  uint16_t size;

  if(out->list_only) {
    xxread(out->fd, &size, sizeof(size));
    out->size = size;

    if(A_HAS_CRC(out))
      size += sizeof(out->crc);

    lseek(out->fd, size, SEEK_CUR);
    return;
  }

  /* read link length */
  xcrc_read(out, &size, sizeof(size));
  out->size = size;

  /* endianess conversion should be done here for size */

  xcrc_read(out, path, size);

  out->link = path;
  if(symlink(out->wp, path) < 0)
    warn("cannot create symlink \"%s\" to \"%s\"", out->wp, path);
}

static void read_fifo(struct sar_file *out, mode_t mode)
{
  if(out->list_only)  {
    if(A_HAS_CRC(out))
      lseek(out->fd, sizeof(out->crc), SEEK_CUR);
    return;
  }

  if(mkfifo(out->wp, mode) < 0)
    warn("cannot create fifo \"%s\"", out->wp);
}

static void read_device(struct sar_file *out, mode_t mode)
{
  uint64_t dev;

  out->size = sizeof(dev);

  if(out->list_only) {
    if(A_HAS_CRC(out))
      lseek(out->fd, sizeof(dev) + sizeof(out->crc), SEEK_CUR);
    else
      lseek(out->fd, sizeof(dev), SEEK_CUR);
    return;
  }

  xcrc_read(out, &dev, sizeof(dev));

  /* endianess conversion should be done here for dev */

  if(mknod(out->wp, mode, dev) < 0)
    err(EXIT_FAILURE, "cannot create device \"%s\"", out->wp);
}

static void read_hardlink(struct sar_file *out, mode_t mode)
{
  char path[WP_MAX];
  uint16_t size;

  if(out->list_only) {
    xxread(out->fd, &size, sizeof(size));
    out->size = size;

    if(A_HAS_CRC(out))
      size += sizeof(out->crc);

    lseek(out->fd, size, SEEK_CUR);
    return;
  }

  /* read link length */
  xcrc_read(out, &size, sizeof(size));
  out->size = size;

  /* endianess conversion should be done here for size */

  xcrc_read(out, path, size);

  out->link = path;
  if(link(path, out->wp) < 0)
    warnx("cannot create hardlink \"%s\" to \"%s\"", out->wp, path);
}

static int rec_extract(struct sar_file *out, size_t idx)
{
  assert(out);
  assert(out->fd);
  assert(out->wp);

  char name[NODE_MAX];
  struct utimbuf times;
  time_t atime = 0;
  time_t mtime = 0;
  uid_t uid = 0;
  gid_t gid = 0;
  mode_t real_mode;
  uint16_t mode;
  uint8_t size, i;

  /* setup crc and fallback variables we don't
     care if we won't compute it or not */
  out->crc = 0;
  out->link = 0;
  out->size = 0;

  xcrc_read(out, &mode, sizeof(mode));

  switch(mode & M_IFMT) {
  case(M_IHARD):
    goto EXTRACT_NAME;
  case(M_ICTRL):
    switch(mode) {
    case(M_ICTRL | M_C_CHILD):
      out->wp[idx] = '\0';
      return 1;
    case(M_ICTRL | M_C_IGNORE):
      warnx("ignored \"%s\", not extracted", out->wp);
      break;
    }
    break;
  }

  /* read file attribues and extended information */
  if(A_HAS_32ID(out)) {
    uint32_t t_uid;
    uint32_t t_gid;

    xcrc_read(out, &t_uid, sizeof(t_uid));
    xcrc_read(out, &t_gid, sizeof(t_gid));

    /* endianess conversion should be done here */

    uid = t_uid;
    gid = t_gid;
  }
  else {
    uint16_t t_uid;
    uint16_t t_gid;

    xcrc_read(out, &t_uid, sizeof(t_uid));
    xcrc_read(out, &t_gid, sizeof(t_gid));

    /* endianess conversion should be done here */

    uid = t_uid;
    gid = t_gid;
  }

  if(A_HAS_64TIME(out)) {
    int64_t t_atime;
    int64_t t_mtime;

    xcrc_read(out, &t_atime, sizeof(t_atime));
    xcrc_read(out, &t_mtime, sizeof(t_mtime));

    /* endianess conversion should be done here */

    atime = t_atime;
    mtime = t_mtime;
  }
  else {
    int32_t t_atime;
    int32_t t_mtime;

    xcrc_read(out, &t_atime, sizeof(t_atime));
    xcrc_read(out, &t_mtime, sizeof(t_mtime));

    /* endianess conversion should be done here */

    atime = t_atime;
    mtime = t_mtime;
  }

EXTRACT_NAME:
  /* extract name */
  xcrc_read(out, &size, sizeof(size));
  xcrc_read(out, name, size);

#ifndef DISABLE_WP_WIDTH_CHECK
  if(idx + size >= WP_MAX)
    errx(EXIT_FAILURE, "maximum size exceeded for working path");
#endif /* WP_WIDTH_CHECK */

  /* copy name to working path */
  for(i = 0 ; i < size ; i++)
    out->wp[idx + i] = name[i];
  out->wp[idx + size] = '\0';

  /* endianess conversion should be done here */
  real_mode = uint162mode(mode);

  switch(mode & M_IFMT) {
  case(M_IREG):
    read_regular(out, real_mode);
    break;
  case(M_IDIR):
    read_dir(out, real_mode);
    break;
  case(M_ILNK):
    read_link(out, real_mode);
    break;
  case(M_IHARD):
    read_hardlink(out, real_mode);
    break;
  case(M_IFIFO):
    read_fifo(out, real_mode);
    break;
  case(M_IBLK):
  case(M_ICHR):
    read_device(out, real_mode);
    break;
  }

  /* change attributes */
  times.actime  = atime;
  times.modtime = mtime;

  if(!out->list_only) {
    chown(out->wp, uid, gid);
    utime(out->wp, &times);
  }

  /* compute crc */
  if(A_HAS_CRC(out) && !out->list_only) {
    uint32_t crc;
    xxread(out->fd, &crc, sizeof(crc));

    if(crc != out->crc)
      warnx("corrupted file \"%s\"", out->wp);
  }

  show_file(out, out->wp, out->link, real_mode, mode,
            uid, gid, out->size, atime, mtime, out->crc,
            A_HAS_CRC(out) && !out->list_only);

  /* check for directory and extracts children */
  if((mode & M_IFMT) == M_IDIR) {
    out->wp[idx + size]     = '/';
    out->wp[idx + size + 1] = '\0';
    /* read until we receive a child control stamp */
    while(rec_extract(out, idx + size + 1) != 1);

    out->wp[idx + size] = '\0';
  }

  return 0;
}

void sar_extract(struct sar_file *out)
{
  assert(out);
  assert(out->fd);
  assert(!out->wp);

  /* create the working path
     this is the one we will use in the future */
  out->wp     = xmalloc(WP_MAX);

  /* read until we receive a child control stamp */
  while(rec_extract(out, 0) != 1);

  free(out->wp);
  UNPTR(out->wp);
}

void sar_list(struct sar_file *out)
{
  out->list_only = true;
  sar_extract(out);
}

void sar_info(struct sar_file *out)
{
  printf("SAR file:\n"
         "\tVersion        : %d\n"
         "\tHas CRC        : %s\n"
         "\tHas wide ID    : %s\n"
         "\tHas wide time  : %s\n"
         "\tHas micro time : %s\n",
         out->version,
         S_BOOLEAN(A_HAS_CRC(out)),
         S_BOOLEAN(A_HAS_32ID(out)),
         S_BOOLEAN(A_HAS_64TIME(out)),
         S_BOOLEAN(A_HAS_NTIME(out)));
}
