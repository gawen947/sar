/* File: main.c
   Time-stamp: <2011-07-16 00:02:25 gawen>

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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <sysexits.h>
#include <errno.h>
#include <time.h>
#include <err.h>

#include "egg.h"
#include "sar.h"
#include "common.h"

enum mode { MD_NONE = 0,
            MD_INFORMATION,
            MD_CREATE,
            MD_EXTRACT,
            MD_LIST };

struct opts_val {
  unsigned int verbose;
  enum mode mode;

  bool use_file;
  bool crc;
  bool wide_id;
  bool wide_stamp;
  bool nano_time;

  const char *file;
  const char *source;
};

static void version()
{
  printf(PACKAGE "-" PACKAGE_VERSION "\n");
  exit(EXIT_SUCCESS);
}

static void help(const struct option *opts, const char *prog_name)
{
  const struct option *opt;
  const char **hlp;
  int size;
  int max = 0;

  /* help messages */
  const char *opts_help[] = {
    "Print version information",                             /* -V */
    "Print this message",                                    /* -h */
    "Be verbose (may be used multiple times)",               /* -v */
    "Checkup capabilities",                                  /* -k */
    "Display basic informations about an archive",           /* -i */
    "Create a new archive",                                  /* -c */
    "Extract all files from an archive",                     /* -x */
    "List all files in an archive",                          /* -t */
    "Use a file instead of standard input/output",           /* -f */
    "Add integrity checks to each file in the archive",      /* -C */
    "Use wider user/group id",                               /* -U */
    "Use wider timestamp (avoid year 1901/2038 problem)",    /* -T */
    "Use more precise timestamps (upto nanoseconds)",        /* -M */
    "Equivalent to -CUTN"                                    /* -w */
  };

  fprintf(stderr, "Usage: %s [OPTIONS]\n", prog_name);

  /* maximum option name size for padding */
  for(opt = opts ; opt->name ; opt++) {
    size = strlen(opt->name);
    if(size > max)
      max = size;
  }

  /* print options and help messages */
  for(opt = opts, hlp = opts_help ; opt->name ; opt++, hlp++) {
    fprintf(stderr, "  -%c, --%s", opt->val, opt->name);

    /* padding */
    size = strlen(opt->name);
    for(; size <= max ; size++)
      fputc(' ', stderr);
    fprintf(stderr, "%s\n", *hlp);
  }
}

static void checkup_cap()
{
  time_t now = time(NULL);

  printf("System:\n");
  printf(" Time width : %lu bits\n", 8 * sizeof(time_t));
  printf(" UID width  : %lu bits\n", 8 * sizeof(uid_t));
  printf(" GID width  : %lu bits\n", 8 * sizeof(gid_t));
  printf(" Mode width : %lu bits\n", 8 * sizeof(mode_t));
  printf("\n");

  if((int32_t)now == now)
    printf(" Should use '-T' option : no\n");
  else
    printf(" Should use '-T' option : yes\n");
}

static void except_archive(int argc, int optind, char *argv[],
                           struct opts_val *val)
{
  if(val->use_file) {
    if(argc - optind != 1)
      errx(EXIT_FAILURE, "except archive name");
    val->file = argv[optind];
  }
  else if(argc - optind)
    errx(EXIT_FAILURE, "except no arguments");
  else
    val->file = NULL;
}

static void except_more(int argc, int optind, char *argv[],
                        struct opts_val *val)
{
  if(val->use_file) {
    if(argc - optind != 2)
      errx(EXIT_FAILURE, "except archive name and a path to archive");
    val->file   = argv[optind++];
    val->source = argv[optind];
  }
  else if(argc - optind != 1)
    errx(EXIT_FAILURE, "except a path to archive");
  else {
    val->file   = NULL;
    val->source = argv[optind];
  }
}


