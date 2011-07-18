/* File: sar.c
   Time-stamp: <2011-07-18 23:33:56 gawen>

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
#include <sys/time.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <endian.h>
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
static void reupdate_time(const struct sar_file *out);
static void rec_add(struct sar_file *out, const char *node);
static enum isclass get_id_size_class(uid_t uid, gid_t gid);
static enum tsclass get_time_size_class(time_t atime, time_t mtime);
static int rec_extract(struct sar_file *out, size_t idx);
static void write_regular(struct sar_file *out);
static void write_link(struct sar_file *out);
static void write_dev(struct sar_file *out);
static void write_control(struct sar_file *out, uint16_t id);
static void write_name(struct sar_file *out, const char *name);
static void read_regular(struct sar_file *out, mode_t mode);
static void read_dir(struct sar_file *out, mode_t mode);
static void read_link(struct sar_file *out, mode_t mode);
static void read_fifo(struct sar_file *out, mode_t mode);
static void read_device(struct sar_file *out, mode_t mode);
static void read_hardlink(struct sar_file *out, mode_t mode);
static void clean_hardlinks(struct sar_file *out);
static char * watch_inode(struct sar_file *out);
static void show_file(const struct sar_file *out, const char *path,
                      const char *link, mode_t mode, uint16_t sar_mode,
                      uid_t uid, gid_t gid, off_t size, time_t atime,
                      time_t mtime, uint32_t crc, bool display_crc);

/* create a new archive from scratch and doesn't
   bother if it was already created we trunc it */
