/* File: main.c
   Time-stamp: <2012-02-26 21:57:36 gawen>

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

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <sysexits.h>
#include <errno.h>
#include <time.h>
#include <err.h>

#include "sar.h"
#include "common.h"

enum mode { MD_NONE = 0,
            MD_INFORMATION,
            MD_CREATE,
            MD_EXTRACT,
            MD_LIST };

struct opts_name {
  char name_short;
  const char *name_long;
  const char *help;
};

struct opts_val {
  unsigned int verbose;
  enum mode mode;

  bool use_file;
  bool no_crc;
  bool no_nano;

  char *cwd;           /* original cwd */
  const char *tmp_cwd; /* new cwd */
  const char *compress;
  const char *file;
  const char *source;
};

static struct opts_val *opt_value;

static void clean_exit()
{
  /* revert to original working directory */
  if(opt_value->cwd) {
    xchdir(opt_value->cwd);
    free(opt_value->cwd);
  }
}

static void version()
{
  printf(PACKAGE " " PACKAGE_VERSION "\n");
  exit(EXIT_SUCCESS);
}

static void help(const struct opts_name *names, const char *prog_name)
{
  const struct opts_name *opt;
  int size;
  int max = 0;

  fprintf(stderr, "Usage: %s [OPTIONS] [ARCHIVE] [FILES]\n", prog_name);

  /* maximum option name size for padding */
  for(opt = names ; opt->name_long ; opt++) {
    size = strlen(opt->name_long);
    if(size > max)
      max = size;
  }

  /* print options and help messages */
  for(opt = names ; opt->name_long ; opt++) {
    if(opt->name_short != 0)
      fprintf(stderr, "  -%c, --%s", opt->name_short, opt->name_long);
    else
      fprintf(stderr, "      --%s", opt->name_long);

    /* padding */
    size = strlen(opt->name_long);
    for(; size <= max ; size++)
      fputc(' ', stderr);
    fprintf(stderr, "%s\n", opt->help);
  }
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
  enum opt { OPT_COMPRESS,
             OPT_LZMA,
             OPT_LZIP,
             OPT_LZOP,
#ifdef COMMIT
             OPT_COMMIT,
#endif /* COMMIT */
             OPT_DIRECTORY   = 'd',
             OPT_LZW         = 'Z',
             OPT_GZIP        = 'z',
             OPT_BZIP2       = 'j',
             OPT_XZ          = 'J',
             OPT_VERSION     = 'V',
             OPT_HELP        = 'h',
             OPT_VERBOSE     = 'v',
             OPT_INFORMATION = 'i',
             OPT_CREATE      = 'c',
             OPT_EXTRACT     = 'x',
             OPT_LIST        = 't',
             OPT_FILE        = 'f',
             OPT_NO_CRC      = 'C',
             OPT_NO_NANO     = 'N' };

  struct opts_name names[] = {
    { 'V', "version",     "Print version information" },
#ifdef COMMIT
    { 0  , "commit",      "Display commit information" },
#endif /* COMMIT */
    { 'h', "help",        "Print this message" },
    { 'v', "verbose",     "Be verbose (may be used multiple times)" },
    { 'd', "directory",   "Change to directory DIR" },
    { 0  , "compress",    "Compress using the specified executable" },
    { 'Z', "lzw",         "Alias for '--compress compress'" },
    { 'z', "gzip",        "Alias for '--compress gzip'" },
    { 'j', "bzip2",       "Alias for '--compress bzip2'" },
    { 'J', "xz",          "Alias for '--compress xz'" },
    { 0  , "lzma",        "Alias for '--compress lzma'" },
    { 0  , "lzip",        "Alias for '--compress lzip'" },
    { 0  , "lzop",        "Alias for '--compress lzop'" },
    { 'i', "information", "Display basic informations about an archive" },
    { 'c', "create",      "Create a new archive" },
    { 'x', "extract",     "Extract all files from an archive" },
    { 't',  "list",       "List all files in an archive" },
    { 'f',  "file",       "Use a file instead of standard input/output" },
    { 'C',  "no-crc",     "Disable integrity checks" },
    { 'N',  "no-nano",    "Disable timestamps precision (upto nanoseconds)" },
    { 0, NULL, NULL }
  };

  struct option opts[] = {
    { "version", no_argument, NULL, OPT_VERSION },
#ifdef COMMIT
    { "commit", no_argument, NULL, OPT_COMMIT },
#endif /* COMMIT */
    { "help", no_argument, NULL, OPT_HELP },
    { "verbose", no_argument, NULL, OPT_VERBOSE },
    { "directory", required_argument, NULL, OPT_DIRECTORY },
    { "compress", required_argument, NULL, OPT_COMPRESS },
    { "lzw", no_argument, NULL, OPT_LZW },
    { "gzip", no_argument, NULL, OPT_GZIP },
    { "bzip2", no_argument, NULL, OPT_BZIP2 },
    { "xz", no_argument, NULL, OPT_XZ },
    { "lzma", no_argument, NULL, OPT_LZMA },
    { "lzip", no_argument, NULL, OPT_LZIP },
    { "lzop", no_argument, NULL, OPT_LZOP },
    { "information", no_argument, NULL, OPT_INFORMATION },
    { "create", no_argument, NULL, OPT_CREATE },
    { "extract", no_argument, NULL, OPT_EXTRACT },
    { "list", no_argument, NULL, OPT_LIST },
    { "file", no_argument, NULL, OPT_FILE },
    { "no-crc", no_argument, NULL, OPT_NO_CRC },
    { "no-nano", no_argument, NULL, OPT_NO_NANO },
    { NULL, 0, NULL, 0 }
  };

  /* retrieve program's name */
  const char *pgn = (const char *)strrchr(argv[0], '/');
  pgn = pgn ? (pgn + 1) : argv[0];

  while(1) {
    int c = getopt_long(argc, argv, "Vhvd:ZzjJicxtfCN", opts, NULL);

    if(c == -1)
      break;

    switch(c) {
    case OPT_VERBOSE:
      val->verbose++;
      break;
    case OPT_COMPRESS:
      val->compress = optarg;
      break;
    case OPT_DIRECTORY:
      val->cwd     = xmalloc(PATH_MAX);
      val->cwd     = xgetcwd(val->cwd, PATH_MAX);
      val->tmp_cwd = optarg;
      break;
    case OPT_GZIP:
      val->compress = "gzip";
      break;
    case OPT_BZIP2:
      val->compress = "bzip2";
      break;
    case OPT_XZ:
      val->compress = "xz";
      break;
    case OPT_LZMA:
      val->compress = "lzma";
      break;
    case OPT_LZIP:
      val->compress = "lzip";
      break;
    case OPT_LZOP:
      val->compress = "lzop";
      break;
    case OPT_LZW:
      val->compress = "compress";
      break;
    case OPT_INFORMATION:
      val->mode = MD_INFORMATION;
      break;
    case OPT_CREATE:
      val->mode = MD_CREATE;
      break;
    case OPT_EXTRACT:
      val->mode = MD_EXTRACT;
      break;
    case OPT_LIST:
      val->mode = MD_LIST;
      val->verbose++;
      break;
    case OPT_FILE:
      val->use_file = true;
      break;
    case OPT_NO_CRC:
      val->no_crc = true;
      break;
    case OPT_NO_NANO:
      val->no_nano    = true;
      break;
#ifdef COMMIT
    case OPT_COMMIT:
      printf("Commit-Id SHA1 : " COMMIT "\n");
      exit(EXIT_SUCCESS);
#endif /* COMMIT */
    case OPT_VERSION:
      version();
      exit(EXIT_SUCCESS);
    case OPT_HELP:
      exit_status = EXIT_SUCCESS;
    default:
      help(names, pgn);
      exit(exit_status);
    }
  }

  /* consider remaining arguments */
  switch(val->mode) {
  case(MD_NONE):
    errx(EXIT_SUCCESS, "You must specify one of the 'cxti' options\n"
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

  if((val->no_crc || val->no_nano) &&
     !(val->mode == MD_CREATE))
    errx(EXIT_FAILURE, "Options 'CN' are only availables with 'c' option\n"
         "Try '%s --help'", pgn);
}

int main(int argc, char *argv[])
{
  struct opts_val val = {0};
  struct sar_file *f  = NULL;

  opt_value = &val;
  atexit(clean_exit);

  cmdline(argc, argv, &val);

  switch(val.mode) {
  case(MD_NONE):
    break;
  case(MD_INFORMATION):
    f = sar_read(val.file, val.compress, val.verbose);
    sar_info(f);
    break;
  case(MD_CREATE):
    f = sar_creat(val.file,
                  val.compress,
                  !val.no_crc,
                  !val.no_nano,
                  val.verbose);
    if(val.tmp_cwd)
      xchdir(val.tmp_cwd);
    sar_add(f, val.source);
    break;
  case(MD_EXTRACT):
    f = sar_read(val.file, val.compress, val.verbose);
    if(val.tmp_cwd)
      xchdir(val.tmp_cwd);
    sar_extract(f);
    break;
  case(MD_LIST):
    f = sar_read(val.file, val.compress, val.verbose);
    if(val.tmp_cwd)
      xchdir(val.tmp_cwd);
    sar_list(f);
    break;
  }

  if(val.mode != MD_NONE)
    sar_close(f);

  exit(EXIT_SUCCESS);
}