static void cmdline(int argc, char *argv[], struct opts_val *val)
{
  int exit_status = EXIT_FAILURE;

  struct option opts[] = {
    { "version", no_argument, NULL, 'V' },
    { "help", no_argument, NULL, 'h' },
    { "verbose", no_argument, NULL, 'v' },
    { "cap", no_argument, NULL, 'k' },
    { "information", no_argument, NULL, 'i' },
    { "create", no_argument, NULL, 'c' },
    { "extract", no_argument, NULL, 'x' },
    { "list", no_argument, NULL, 't' },
    { "file", no_argument, NULL, 'f' },
    { "crc", no_argument, NULL, 'C' },
    { "wide-id", no_argument, NULL, 'U' },
    { "wide-time", no_argument, NULL, 'T' },
    { "nano-time", no_argument, NULL, 'N' },
    { "wide", no_argument, NULL, 'w' },
    { NULL, 0, NULL, 0 }
  };

  /* retrieve program's name */
  const char *pgn = (const char *)strrchr(argv[0], '/');
  pgn = pgn ? (pgn + 1) : argv[0];

  while(1) {
    int c = getopt_long(argc, argv, "VhvkicxtfCUTNw", opts, NULL);

    if(c == -1)
      break;

    switch(c) {
    case 'v':
      val->verbose++;
      break;
    case 'i':
      val->mode = MD_INFORMATION;
      break;
    case 'c':
      val->mode = MD_CREATE;
      break;
    case 'x':
      val->mode = MD_EXTRACT;
      break;
    case 't':
      val->mode = MD_LIST;
      break;
    case 'f':
      val->use_file = true;
      break;
    case 'C':
      val->crc = true;
      break;
    case 'U':
      val->wide_id = true;
      break;
    case 'T':
      val->wide_stamp = true;
      break;
    case 'M':
      val->nano_time = true;
      break;
    case 'w':
      val->crc        = true;
      val->wide_id    = true;
      val->wide_stamp = true;
      val->nano_time = true;
      break;
    case 'k':
      checkup_cap();
      exit(EXIT_SUCCESS);
    case 'V':
      version();
      exit(EXIT_SUCCESS);
    case 'h':
      exit_status = EXIT_SUCCESS;
    default:
      help(opts, pgn);
      exit(exit_status);
    }
  }

  /* consider remaining arguments */
  switch(val->mode) {
  case(MD_NONE):
    errx(EXIT_SUCCESS, "You must specify one of the 'cxti' or '--cap' options\n"
         "Try '%s --help'", pgn);
  case(MD_INFORMATION):
    if(val->use_file) {
      if(argc - optind != 1)
        errx(EXIT_FAILURE, "except archive name");
      val->file = argv[optind];
    }
    else if(argc - optind)
      errx(EXIT_FAILURE, "except no arguments");
    else
      val->file = NULL;
    break;
  case(MD_CREATE):
    except_more(argc, optind, argv, val);
    break;
  case(MD_LIST):
  case(MD_EXTRACT):
    except_archive(argc, optind, argv, val);
    break;
  }

#ifndef DISABLE_EGGS
    q0(val->verbose);
#endif /* EGGS */
}

int main(int argc, char *argv[])
{
  struct opts_val val = {0};
  struct sar_file *f;

  cmdline(argc, argv, &val);

  switch(val.mode) {
  case(MD_NONE):
    break;
  case(MD_INFORMATION):
    f = sar_read(val.file, val.verbose);
    sar_info(f);
    break;
  case(MD_CREATE):
    f = sar_creat(val.file, val.wide_id, val.wide_stamp, val.crc,
                  val.nano_time, val.verbose);
    sar_add(f, val.source);
    break;
  case(MD_EXTRACT):
    f = sar_read(val.file, val.verbose);
    sar_extract(f);
    break;
  case(MD_LIST):
    f = sar_read(val.file, val.verbose);
    sar_list(f);
    break;
  }

  exit(EXIT_SUCCESS);
}
