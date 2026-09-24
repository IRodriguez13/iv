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

#define INITIAL_LINES 256

/* Rotating backup cap: slot 1 is newest. Older than this are dropped. */
#define IV_BACKUP_SLOTS 10

#define IV_VERSION "0.11.0"

/* Options (set by main from argv) */
typedef struct {
    int dry_run;
    int no_backup;
    int no_numbers;
    int global_replace;     /* -g: replace all matches per line */
    int use_regex;          /* -E: regex for substitute and -m / -n / -nv */
    int quiet;              /* -q: suppress tee-like output */
    int to_stdout;          /* --stdout: write result to stdout, do not modify file */
    int json;               /* --json: structured output for -n */
    int persist;            /* --persist: move repo from /tmp to ~/.local/share/iv/ */
    int unpersist;          /* --unpersist: move repo from ~/.local/share/iv/ to /tmp */
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
    IV_STREAM_REPLACE = 3
};

/* Stream a planned range. VIEW writes to out; DELETE/REPLACE transform.
 * text is used for REPLACE (once per line in range). */
int iv_stream_by_plan(FILE *in, FILE *out, const IvRangePlan *p, int op,
                      const char *text, int no_numbers);
int iv_stream_delete_match(FILE *in, FILE *out, const char *filter,
                           int use_regex);
int iv_stream_replace_match(FILE *in, FILE *out, const char *filter,
                            int use_regex, const char *text);

/* Backup root directory depending on persistence:
 *   persisted → $XDG_DATA_HOME/iv  or  ~/.local/share/iv
 *   ephemeral → $IV_BACKUP_DIR     or  /tmp/iv_<user>
 * The returned string is static or from the environment; do not free it. */
const char *get_backup_root(int persisted);

/* Build the per-file subdirectory inside the backup root.
 * Format: <repo_name>%<sanitized_path>
 * E.g.: /home/ivan/myproject/src/main.c → "myproject%src%main.c"
 * buf should be at least 512 bytes. */
void get_backup_subdir(const char *filename, char *buf, size_t size);

/* Full path to the backup directory for filename.
 * If persisted=1 uses ~/.local/share/iv/, otherwise uses /tmp/iv_<user>/
 * Creates the directory if it does not exist. */
void get_backup_dir_for_file(const char *filename, int persisted,
                             char *buf, size_t size);

/* Full path to backup slot N for filename. */
void get_backup_path_n(const char *filename, int persisted, int n,
                       char *buf, size_t size);

/* Full path to the .meta for slot N. */
void get_backup_meta_path(const char *filename, int persisted, int n,
                          char *buf, size_t size);

/* Create a rotating backup (slot 1 newest, drop past IV_BACKUP_SLOTS).
 * Returns 0 on success or if the source does not exist; -1 on I/O error. */
int backup_file(const char *filename, int persisted);

/* Move a file's backup directory from /tmp to
 * ~/.local/share/iv/ (persist=1) or the other way around (persist=0).
 * Returns 0 on success, -1 on error. */
int transfer_backup_repo(const char *filename, int to_persist);

void write_with_escapes(FILE *f, const char *text);

int apply_patch(const char *filename, char *lines[], int count,
                int start, int end, const char *new_text, int mode,
                const IvOpts *opts);

int search_replace(char *lines[], int count, const char *pattern,
                   const char *replacement, int global);

int search_replace_regex(char *lines[], int count, const char *pattern,
                         const char *replacement, int global);

int search_replace_filtered(char *lines[], int count, const char *pattern,
                            const char *replacement, int global,
                            const char *filter, int filter_regex);

int search_replace_regex_filtered(char *lines[], int count, const char *pattern,
                                  const char *replacement, int global,
                                  const char *filter, int filter_regex);

int replace_field(char *lines[], int count, char delim, int field_num,
                  const char *value);

/* Stream substitute/field transforms. nrepl receives replacement count.
 * Returns 0 on success, -1 on regex/I/O error. */
int iv_stream_subst(FILE *in, FILE *out, const char *(*pairs)[2],
                    int npairs, const IvOpts *opts, int *nrepl);
int iv_stream_fields(FILE *in, FILE *out, char delim, int field_num,
                     const char *value);

/* Transactional write: temp + fsync + rename. Original intact on failure.
 * write_fn: 0 = commit, >0 = discard temp (no-op success), <0 = fail. */
int iv_commit_stream(const char *path, IvWriteFn write_fn, void *ctx);
int iv_commit_lines(const char *path, char *lines[], int count);
int iv_copy_file(const char *src, const char *dst);
int iv_restore_file(const char *src, const char *dst);
int iv_check_stream(FILE *f);

int write_lines_to_file(const char *filename, char *lines[], int count);
int write_lines_to_stream(FILE *f, char *lines[], int count);

char *read_stdin(void);
char *read_file_content(const char *path);
int   is_binary_file(const char *path);

void show_file(char *lines[], int count, int no_numbers);
void show_range(char *lines[], int count, int start, int end, int no_numbers);
int  wc_lines(char *lines[], int count);
void find_line_numbers(char *lines[], int count, const char *pattern, int json,
                       int use_regex);
void find_matching_lines(char *lines[], int count, const char *pattern,
                         int no_numbers, int use_regex);
int  stream_file_with_numbers(const char *path);
int  stream_show_file(FILE *f, int no_numbers);
int  stream_show_range(FILE *f, int start, int end, int no_numbers);
int  stream_wc(FILE *f);
int  stream_find_line_numbers(FILE *f, const char *pattern, int json,
                              int use_regex);
int  stream_find_matching_lines(FILE *f, const char *pattern, int no_numbers,
                                int use_regex);
int  stream_count_lines(FILE *f);

/* List backups in the given root. filter=NULL: all; filter="file": only that one. */
void list_backups(const char *filter, int persisted);
void list_backups_with_meta(const char *filter, int persisted);

/* Show metadata (stderr) and content (stdout) of slot N. */
int show_backup_slot(const char *filename, int persisted, int n);

/* Remove backups. filter=NULL: all; filter="file": only those for that file. */
void clean_backups(const char *filter, int persisted);

#endif
