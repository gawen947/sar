/* File: main.c
   Time-stamp: <2011-07-15 01:24:12 gawen>

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
#include <err.h>

#include "egg.h"
#include "sar.h"

enum mode { MD_NONE = 0,
            MD_INFORMATION,
            MD_CREATE,
            MD_EXTRACT,
            MD_LIST };

struct opts_val {
  unsigned int verbose;
  enum mode mode;

  bool file;
  bool crc;
  bool wide_id;
  bool wide_stamp;
  bool micro_time;

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
    "Display basic informations about an archive",           /* -i */
    "Create a new archive",                                  /* -c */
    "Extract all files from an archive",                     /* -x */
    "List all files in an archive",                          /* -t */
    "Use a file instead of standard input/output",           /* -f */
    "Add integrity checks to each file in the archive",      /* -C */
    "Use wider user/group id",                               /* -U */
    "Use wider timestamp (avoid year 1901/2038 problem)",    /* -T */
    "Use more precise timestamps",                           /* -M */
    "Equivalent to -CUTM"                                    /* -w */
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

static void cmdline(int argc, char *argv[], struct opts_val *val)
{
  int exit_status = EXIT_FAILURE;

  struct option opts[] = {
    { "version", no_argument, NULL, 'V' },
    { "help", no_argument, NULL, 'h' },
    { "verbose", no_argument, NULL, 'v' },
    { "information", no_argument, NULL, 'i' },
    { "create", no_argument, NULL, 'c' },
    { "extract", no_argument, NULL, 'x' },
    { "list", no_argument, NULL, 't' },
    { "file", no_argument, NULL, 'f' },
    { "crc", no_argument, NULL, 'C' },
    { "wide-id", no_argument, NULL, 'U' },
    { "wide-time", no_argument, NULL, 'T' },
    { "micro-time", no_argument, NULL, 'M' },
    { "wide", no_argument, NULL, 'w' }
    { NULL, 0, NULL, 0 }
  };

  /* retrieve program's name */
  const char *pgn = (const char *)strrchr(argv[0], '/');
  pgn = pgn ? (pgn + 1) : argv[0];

  while(1) {
    int c = getopt_long(argc, argv, "VhvicxtfCUTMw", opts, NULL);

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
      val->file = true;
      break;
    case 'C':
      if(val->mode != MD_CREATE)
        errx(EXIT_FAILURE, "%c should be used with 'create' mode", c);
      val->crc = true;
      break;
    case 'U':
      if(val->mode != MD_CREATE)
        errx(EXIT_FAILURE, "%c should be used with 'create' mode", c);
      val->wide_id = true;
      break;
    case 'T':
      if(val->mode != MD_CREATE)
        errx(EXIT_FAILURE, "%c should be used with 'create' mode", c);
      val->wide_stamp = true;
      break;
    case 'M':
      if(val->mode != MD_CREATE)
        errx(EXIT_FAILURE, "%c should be used with 'create' mode", c);
      val->micro_time = true;
      break;
    case 'w':
      if(val->mode != MD_CREATE)
        errx(EXIT_FAILURE, "%c should be used with 'create' mode", c);
      val->crc        = true;
      val->wide_id    = true;
      val->wide_stamp = true;
      val->micro_time = true;
      break;
    case 'V':
      version();
      exit(EXIT_SUCCESS);
    case 'h':
      exit_status = EXIT_SUCCESS;
    default:
      help(opts, pgn);
      exit(exit_status);
    }

    /* consider remaining arguments */

#ifndef DISABLE_EGGS
    q0(val->verbose);
#endif /* EGGS */
}

