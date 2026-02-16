#ifndef IV_H
#define IV_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define MAX_LINES 4096
#define MAX_LEN   1024

#define IV_VERSION "0.0.1"

/* Options (set by main from argv) */
typedef struct {
    int dry_run;
    int no_backup;
    int no_numbers;
    int global_replace;  /* -g for search/replace all matches */
    int use_regex;      /* -E for regex in search/replace */
    int quiet;          /* -q suppress tee-like output */
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

void write_lines_to_file(const char *filename, char *lines[], int count);

/* Read stdin into dynamically allocated string. Caller frees. */
char *read_stdin(void);

/* Read file into dynamically allocated string. Caller frees. Returns NULL on error. */
char *read_file_content(const char *path);

/* Returns 1 if file contains null bytes (binary), 0 otherwise */
int is_binary_file(const char *path);

/* Find line numbers where pattern appears. Prints to stdout, one per line. */
void find_line_numbers(char *lines[], int count, const char *pattern);

/* Stream file to stdout with line numbers. Returns 0 on success, -1 on error. */
int stream_file_with_numbers(const char *path);

/* List backups in backup dir. filter=NULL: all; filter="file": backups for that file */
void list_backups(const char *filter);

/* Remove backups. filter=NULL: all; filter="file": backup for that file only */
void clean_backups(const char *filter);

#endif
