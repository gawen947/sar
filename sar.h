/* File: sar.h
   Time-stamp: <2011-07-14 17:57:12 gawen>

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

#ifndef _SAR_H_
#define _SAR_H_

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#ifdef HAVE_CONFIG
#include "config.h"
#endif /* HAVE_CONFIG */

#define PACKAGE      "sar"            /* name */
#define PACKAGE_LONG "Simple ARchive" /* long name */

#ifndef COMMIT
# define PACKAGE_VERSION VERSION
#else
# define PACKAGE_VERSION VERSION " (commit:" COMMIT ")" /* add git commit */
#endif /* COMMIT */

#define MAGIK_VERSION 0          /* simple archive file format version
                                    (up to 255) */
#define MAGIK_FMT_VER 0xff000000 /* bit mask for archive file format bits fields
                                    in magik number */
#define MAGIK      (0x00524153 | MAGIK_VERSION)

struct sar_file {
  int fd;          /* file descriptor of the archive */
  uint8_t flags;   /* flags of this archive */
  uint8_t version; /* version of sar archive */

  char *wp;        /* working path */
  size_t wp_sz;    /* working path size */
  size_t wp_idx;   /* working path index */
  size_t wp_max;   /* working path max size */

  struct sar_hardlink *hl_tbl; /* hard link table */
  size_t hl_tbl_sz;

  uint32_t crc;    /* current crc */
};

struct sar_node {
  /* every node */
  uint16_t mode;    /* filetype and permissions */
  uint32_t uid;     /* user id */
  uint32_t gid;     /* group id */
  int64_t atime;   /* access time */
  int64_t mtime;   /* modification time */
  uint32_t crc;     /* checksum */
  const char *name; /* node name */

  /* regular file */
  uint64_t size;    /* file size */

  /* symlink, hardlink */
  const char *path; /* link path */

  /* device */
  uint32_t dev;     /* device (minor/major) */
};

struct sar_hardlink {
  ino_t inode;      /* inode number */
  dev_t device;     /* device id */
  nlink_t links;    /* number of hard links */
  char *path;       /* path to the file */
};

/* mode related flags */
/* file format flags */
#define M_IFMT  0x7    /* bit mask for file type bit fields */
#define M_IREG  0x0    /* regular file */
#define M_IDIR  0x1    /* directory */
#define M_ILNK  0x2    /* link */
#define M_IFIFO 0x3    /* fifo */
#define M_IBLK  0x4    /* block device */
#define M_ICHR  0x5    /* character device */
#define M_IHARD 0x6    /* hardlink */
#define M_ICTRL 0x7    /* control node */

/* permission flags */
#define M_ISUID 0x8    /* set UID bit */
#define M_ISGID 0x10   /* set-group-ID bit */
#define M_ISVTX 0x20   /* sticky bit */
#define M_IRUSR 0x40   /* owner has read permission */
#define M_IWUSR 0x80   /* owner has write permission */
#define M_IXUSR 0x100  /* owner has execute permission */
#define M_IRGRP 0x200  /* group has read permission */
#define M_IWGRP 0x400  /* group has write permission */
#define M_IXGRP 0x800  /* group has execute permission */
#define M_IROTH 0x1000 /* other has read permission */
#define M_IWOTH 0x2000 /* other has write permission */
#define M_IXOTH 0x4000 /* other has execute permission */

/* control mode flags */
#define M_C_CHILD  0x0 /* end of children */
#define M_C_IGNORE 0x8 /* unsupported file type or dummy */

/* file types macro */
#define M_IS(m, t) ((m & M_IFMT) == M_I ## t)
#define M_ISREG(m)  M_IS(m, REG)
#define M_ISDIR(m)  M_IS(m, DIR)
#define M_ISLNK(m)  M_IS(m, LNK)
#define M_ISFIFO(m) M_IS(m, FIFO)
#define M_ISBLK(m)  M_IS(m, BLK)
#define M_ISCHR(m)  M_IS(m, CHR)
#define M_ISHARD(m) M_IS(m, HARD)
#define M_ISCTRL(m) M_IS(m, CTRL)

/* archive flags */
#define A_I32ID   0x1 /* use 32 bits uid/gid instead of 16 bits */
#define A_I64TIME 0x2 /* use 64 bits time instead of 32 bits */
#define A_ICRC    0x4 /* use a checksum for each file */

#define A_HAS(a, t)    ((a->flags) & A_I ## t)
#define A_HAS_32ID(a)   A_HAS(a, 32ID)
#define A_HAS_64TIME(a) A_HAS(a, 64TIME)
#define A_HAS_CRC(a)    A_HAS(a, CRC)

/* default and max sizes */
enum max     { WP_MAX = PATH_MAX };
enum size    { HL_TBL_SZ = 1024,
               IO_SZ     = 32768 };

struct sar_file * sar_creat(const char *path,
                            bool use_32id,
                            bool use_64time,
                            bool use_crc);
struct sar_file * sar_read(const char *path);
void sar_add(struct sar_file *out, const char *path);
void sar_extract(struct sar_file *out);
void sar_close(struct sar_file *file);

#endif /* _SAR_H_ */
