/* File: translation.c
   Time-stamp: <2011-07-13 23:46:55 gawen>

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

#include "sar.h"

/* this file contains the translation functions for the types of the
   operating system to the archive types and vice versa */

uint16_t mode2uint16(mode_t mode)
{
  register uint16_t sar_mode;

  /* file type */
  switch(mode & S_IFMT) {
  case(S_IFREG):
    sar_mode = M_IREG;
    break;
  case(S_IFDIR):
    sar_mode = M_IDIR;
    break;
  case(S_IFLNK):
    sar_mode = M_ILNK;
    break;
  case(S_IFIFO):
    sar_mode = M_IFIFO;
    break;
  case(S_IFBLK):
    sar_mode = M_IBLK;
    break;
  case(S_IFCHR):
    sar_mode = M_ICHR;
    break;
  default:
    /* other files type are simply ignored
       this is the case for sockets */
    sar_mode = M_ICTRL | M_C_IGNORE;
    return sar_mode;
  }

  /* set UID/GID, sticky bit */
  if(mode & S_ISUID)
    sar_mode |= M_ISUID;
  if(mode & S_ISGID)
    sar_mode |= M_ISGID;
  if(mode & S_ISVTX)
    sar_mode |= M_ISVTX;

  /* owner permission */
  if(mode & S_IRUSR)
    sar_mode |= M_IRUSR;
  if(mode & S_IWUSR)
    sar_mode |= M_IWUSR;
  if(mode & S_IXUSR)
    sar_mode |= M_IXUSR;

  /* group permission */
  if(mode & S_IRGRP)
    sar_mode |= M_IRGRP;
  if(mode & S_IWGRP)
    sar_mode |= M_IWGRP;
  if(mode & S_IXGRP)
    sar_mode |= M_IXGRP;

  /* owner permission */
  if(mode & S_IROTH)
    sar_mode |= M_IROTH;
  if(mode & S_IWOTH)
    sar_mode |= M_IWOTH;
  if(mode & S_IXOTH)
    sar_mode |= M_IXOTH;

  return sar_mode;
}

mode_t uint162mode(uint16_t sar_mode)
{
  register mode_t mode;

  /* file type */
  switch(sar_mode & M_IFMT) {
  case(M_IREG):
    mode = S_IFREG;
    break;
  case(M_IDIR):
    mode = S_IFDIR;
    break;
  case(M_ILNK):
    mode = S_IFLNK;
    break;
  case(M_IFIFO):
    mode = S_IFIFO;
    break;
  case(M_IBLK):
    mode = S_IFBLK;
    break;
  case(M_ICHR):
    mode = S_IFCHR;
    break;
  default:
    /* any other type is not describable as a
       mode_t type so we return -1 to return
       something */
    return -1;
  }

  /* set UID/GID, sticky bit */
  if(sar_mode & M_ISUID)
    mode |= S_ISUID;
  if(sar_mode & M_ISGID)
    mode |= S_ISGID;
  if(sar_mode & M_ISVTX)
    mode |= S_ISVTX;

  /* owner permission */
  if(sar_mode & M_IRUSR)
    mode |= S_IRUSR;
  if(sar_mode & M_IWUSR)
    mode |= S_IWUSR;
  if(sar_mode & M_IXUSR)
    mode |= S_IXUSR;

  /* group permission */
  if(sar_mode & M_IRGRP)
    mode |= S_IRGRP;
  if(sar_mode & M_IWGRP)
    mode |= S_IWGRP;
  if(sar_mode & M_IXGRP)
    mode |= S_IXGRP;

  /* owner permission */
  if(sar_mode & M_IROTH)
    mode |= S_IROTH;
  if(sar_mode & M_IWOTH)
    mode |= S_IWOTH;
  if(sar_mode & M_IXOTH)
    mode |= S_IXOTH;

  return mode;
}