struct sar_file * sar_creat(const char *path,
                            const char *compress,
                            bool use_crc,
                            bool use_ntime,
                            unsigned int verbose)
{
  uint32_t magik = MAGIK;
  uint32_t s_magik;

  struct sar_file *out = xmalloc(sizeof(struct sar_file));

  out->verbose = verbose;
  out->version = MAGIK_VERSION;

  if(use_crc)
    out->flags |= A_ICRC;
  if(use_ntime)
    out->flags |= A_INTIME;

  if(!path)
    out->fd = STDOUT_FILENO;
  else {
    out->fd = open(path, O_CREAT | O_RDWR | O_TRUNC,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if(out->fd < 0)
      err(EXIT_FAILURE, "could not open file \"%s\"", path);
  }

  if(compress) {
    int fd[2];
    pid_t pid;

    xpipe(fd);
    pid = xfork();

    if(!pid) /* child */ {
      close(fd[1]);

      xdup2(fd[0], STDIN_FILENO);
      xdup2(out->fd, STDOUT_FILENO);

      close(fd[0]);
      close(out->fd);

      execlp(compress, compress, NULL);
      err(EXIT_FAILURE, "cannot execute \"%s\"", compress);
    }

    close(fd[0]);
    out->fd = fd[1];
  }

  /* write magik number and flags
     notice we convert magik to little endian first
     flags which is 1 byte wide is not converted though */
  s_magik = htole32(magik);
  xwrite(out->fd, &s_magik, sizeof(s_magik));
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

  int status = 0;

  /* we need to wait for compression child to return */
  wait(&status);

  if(status)
    errx(EXIT_FAILURE, "failed to compress");

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
  size_t i;

  /* create hard link table */
  out->hl_tbl    = xmalloc(HL_TBL_SZ * sizeof(struct sar_hardlink));
  out->hl_tbl_sz = HL_TBL_SZ;
  clean_hardlinks(out);

  /* now we may create the working path
     this is the one we will use in the future */
  out->wp     = xmalloc(WP_MAX);
  out->wp_idx = n_strncpy(out->wp, path, WP_MAX);
  npath       = out->wp;

  /* remove trailing / */
  for(i = out->wp_idx - 1 ; out->wp[i] == '/' ; i--)
    out->wp[i] = '\0';
  out->wp_idx = i + 1;

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

static char * watch_inode(struct sar_file *out)
{
  assert(out);
  assert(out->hl_tbl);

  long i        = out->hl_tbl_sz;
  long null_idx = -1;

  /* search for hardlink */
  while(i--) {
    if(!out->hl_tbl[i].path)
      null_idx = i;
    else if(out->hl_tbl[i].inode  == out->stat.st_ino &&
            out->hl_tbl[i].device == out->stat.st_dev) {
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
    long i = out->hl_tbl_sz;

    /* reallocate */
    out->hl_tbl_sz += HL_TBL_SZ;
    out->hl_tbl = xrealloc(out->hl_tbl,
                           out->hl_tbl_sz * sizeof(struct sar_hardlink));

    /* clean last elements */
    for(; i < out->hl_tbl_sz ; i++)
      out->hl_tbl[i].path = NULL;

    null_idx = i - 1;
  }

  out->hl_tbl[null_idx].inode  = out->stat.st_ino;
  out->hl_tbl[null_idx].device = out->stat.st_dev;
  out->hl_tbl[null_idx].links  = out->stat.st_nlink;
  out->hl_tbl[null_idx].path   = strdup(out->wp);

  return NULL;
}

static enum fsclass get_file_size_class(off_t size)
{
  if(size <= UINT8_MAX)
    return N_FBYTE;
  else if(size <= UINT16_MAX)
    return N_FKILO;
  else if(size <= UINT32_MAX)
    return N_FGIGA;
  else
    return N_FHUGE;
}

static enum isclass get_id_size_class(uid_t uid, gid_t gid)
{
  if(uid == 0 && gid == 0)
    return N_IRR;
  else if(uid == 1000 && gid == 1000)
    return N_IUU;
  else if(uid == gid && uid <= UINT8_MAX)
    return N_ISRB;
  else if(uid == gid && uid >= 1000 && uid <= (1000 + UINT8_MAX))
    return N_ISUB;
  else if(uid == 0 && (gid <= UINT8_MAX))
    return N_IRB;
  else if(uid == 1000 && gid >= 1000 && gid <= (1000 + UINT8_MAX))
    return N_IUB;
  else if(uid == gid && uid <= UINT16_MAX)
    return N_ISKILO;
  else if(uid <= UINT8_MAX && gid <= UINT8_MAX)
    return N_IBBYTE;
  else if(uid >= 1000 && gid >= 1000 &&
          uid <= (1000 + UINT8_MAX) && gid <= (1000 + UINT8_MAX))
    return N_IBUBYTE;
  else if(uid <= UINT8_MAX && gid <= UINT16_MAX)
    return N_IBK;
  else if(gid <= UINT16_MAX && uid <= UINT8_MAX)
    return N_IKB;
  else if(uid == gid)
    return N_ISGIGA;
  else if(uid <= UINT16_MAX && gid <= UINT16_MAX)
    return N_IBKILO;
  else if(uid <= UINT16_MAX)
    return N_IKG;
  else if(gid <= UINT16_MAX)
    return N_IGK;
  else
    return N_IGG;
}

static enum tsclass get_time_size_class(time_t atime, time_t mtime)
{
  if(atime == mtime && (int32_t)atime == atime)
    return N_TS32;
  else if(atime == mtime)
    return N_TS64;
  else if((int32_t)atime == atime && (int32_t)mtime == mtime)
    return N_TB32;
  else
    return N_TB64;
}

static void write_regular(struct sar_file *out)
{
  char iobuf[IO_SZ];
  enum fsclass class;
  ssize_t n;
  int fd;

  /* store size first */
  class = out->nsclass & N_FILE;
  switch(class) {
    uint8_t  size_byte;
    uint16_t size_kilo;
    uint32_t size_giga;
    uint64_t size_huge;

  case(N_FBYTE):
    size_byte = out->stat.st_size;
    crc_write(out, &size_byte, sizeof(size_byte));
    break;
  case(N_FKILO):
    size_kilo = htole16(out->stat.st_size);
    crc_write(out, &size_kilo, sizeof(size_kilo));
    break;
  case(N_FGIGA):
    size_giga = htole32(out->stat.st_size);
    crc_write(out, &size_giga, sizeof(size_giga));
    break;
  case(N_FHUGE):
    size_huge = htole64(out->stat.st_size);
    crc_write(out, &size_huge, sizeof(size_huge));
    break;
  }

  /* store file */
  fd = open(out->wp, O_RDONLY);

  /* if it fails here the archive is screwed out */
  if(fd < 0)
    err(EXIT_FAILURE, "cannot open \"%s\"", out->wp);

  while((n = xread(fd, iobuf, IO_SZ)))
    crc_write(out, iobuf, n);

  close(fd);
}

static void write_link(struct sar_file *out)
{
  ssize_t n;
  enum fsclass class;

  /* read link */
  out->link = xreadlink_malloc_n(out->wp, &n);

  /* if it fails here the archive is screwed out */
  if(!out->link)
    err(EXIT_FAILURE, "cannot read \"%s\"", out->wp);

  /* store size */
  class = out->nsclass & N_FILE;
  switch(class) {
    uint8_t  size_byte;
    uint16_t size_kilo;

  case(N_FBYTE):
    size_byte = n;
    crc_write(out, &size_byte, sizeof(size_byte));
    break;
  case(N_FKILO):
    size_kilo = htole16(n);
    crc_write(out, &size_kilo, sizeof(size_kilo));
    break;
  case(N_FGIGA):
  case(N_FHUGE):
    errx(EXIT_FAILURE, "link size too large for \"%s\"", out->wp);
  }

  crc_write(out, out->link, n);
}

static void write_dev(struct sar_file *out)
{
  uint64_t dev = htole64(out->stat.st_rdev);

  crc_write(out, &dev, sizeof(dev));
}

static void write_control(struct sar_file *out, uint16_t id)
{
  uint16_t control = htole16(M_ICTRL | id);

  xwrite(out->fd, &control, sizeof(control));
}

static void write_name(struct sar_file *out, const char *name)
{
  uint8_t s_size;
  size_t size    = strlen(name);

#ifndef DISABLE_NAME_WIDTH_CHECK
  /* check for large node name */
  if(size > NAME_MAX) {
    char *short_name = strndup(name, NAME_MAX);
    size   = NAME_MAX;
    s_size = NAME_MAX;

    short_name[NAME_MAX - 1] = '~';
    short_name[NAME_MAX]     = '\0';

    crc_write(out, &s_size, sizeof(s_size));
    crc_write(out, short_name, NAME_MAX);

    warnx("name too long for \"%s\" reduced to \"%s\"", out->wp, short_name);

    free(short_name);
  }
  else {
    s_size = size;
    crc_write(out, &s_size, sizeof(s_size));
    crc_write(out, name, s_size);
  }
#else
  s_size = size;
  crc_write(out, &s_size, sizeof(s_size));
  crc_write(out, name, size);
#endif /* NAME_WIDTH_CHECK */
}

static void show_file(const struct sar_file *out,
                      const char *path, const char *link,
                      mode_t mode, uint16_t sar_mode,
                      uid_t uid, gid_t gid,
                      off_t size,
                      time_t atime, time_t mtime,
                      uint32_t crc, bool display_crc)
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

    if((sar_mode & M_IFMT) == M_IHARD) {
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
    printf("% 9ld\t", size);

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

  enum isclass iclass;
  enum tsclass tclass;
  uint16_t mode, s_mode;
  uint8_t s_nsclass;

  /* stat the file first to reupdate access time later */
  if(lstat(out->wp, &out->stat) < 0) {
    warn("could not stat \"%s\"", out->wp);
    return -1;
  }

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

  /* watch for hard link */
  if(out->stat.st_nlink >= 2 && !S_ISDIR(out->stat.st_mode)) {
    char *link = watch_inode(out);

    if(link) {
      uint16_t s_link_sz;
      uint16_t link_sz = strlen(link);

      /* compute mode and convert endianess */
      mode      = (mode2uint16(out->stat.st_mode) & M_IPERM) | M_IHARD;
      s_mode    = htole16(mode);
      s_link_sz = htole16(link_sz);

      crc_write(out, &s_mode, sizeof(s_mode));
      write_name(out, name);
      crc_write(out, &s_link_sz, sizeof(s_link_sz));
      crc_write(out, link, link_sz);

      /* save link for displaying */
      out->link = link;

      goto CRC;
    }
  }

  /* now that we stated the file and we took care of hardlink
     we may compute node file class */
  out->nsclass  = get_file_size_class(out->stat.st_size);
  out->nsclass |= get_id_size_class(out->stat.st_uid, out->stat.st_gid);
  out->nsclass |= get_time_size_class(out->stat.st_atime, out->stat.st_mtime);

  /* store mode first */
  mode   = mode2uint16(out->stat.st_mode);
  s_mode = htole16(mode);
  crc_write(out, &s_mode, sizeof(s_mode));

  /* store node size class */
  s_nsclass = htole16(out->nsclass);
  crc_write(out, &s_nsclass, sizeof(s_nsclass));

  /* store uid/gid */
  iclass = out->nsclass & N_ID;
  switch(iclass) {
    uint8_t  id_byte;
    uint16_t id_kilo;
    uint32_t id_giga;

  case(N_IRR):
  case(N_IUU):
    break;
  case(N_ISRB):
    id_byte = out->stat.st_uid;
    crc_write(out, &id_byte, sizeof(id_byte));
    break;
  case(N_ISUB):
    id_byte = (out->stat.st_uid - 1000);
    crc_write(out, &id_byte, sizeof(id_byte));
    break;
  case(N_IRB):
    id_byte = out->stat.st_gid;
    crc_write(out, &id_byte, sizeof(id_byte));
    break;
  case(N_IUB):
    id_byte = out->stat.st_gid - 1000;
    crc_write(out, &id_byte, sizeof(id_byte));
    break;
  case(N_ISKILO):
    id_kilo = htole16(out->stat.st_uid);
    crc_write(out, &id_kilo, sizeof(id_kilo));
    break;
  case(N_IBBYTE):
    id_byte = out->stat.st_uid;
    crc_write(out, &id_byte, sizeof(id_byte));

    id_byte = out->stat.st_gid;
    crc_write(out, &id_byte, sizeof(id_byte));
    break;
  case(N_IBUBYTE):
    id_byte = out->stat.st_uid - 1000;
    crc_write(out, &id_byte, sizeof(id_byte));

    id_byte = out->stat.st_gid - 1000;
    crc_write(out, &id_byte, sizeof(id_byte));
    break;
  case(N_IBK):
    id_byte = out->stat.st_uid;
    id_kilo = htole16(out->stat.st_gid);

    crc_write(out, &id_byte, sizeof(id_byte));
    crc_write(out, &id_kilo, sizeof(id_kilo));
    break;
  case(N_IKB):
    id_kilo = htole16(out->stat.st_uid);
    id_byte = out->stat.st_gid;

    crc_write(out, &id_kilo, sizeof(id_kilo));
    crc_write(out, &id_byte, sizeof(id_byte));
    break;
  case(N_ISGIGA):
    id_giga = htole32(out->stat.st_uid);
    crc_write(out, &id_giga, sizeof(id_giga));
    break;
  case(N_IBKILO):
    id_kilo = htole16(out->stat.st_uid);
    crc_write(out, &id_kilo, sizeof(id_kilo));

    id_kilo = htole16(out->stat.st_gid);
    crc_write(out, &id_kilo, sizeof(id_kilo));
    break;
  case(N_IKG):
    id_kilo = htole16(out->stat.st_uid);
    id_giga = htole32(out->stat.st_gid);

    crc_write(out, &id_kilo, sizeof(id_kilo));
    crc_write(out, &id_giga, sizeof(id_giga));
    break;
  case(N_IGK):
    id_giga = htole32(out->stat.st_uid);
    id_kilo = htole16(out->stat.st_gid);

    crc_write(out, &id_giga, sizeof(id_giga));
    crc_write(out, &id_kilo, sizeof(id_kilo));
  case(N_IGG):
    id_giga = htole32(out->stat.st_uid);
    crc_write(out, &id_giga, sizeof(id_giga));

    id_giga = htole32(out->stat.st_gid);
    crc_write(out, &id_giga, sizeof(id_giga));
    break;
  }

  /* store atime/mtime */
  tclass = out->nsclass & N_TIME;
  switch(tclass) {
    int32_t time_giga;
    int64_t time_huge;

  case(N_TS32):
    time_giga = htole32(out->stat.st_atime);
    crc_write(out, &time_giga, sizeof(time_giga));
    break;
  case(N_TS64):
    time_huge = htole32(out->stat.st_atime);
    crc_write(out, &time_huge, sizeof(time_huge));
    break;
  case(N_TB32):
    time_giga = htole32(out->stat.st_atime);
    crc_write(out, &time_giga, sizeof(time_giga));

    time_giga = htole32(out->stat.st_mtime);
    crc_write(out, &time_giga, sizeof(time_giga));
    break;
  case(N_TB64):
    time_huge = htole64(out->stat.st_atime);
    crc_write(out, &time_huge, sizeof(time_huge));

    time_huge = htole64(out->stat.st_mtime);
    crc_write(out, &time_huge, sizeof(time_huge));
    break;
  }

  if(A_HAS_NTIME(out)) {
    uint32_t atime_ns = htole32(out->stat.st_atim.tv_nsec);
    uint32_t mtime_ns = htole32(out->stat.st_mtim.tv_nsec);

    crc_write(out, &atime_ns, sizeof(atime_ns));
    crc_write(out, &mtime_ns, sizeof(mtime_ns));
  }

  write_name(out, name);

  switch(out->stat.st_mode & S_IFMT) {
  case(S_IFREG):
    write_regular(out);
    break;
  case(S_IFLNK):
    write_link(out);
    break;
  case(S_IFCHR):
  case(S_IFBLK):
    write_dev(out);
    break;
  default:
    break;
  }

CRC:
  /* write crc if needed */
  if(A_HAS_CRC(out)) {
    uint32_t s_crc = htole32(out->crc);
    xwrite(out->fd, &s_crc, sizeof(s_crc));
  }

  /* update remote mode */
  if(rmode)
    *rmode = out->stat.st_mode;

  /* display file */
  show_file(out, out->wp, out->link, out->stat.st_mode, mode, out->stat.st_uid,
            out->stat.st_gid, out->stat.st_size, out->stat.st_atime,
            out->stat.st_mtime, out->crc, A_HAS_CRC(out));
  if(out->link)
    free(out->link);

  /* reupdate access time */
  reupdate_time(out);

  return 0;
}

static void reupdate_time(const struct sar_file *out)
{
  struct timespec times[2];

  times[0].tv_sec  = out->stat.st_atime;
  times[0].tv_nsec = out->stat.st_atim.tv_nsec;

  times[1].tv_sec  = out->stat.st_mtime;
  times[1].tv_nsec = out->stat.st_mtim.tv_nsec;

  utimensat(AT_FDCWD, out->wp, times, AT_SYMLINK_NOFOLLOW);
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
struct sar_file * sar_read(const char *path,
                           const char *compress,
                           unsigned int verbose)
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

  if(compress) {
    int fd[2];
    pid_t pid;

    xpipe(fd);
    pid = xfork();

    if(!pid) /* child */ {
      close(fd[0]);

      xdup2(out->fd, STDIN_FILENO);
      xdup2(fd[1], STDOUT_FILENO);

      close(out->fd);
      close(fd[1]);

      execlp(compress, compress, "-d", NULL);
      err(EXIT_FAILURE, "cannot execute \"%s\"", compress);
    }

    close(fd[1]);
    out->fd = fd[0];
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
  char iobuf[IO_SZ];
  enum fsclass class;
  off_t size;
  int fd;

  /* read size first */
  class = out->nsclass & N_FILE;
  switch(class) {
    uint8_t  size_byte;
    uint16_t size_kilo;
    uint32_t size_giga;
    uint64_t size_huge;

  case(N_FBYTE):
    xcrc_read(out, &size_byte, sizeof(size_byte));
    size = size_byte;
    break;
  case(N_FKILO):
    xcrc_read(out, &size_kilo, sizeof(size_kilo));
    size = le16toh(size_kilo);
    break;
  case(N_FGIGA):
    xcrc_read(out, &size_giga, sizeof(size_giga));
    size = le32toh(size_giga);
    break;
  case(N_FHUGE):
    xcrc_read(out, &size_huge, sizeof(size_huge));
    size = le64toh(size_huge);
    break;
  }

  out->size = size;

  /* when we list the archive we don't want to read
     to whole file */
  if(out->list_only) {
    xskip(out->fd, size);
    return;
  }

  /* open output file */
  fd = open(out->wp, O_CREAT | O_RDWR | O_TRUNC, mode);
  if(fd < 0)
    err(EXIT_FAILURE, "could not open output file \"%s\"", out->wp);

  /* read file */
  while(size) {
    size_t n = MIN(size, IO_SZ);

    xcrc_read(out, iobuf, n);

    /* copy buffer */
    xwrite(fd, iobuf, n);

    size -= n;
  }

  assert(size == 0);

  close(fd);
}

static void read_dir(struct sar_file *out, mode_t mode)
{
  if(!out->list_only && mkdir(out->wp, mode) < 0)
    warn("cannot create directory \"%s\"", out->wp);
}

static void read_link(struct sar_file *out, mode_t mode)
{
  char path[WP_MAX + 1];
  enum fsclass class;
  uint16_t size;

  /* read link length */
  class = out->nsclass & N_FILE;
  switch(class) {
    uint8_t  size_byte;
    uint16_t size_kilo;

  case(N_FBYTE):
    xcrc_read(out, &size_byte, sizeof(size_byte));
    size = size_byte;
    break;
  case(N_FKILO):
    xcrc_read(out, &size_kilo, sizeof(size_kilo));
    size = le16toh(size_kilo);
    break;
  case(N_FGIGA):
  case(N_FHUGE):
    errx(EXIT_FAILURE, "link size too large for \"%s\"", out->wp);
  }
  out->size = size;

  /* check size and extract path */
  if(size > WP_MAX)
    errx(EXIT_FAILURE, "path size exceeded");
  xcrc_read(out, path, size);
  path[size] = '\0';

  /* duplicate path for displaying
     it will be freed later by caller */
  out->link = strdup(path);
  if(!out->list_only && symlink(path, out->wp) < 0)
    warn("cannot create symlink \"%s\" to \"%s\"", out->wp, path);
}

static void read_fifo(struct sar_file *out, mode_t mode)
{
  if(!out->list_only && mkfifo(out->wp, mode) < 0)
    warn("cannot create fifo \"%s\"", out->wp);
}

static void read_device(struct sar_file *out, mode_t mode)
{
  uint64_t dev;

  /* used for displaying */
  out->size = sizeof(dev);

  /* avoid reading when listing the archive */
  if(out->list_only) {
    xskip(out->fd, sizeof(dev));
    return;
  }

  xcrc_read(out, &dev, sizeof(dev));
  dev = le64toh(dev);

  if(mknod(out->wp, mode, dev) < 0)
    err(EXIT_FAILURE, "cannot create device \"%s\"", out->wp);
}

static void read_hardlink(struct sar_file *out, mode_t mode)
{
  char path[WP_MAX + 1];
  uint16_t size;

  /* read link length */
  xcrc_read(out, &size, sizeof(size));
  size = le16toh(size);
  out->size = size;

  /* check size and extract path */
  if(size > WP_MAX)
    errx(EXIT_FAILURE, "path size exceeded");
  xcrc_read(out, path, size);
  path[size] = '\0';

  /* duplicate link path for displaying
     it will be freed later by caller */
  out->link = strdup(path);
  if(!out->list_only && link(path, out->wp) < 0)
    warnx("cannot create hardlink \"%s\" to \"%s\"", out->wp, path);
}

static int rec_extract(struct sar_file *out, size_t idx)
{
  assert(out);
  assert(out->fd);
  assert(out->wp);

  char name[NODE_MAX + 1];
  enum isclass iclass;
  enum tsclass tclass;
  time_t atime = 0;
  time_t mtime = 0;
  uid_t uid = 0;
  gid_t gid = 0;
  mode_t real_mode;
  uint32_t atime_ns = 0;
  uint32_t mtime_ns = 0;
  uint32_t crc;
  uint16_t mode;
  uint8_t size, i;

  /* setup crc and fallback variables we don't
     care if we compute it or not */
  out->crc = 0;
  out->link = NULL;
  out->size = 0;

  /* read mode and convert endianess */
  xcrc_read(out, &mode, sizeof(mode));
  mode = le16toh(mode);

  /* switch for control and hardlink */
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

  /* read node size class */
  xcrc_read(out, &out->nsclass, sizeof(out->nsclass));

  /* read uid/gid */
  iclass = out->nsclass & N_ID;
  switch(iclass) {
    uint8_t  id_byte;
    uint16_t id_kilo;
    uint32_t id_giga;

  case(N_IRR):
    uid = 0;
    gid = 0;
    break;
  case(N_IUU):
    uid = 1000;
    gid = 1000;
    break;
  case(N_ISRB):
    xcrc_read(out, &id_byte, sizeof(id_byte));
    uid = id_byte;
    gid = id_byte;
    break;
  case(N_ISUB):
    xcrc_read(out, &id_byte, sizeof(id_byte));
    uid = id_byte + 1000;
    gid = id_byte + 1000;
    break;
  case(N_IRB):
    xcrc_read(out, &id_byte, sizeof(id_byte));
    uid = 0;
    gid = id_byte;
    break;
  case(N_IUB):
    xcrc_read(out, &id_byte, sizeof(id_byte));
    uid = 1000;
    gid = id_byte + 1000;
    break;
  case(N_ISKILO):
    xcrc_read(out, &id_kilo, sizeof(id_kilo));
    id_kilo = le16toh(id_kilo);
    uid = id_kilo;
    gid = id_kilo;
    break;
  case(N_IBBYTE):
    xcrc_read(out, &id_byte, sizeof(id_byte));
    uid = id_byte;

    xcrc_read(out, &id_byte, sizeof(id_byte));
    gid = id_byte;
    break;
  case(N_IBUBYTE):
    xcrc_read(out, &id_byte, sizeof(id_byte));
    uid = id_byte + 1000;

    xcrc_read(out, &id_byte, sizeof(id_byte));
    gid = id_byte + 1000;
    break;
  case(N_IBK):
    xcrc_read(out, &id_byte, sizeof(id_byte));
    xcrc_read(out, &id_kilo, sizeof(id_kilo));
    id_kilo = le16toh(id_kilo);
    uid = id_byte;
    gid = id_kilo;
    break;
  case(N_IKB):
    xcrc_read(out, &id_kilo, sizeof(id_kilo));
    xcrc_read(out, &id_byte, sizeof(id_byte));
    id_kilo = le16toh(id_kilo);
    uid = id_kilo;
    gid = id_byte;
    break;
  case(N_ISGIGA):
    xcrc_read(out, &id_giga, sizeof(id_giga));
    id_giga = le32toh(id_giga);
    uid = id_giga;
    gid = id_giga;
    break;
  case(N_IBKILO):
    xcrc_read(out, &id_kilo, sizeof(id_kilo));
    id_kilo = le16toh(id_kilo);
    uid = id_kilo;

    xcrc_read(out, &id_kilo, sizeof(id_kilo));
    id_kilo = le16toh(id_kilo);
    gid = id_kilo;
    break;
  case(N_IKG):
    xcrc_read(out, &id_kilo, sizeof(id_kilo));
    xcrc_read(out, &id_giga, sizeof(id_giga));
    id_kilo = le16toh(id_kilo);
    id_giga = le32toh(id_giga);
    uid = id_kilo;
    gid = id_giga;
    break;
  case(N_IGK):
    xcrc_read(out, &id_giga, sizeof(id_giga));
    xcrc_read(out, &id_kilo, sizeof(id_kilo));
    id_kilo = le16toh(id_kilo);
    id_giga = le32toh(id_giga);
    uid = id_giga;
    gid = id_kilo;
  case(N_IGG):
    xcrc_read(out, &id_giga, sizeof(id_giga));
    id_giga = le32toh(id_giga);
    uid = id_giga;

    xcrc_read(out, &id_giga, sizeof(id_giga));
    id_giga = le32toh(id_giga);
    gid = id_giga;
    break;
  }

  /* read atime/mtime */
  tclass = out->nsclass & N_TIME;
  switch(tclass) {
    int32_t time_giga;
    int64_t time_huge;

  case(N_TS32):
    xcrc_read(out, &time_giga, sizeof(time_giga));
    time_giga = le32toh(time_giga);
    atime = time_giga;
    mtime = time_giga;
    break;
  case(N_TS64):
    xcrc_read(out, &time_huge, sizeof(time_huge));
    time_huge = le64toh(time_huge);
    atime = time_huge;
    mtime = time_huge;
    break;
  case(N_TB32):
    xcrc_read(out, &time_giga, sizeof(time_giga));
    atime = le32toh(time_giga);

    xcrc_read(out, &time_giga, sizeof(time_giga));
    mtime = le32toh(time_giga);
    break;
  case(N_TB64):
    xcrc_read(out, &time_huge, sizeof(time_huge));
    atime = le64toh(time_huge);

    xcrc_read(out, &time_huge, sizeof(time_huge));
    mtime = le64toh(time_huge);
    break;
  }

  if(A_HAS_NTIME(out)) {
    xcrc_read(out, &atime_ns, sizeof(atime_ns));
    xcrc_read(out, &mtime_ns, sizeof(mtime_ns));
    atime_ns = le32toh(atime_ns);
    mtime_ns = le32toh(mtime_ns);
  }

EXTRACT_NAME:
  /* extract name size, size is one byte so
     we don't do the endianess conversion */
  xcrc_read(out, &size, sizeof(size));

  /* checkup size and extract name */
  if(size > NODE_MAX)
    errx(EXIT_FAILURE, "node max size exceeded");
  xcrc_read(out, name, size);

#ifndef DISABLE_WP_WIDTH_CHECK
  if(idx + size >= WP_MAX)
    errx(EXIT_FAILURE, "maximum size exceeded for working path");
#endif /* WP_WIDTH_CHECK */

  /* copy name to working path */
  for(i = 0 ; i < size ; i++)
    out->wp[idx + i] = name[i];
  out->wp[idx + size] = '\0';

  /* we would like to see the real mode too */
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

  /* change attributes of the extracted file */
  if(!out->list_only) {
    lchown(out->wp, uid, gid);

    /* avoid dereference symbolic links */
    if((mode & M_IFMT) != M_ILNK)
      chmod(out->wp, real_mode);

    if(A_HAS_NTIME(out)) {
      struct timespec times[2];

      times[0].tv_sec  = atime;
      times[0].tv_nsec = (long)atime_ns;

      times[1].tv_sec  = mtime;
      times[1].tv_nsec = (long)mtime_ns;

      utimensat(AT_FDCWD, out->wp, times, AT_SYMLINK_NOFOLLOW);
    }
    else {
      struct utimbuf times;

      times.actime  = atime;
      times.modtime = mtime;
      utime(out->wp, &times);
    }
  }

  /* compute crc */
  if(A_HAS_CRC(out)) {
    xxread(out->fd, &crc, sizeof(crc));
    crc = le32toh(crc);

    if(!out->list_only && crc != out->crc)
      warnx("corrupted file \"%s\"", out->wp);
  }

  show_file(out, out->wp, out->link, real_mode, mode,
            uid, gid, out->size, atime, mtime, out->list_only ? crc : out->crc,
            A_HAS_CRC(out));
  if(out->link)
    free(out->link);

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
         "\tHas nano time  : %s\n",
         out->version,
         S_BOOLEAN(A_HAS_CRC(out)),
         S_BOOLEAN(A_HAS_NTIME(out)));
}
