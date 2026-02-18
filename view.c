/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#include "iv.h"

void show_file(char *lines[], int count, int no_numbers) {
    for (int i = 0; i < count; i++) {
        if (no_numbers)
            printf("%s", lines[i]);
        else
            printf("%4d | %s", i+1, lines[i]);
    }
}

void show_range(char *lines[], int count, int start, int end, int no_numbers) {
    if (start < 1) start = 1;
    if (end > count) end = count;
    for (int i = start-1; i < end; i++) {
        if (no_numbers)
            printf("%s", lines[i]);
        else
            printf("%4d | %s", i+1, lines[i]);
    }
}

int wc_lines(char *lines[], int count) {
    return count;
}

void find_line_numbers(char *lines[], int count, const char *pattern, int json) {
    if (!pattern || !*pattern) return;
    if (json) printf("{\"lines\":[");
    int first = 1;
    for (int i = 0; i < count; i++) {
        if (strstr(lines[i], pattern)) {
            if (json) {
                if (!first) printf(",");
                printf("%d", i + 1);
                first = 0;
            } else {
                printf("%d\n", i + 1);
            }
        }
    }
    if (json) printf("]}\n");
}

int stream_file_with_numbers(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char *line = NULL;
    size_t cap = 0;
    int n = 0;
    while (getline(&line, &cap, f) != -1) {
        printf("%4d | %s", ++n, line);
    }
    free(line);
    fclose(f);
    return 0;
}
