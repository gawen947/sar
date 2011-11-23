/* File: sar.h
   Time-stamp: <2011-11-23 16:53:23 gawen>

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
# define PACKAGE_VERSION VERSION " (commit:" PARTIAL_COMMIT ")" /* add git commit */
#endif /* COMMIT */

#define MAGIK_VERSION 0          /* simple archive file format version
                                    (up to 255) */
#define MAGIK_FMT_VER 0xff000000 /* bit mask for archive file format bits fields
                                    in magik number */
#define MAGIK      (0x00524153 | MAGIK_VERSION)

struct sar_file {
  int fd;                  /* file descriptor of the archive */
  uint8_t flags;           /* flags of this archive */
  uint8_t version;         /* version of sar archive */

  unsigned int verbose;    /* verbose level */
  bool list_only;          /* do not extract file skip them instead */

  char *wp;                /* working path */
  size_t wp_sz;            /* working path size */
  size_t wp_idx;           /* working path index */
  size_t wp_max;           /* working path max size */
  struct stat stat;        /* current stat information */
  uint8_t nsclass;         /* current node size class */
  uint32_t crc;            /* current crc */
  char *link;              /* symlink or hardlink destination */
  off_t size;              /* size of a node */


  struct sar_hardlink *hl_tbl; /* hard link table */
  size_t hl_tbl_sz;
};

struct sar_hardlink {
  ino_t inode;      /* inode number */
  dev_t device;     /* device id */
  nlink_t links;    /* number of hard links */
  char *path;       /* path to the file */
};

/* mode related flags */
/* file format flags */
#define M_IFMT           0x7 /* bit mask for file type bit fields */
enum mformat { M_IREG  = 0x0, /* regular file */
               M_IDIR  = 0x1, /* directory */
               M_ILNK  = 0x2, /* link */
               M_IFIFO = 0x3, /* fifo */
               M_IBLK  = 0x4, /* block device */
               M_ICHR  = 0x5, /* character device */
               M_IHARD = 0x6, /* hardlink */
               M_ICTRL = 0x7, /* control node */ };

/* permission flags */
#define M_IPERM        0xfff8 /* bit maks for permission type bit fields */
enum mperm { M_ISUID = 0x8,    /* set UID bit */
             M_ISGID = 0x10,   /* set-group-ID bit */
             M_ISVTX = 0x20,   /* sticky bit */
             M_IRUSR = 0x40,   /* owner has read permission */
             M_IWUSR = 0x80,   /* owner has write permission */
             M_IXUSR = 0x100,  /* owner has execute permission */
             M_IRGRP = 0x200,  /* group has read permission */
             M_IWGRP = 0x400,  /* group has write permission */
             M_IXGRP = 0x800,  /* group has execute permission */
             M_IROTH = 0x1000, /* other has read permission */
             M_IWOTH = 0x2000, /* other has write permission */
             M_IXOTH = 0x4000, /* other has execute permission */ };

/* control mode flags */
enum mctrl { M_C_CHILD  = 0x0, /* end of children */
             M_C_IGNORE = 0x8, /* unsupported file type or dummy */ };

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
#define A_ICRC    0x1                 /* use a checksum for each file */
#define A_INTIME  0x2                 /* use nanosecond timestamp */
#define A_IMASK   (A_ICRC | A_INTIME) /* flags mask */
#define A_HAS(a, t)    ((a->flags) & A_I ## t)
#define A_HAS_CRC(a)    A_HAS(a, CRC)
#define A_HAS_NTIME(a)  A_HAS(a, NTIME)

/* node size class related flags */
/* file size class flags */
#define N_FILE           0x3  /* bit mask for file size class bit fields */
enum fsclass { N_FBYTE = 0x0, /* byte file size class ( 0   - 255 ) Bytes */
               N_FKILO = 0x1, /* kilo file size class ( 255 - 65K ) Bytes */
               N_FGIGA = 0x2, /* giga file size class ( 65K - 4G  ) Bytes */
               N_FHUGE = 0x3  /* huge file size class ( 4G  - 10E ) Bytes */ };

/* id size class flags */
#define N_ID                0x3c  /* bit mask for id size class bit fields */
enum isclass { N_IRR      = 0x0,  /* root/root id size class (0 Byte) */
               N_IUU      = 0x4,  /* user/user id size class (0 Byte) */
               N_ISRB     = 0x8,  /* same root byte id size class (1 Byte) */
               N_ISUB     = 0xc,  /* same user byte id size class (1 Byte) */
               N_IRB      = 0x10, /* root/byte id size class (1 Byte) */
               N_IUB      = 0x14, /* user/byte id size class (1 Byte) */
               N_ISKILO   = 0x18, /* same kilo id size class (2 Bytes) */
               N_IBBYTE   = 0x1c, /* both byte id size class (2 Bytes) */
               N_IBUBYTE  = 0x20, /* both user byte id size class (2 Bytes) */
               N_IBK      = 0x24, /* byte/kilo id size class (3 Bytes) */
               N_IKB      = 0x28, /* kilo/byte id size class (3 Bytes) */
               N_ISGIGA   = 0x2c, /* same giga id size class (4 Bytes) */
               N_IBKILO   = 0x30, /* kilo/kilo id size class (4 Bytes) */
               N_IKG      = 0x34, /* kilo/giga id size class (6 Bytes) */
               N_IGK      = 0x38, /* giga/kilo id size class (6 Bytes) */
               N_IGG      = 0x3c, /* giga/giga id size class (8 Bytes) */ };

/* time size class flags */
#define N_TIME          0xc0  /* bit mask for time size class bit fields */
enum tsclass { N_TS32 = 0x0,  /* same 32bits time size class (4 Bytes) */
               N_TS64 = 0x40, /* same 64bits time size class (8 Bytes) */
               N_TB32 = 0x80, /* both 32bits time size class (8 Bytes) */
               N_TB64 = 0xc0, /* both 64bits time size class (8 Bytes) */ };

/* default and max sizes */
enum max     { WP_MAX = 4095,
               NODE_MAX = 255,
               DATE_MAX = 255 };
enum size    { HL_TBL_SZ = 1024,
               IO_SZ     = 65536 };

/* misc. */
#define DATE_FORMAT "%d %b %Y %H:%M"

struct sar_file * sar_creat(const char *path,
                            const char *compress,
                            bool use_crc,
                            bool use_ntime,
                            unsigned int verbose);
struct sar_file * sar_read(const char *path,
                           const char *compress,
                           unsigned int verbose);
void sar_add(struct sar_file *out, const char *path);
void sar_extract(struct sar_file *out);
void sar_list(struct sar_file *out);
void sar_info(struct sar_file *out);
void sar_close(struct sar_file *file);

#endif /* _SAR_H_ */
