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

/* Number of backup slots: there is no fixed limit in the logic; this value is
 * only used by rotation to decide how many slots to shift up.
 * The user can have as many as the filesystem allows. */
#define IV_BACKUP_SLOTS 10

#define IV_VERSION "0.10.0"

/* Options (set by main from argv) */
typedef struct {
    int dry_run;
    int no_backup;
    int no_numbers;
    int global_replace;     /* -g: replace all matches per line */
    int use_regex;          /* -E: use regex in search/replace */
    int quiet;              /* -q: suppress tee-like output */
    int to_stdout;          /* --stdout: write result to stdout, do not modify file */
    int json;               /* --json: structured output for -n */
    int persist;            /* --persist: move repo from /tmp to ~/.local/share/iv/ */
    int unpersist;          /* --unpersist: move repo from ~/.local/share/iv/ to /tmp */
    const char *multimatch; /* -m: apply only to lines that contain this pattern */
    char field_delim;       /* -F: field delimiter */
    int field_num;          /* -F: field number (1-based) */
} IvOpts;


/* Parse range specification ("1-5", "-3--1", "-5-", "5") into start,end
 * 1-based. count = total lines. Returns 0 on success, -1 on error. */
int parse_range(const char *spec, int count, int *start, int *end);


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


/* Create a rotating backup for the file (slot 1, shift older ones).
 * If persisted=1 save in ~/.local/share/iv/, otherwise in /tmp. */
void backup_file(const char *filename, int persisted);

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
                            const char *filter);

int search_replace_regex_filtered(char *lines[], int count, const char *pattern,
                                  const char *replacement, int global,
                                  const char *filter);

int replace_field(char *lines[], int count, char delim, int field_num,
                  const char *value);


void write_lines_to_file(const char *filename, char *lines[], int count);
void write_lines_to_stream(FILE *f, char *lines[], int count);

char *read_stdin(void);
char *read_file_content(const char *path);
int   is_binary_file(const char *path);


void show_file(char *lines[], int count, int no_numbers);
void show_range(char *lines[], int count, int start, int end, int no_numbers);
int  wc_lines(char *lines[], int count);
void find_line_numbers(char *lines[], int count, const char *pattern, int json);
int  stream_file_with_numbers(const char *path);


/* List backups in the given root. filter=NULL: all; filter="file": only that one. */
void list_backups(const char *filter, int persisted);
void list_backups_with_meta(const char *filter, int persisted);

/* Show metadata (stderr) and content (stdout) of slot N. */
int show_backup_slot(const char *filename, int persisted, int n);

/* Remove backups. filter=NULL: all; filter="file": only those for that file. */
void clean_backups(const char *filter, int persisted);

#endif