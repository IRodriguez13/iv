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

#define IV_VERSION "0.1.1"

/* Options (set by main from argv) */
typedef struct {
    int dry_run;
    int no_backup;
    int no_numbers;
    int global_replace;  /* -g for search/replace all matches */
    int use_regex;      /* -E for regex in search/replace */
    int quiet;          /* -q suppress tee-like output */
    int to_stdout;      /* --stdout: write result to stdout, don't modify file */
    int json;           /* --json: structured output for -n */
    const char *multimatch;  /* -m: apply only to lines matching this pattern */
    char field_delim;   /* -F: field delimiter (0 = off) */
    int field_num;      /* -F: 1-based field number */
} IvOpts;

/* Parse range string (e.g. "1-5", "-3--1", "-5-") into start,end (1-based).
 * count = total lines. Returns 0 on success, -1 on parse error. */
int parse_range(const char *spec, int count, int *start, int *end);

/* View */
void show_file(char *lines[], int count, int no_numbers);
void show_range(char *lines[], int count, int start, int end, int no_numbers);
int wc_lines(char *lines[], int count);

/* Edit */
void get_backup_path(const char *filename, char *buf, size_t size);
void backup_file(const char *filename);
void write_with_escapes(FILE *f, const char *text);
int apply_patch(const char *filename, char *lines[], int count,
                int start, int end, const char *new_text, int mode,
                const IvOpts *opts);

/* Search/replace: returns number of replacements */
int search_replace(char *lines[], int count, const char *pattern,
                   const char *replacement, int global);

/* Search/replace with regex (-E). Returns number of replacements, -1 on regex error */
int search_replace_regex(char *lines[], int count, const char *pattern,
                         const char *replacement, int global);

/* Search/replace only on lines matching filter. filter=NULL = all lines. */
int search_replace_filtered(char *lines[], int count, const char *pattern,
                           const char *replacement, int global, const char *filter);
int search_replace_regex_filtered(char *lines[], int count, const char *pattern,
                                  const char *replacement, int global, const char *filter);

/* Replace field N (1-based) with value. delim=0 means no field mode. */
int replace_field(char *lines[], int count, char delim, int field_num, const char *value);

void write_lines_to_file(const char *filename, char *lines[], int count);
void write_lines_to_stream(FILE *f, char *lines[], int count);

/* Read stdin into dynamically allocated string. Caller frees. */
char *read_stdin(void);

/* Read file into dynamically allocated string. Caller frees. Returns NULL on error. */
char *read_file_content(const char *path);

/* Returns 1 if file contains null bytes (binary), 0 otherwise */
int is_binary_file(const char *path);

/* Find line numbers where pattern appears. json: 1 = output {"lines":[1,5,7]}. */
void find_line_numbers(char *lines[], int count, const char *pattern, int json);

/* Stream file to stdout with line numbers. Returns 0 on success, -1 on error. */
int stream_file_with_numbers(const char *path);

/* List backups in backup dir. filter=NULL: all; filter="file": backups for that file */
void list_backups(const char *filter);

/* Remove backups. filter=NULL: all; filter="file": backup for that file only */
void clean_backups(const char *filter);

#endif
