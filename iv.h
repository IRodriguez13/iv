#ifndef IV_H
#define IV_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 4096
#define MAX_LEN   1024

/* Options (set by main from argv) */
typedef struct {
    int dry_run;
    int no_backup;
    int no_numbers;
    int global_replace;  /* -g for search/replace all matches */
} IvOpts;

/* Parse range string (e.g. "1-5", "-3--1", "-5-") into start,end (1-based).
 * count = total lines. Returns 0 on success, -1 on parse error. */
int parse_range(const char *spec, int count, int *start, int *end);

/* View */
void show_file(char *lines[], int count, int no_numbers);
void show_range(char *lines[], int count, int start, int end, int no_numbers);
int wc_lines(char *lines[], int count);

/* Edit */
void backup_file(const char *filename);
void write_with_escapes(FILE *f, const char *text);
int apply_patch(const char *filename, char *lines[], int count,
                int start, int end, const char *new_text, int mode,
                const IvOpts *opts);

/* Search/replace: returns number of replacements */
int search_replace(char *lines[], int count, const char *pattern,
                   const char *replacement, int global);

void write_lines_to_file(const char *filename, char *lines[], int count);

/* Read stdin into dynamically allocated string. Caller frees. */
char *read_stdin(void);

#endif
