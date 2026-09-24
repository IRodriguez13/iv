/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#ifndef IV_H
#define IV_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define IV_VERSION "0.11.1"

/* GNU Coreutils --backup methods (cp/mv/install). Default is none. */
enum {
    IV_BACKUP_NONE = 0,
    IV_BACKUP_SIMPLE,
    IV_BACKUP_NUMBERED,
    IV_BACKUP_EXISTING
};

/* Options (set by main from argv) */
typedef struct {
    int dry_run;
    int backup;             /* IV_BACKUP_*; none unless -b / --backup / -S */
    const char *backup_suffix; /* -S / --suffix; else SIMPLE_BACKUP_SUFFIX or ~ */
    int no_numbers;
    int global_replace;     /* -g: replace all matches per line */
    int use_regex;          /* -E: regex for substitute and -m */
    int quiet;              /* -q: suppress tee-like output */
    int to_stdout;          /* --stdout: write result to stdout, do not modify file */
    const char *multimatch; /* -m: apply only to matching lines */
    char field_delim;       /* -F: field delimiter (byte, not CSV quoting) */
    int field_num;          /* -F: field number (1-based) */
} IvOpts;

typedef int (*IvWriteFn)(FILE *out, void *ctx);

enum {
    IV_RANGE_FORWARD = 1,
    IV_RANGE_TAIL = 2,
    IV_RANGE_HYBRID = 3
};

typedef struct {
    int kind;
    int start;  /* FORWARD/HYBRID: 1-based inclusive */
    int end;    /* FORWARD: inclusive, -1 = through last line */
    int window; /* TAIL: last N lines kept in the ring */
    int from;   /* TAIL: first ring index to act on (0 = oldest) */
    int to;     /* TAIL: last ring index to act on */
    int hold;   /* HYBRID: last N lines stay outside the range */
} IvRangePlan;

/* Parse range specification ("1-5", "-3--1", "-5-", "5") into start,end
 * 1-based. count = total lines. Returns 0 on success, -1 on error. */
int parse_range(const char *spec, int count, int *start, int *end);

/* Classify a range without knowing the file length. */
int plan_range(const char *spec, IvRangePlan *p);

/* 1 if the spec needs the total line count (end-relative). */
int range_needs_total(const char *spec);

enum {
    IV_STREAM_VIEW = 0,
    IV_STREAM_DELETE = 2,
    IV_STREAM_REPLACE = 3,
    IV_STREAM_INSERT = 4,      /* text before each line in range */
    IV_STREAM_INSERT_ONCE = 5, /* text before the first line in range */
    IV_STREAM_APPEND = 6       /* copy all lines, then text at EOF */
};

/* Stream a planned range. VIEW writes to out; the rest transform.
 * text is used for REPLACE / INSERT / APPEND. */
int iv_stream_by_plan(FILE *in, FILE *out, const IvRangePlan *p, int op,
                      const char *text, int no_numbers);
int iv_stream_delete_match(FILE *in, FILE *out, const char *filter,
                           int use_regex);
int iv_stream_replace_match(FILE *in, FILE *out, const char *filter,
                            int use_regex, const char *text);

/* GNU --backup method names (unique abbreviations). -1 if unknown/ambiguous. */
int iv_parse_backup_method(const char *s);
/* VERSION_CONTROL, or existing if unset. -1 if VERSION_CONTROL is invalid. */
int iv_backup_from_env(void);
/* Copy path to a GNU backup name. No-op if backup is none or path is missing. */
int iv_backup_file(const char *path, const IvOpts *opts);

void write_with_escapes(FILE *f, const char *text);

/* Stream substitute/field transforms. nrepl receives replacement count.
 * Returns 0 on success, -1 on regex/I/O error. */
int iv_stream_subst(FILE *in, FILE *out, const char *(*pairs)[2],
                    int npairs, const IvOpts *opts, int *nrepl);
int iv_stream_fields(FILE *in, FILE *out, char delim, int field_num,
                     const char *value);

/* Transactional write: temp + fsync + rename. Original intact on failure.
 * write_fn: 0 = commit, >0 = discard temp (no-op success), <0 = fail. */
int iv_commit_stream(const char *path, IvWriteFn write_fn, void *ctx);
int iv_copy_file(const char *src, const char *dst);
void iv_init_stdio(void);
void iv_enlarge_buf(FILE *f);
int iv_fputs(FILE *out, const char *s);
int iv_fwrite(FILE *out, const void *p, size_t n);
int iv_out_status(FILE *out);
int iv_stdout_closed(void);
int iv_check_stream(FILE *f);

char *read_stdin(void);
char *read_file_content(const char *path);

int  stream_show_file(FILE *f, int no_numbers);

#endif
