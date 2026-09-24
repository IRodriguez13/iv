/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#include "iv.h"
#include <regex.h>

static int match_pat(const char *line, const char *pattern, regex_t *re)
{
    if (!pattern || !*pattern)
        return 0;
    if (re)
        return regexec(re, line, 0, NULL, 0) == 0;
    return strstr(line, pattern) != NULL;
}

static int compile_pat(regex_t *re, const char *pattern, int use_regex)
{
    if (!use_regex || !pattern || !*pattern)
        return 0;
    return regcomp(re, pattern, REG_EXTENDED | REG_NOSUB);
}

void show_file(char *lines[], int count, int no_numbers)
{
    for (int i = 0; i < count; i++)
    {
        if (no_numbers)
            printf("%s", lines[i]);
        else
            printf("%4d | %s", i + 1, lines[i]);
    }
}

void show_range(char *lines[], int count, int start, int end, int no_numbers)
{
    if (start < 1)
        start = 1;
    if (end > count)
        end = count;
    for (int i = start - 1; i < end; i++)
    {
        if (no_numbers)
            printf("%s", lines[i]);
        else
            printf("%4d | %s", i + 1, lines[i]);
    }
}

int wc_lines(char *lines[], int count)
{
    (void)lines;
    return count;
}

void find_line_numbers(char *lines[], int count, const char *pattern, int json,
                       int use_regex)
{
    regex_t re;
    regex_t *rp = NULL;
    int first = 1;

    if (!pattern || !*pattern)
        return;
    if (use_regex)
    {
        if (compile_pat(&re, pattern, 1) != 0)
        {
            fprintf(stderr, "iv: invalid regex pattern\n");
            return;
        }
        rp = &re;
    }
    if (json)
        printf("{\"lines\":[");
    for (int i = 0; i < count; i++)
    {
        if (!match_pat(lines[i], pattern, rp))
            continue;
        if (json)
        {
            if (!first)
                printf(",");
            printf("%d", i + 1);
            first = 0;
        }
        else
            printf("%d\n", i + 1);
    }
    if (json)
        printf("]}\n");
    if (rp)
        regfree(rp);
}

void find_matching_lines(char *lines[], int count, const char *pattern,
                         int no_numbers, int use_regex)
{
    regex_t re;
    regex_t *rp = NULL;

    if (!pattern || !*pattern)
        return;
    if (use_regex)
    {
        if (compile_pat(&re, pattern, 1) != 0)
        {
            fprintf(stderr, "iv: invalid regex pattern\n");
            return;
        }
        rp = &re;
    }
    for (int i = 0; i < count; i++)
    {
        if (!match_pat(lines[i], pattern, rp))
            continue;
        if (no_numbers)
            printf("%s", lines[i]);
        else
            printf("%4d | %s", i + 1, lines[i]);
    }
    if (rp)
        regfree(rp);
}

int stream_file_with_numbers(const char *path)
{
    FILE *f = fopen(path, "r");
    int rc;

    if (!f)
        return -1;
    rc = stream_show_file(f, 0);
    fclose(f);
    return rc;
}

int stream_show_file(FILE *f, int no_numbers)
{
    char *line = NULL;
    size_t cap = 0;
    int n = 0;

    while (getline(&line, &cap, f) != -1)
    {
        n++;
        if (no_numbers)
            fputs(line, stdout);
        else
            printf("%4d | %s", n, line);
    }
    free(line);
    return ferror(f) ? -1 : 0;
}

int stream_show_range(FILE *f, int start, int end, int no_numbers)
{
    char *line = NULL;
    size_t cap = 0;
    int n = 0;

    if (start < 1)
        start = 1;
    while (getline(&line, &cap, f) != -1)
    {
        n++;
        if (n < start)
            continue;
        if (n > end)
            break;
        if (no_numbers)
            fputs(line, stdout);
        else
            printf("%4d | %s", n, line);
    }
    free(line);
    return ferror(f) ? -1 : 0;
}

int stream_wc(FILE *f)
{
    char *line = NULL;
    size_t cap = 0;
    int n = 0;

    while (getline(&line, &cap, f) != -1)
        n++;
    free(line);
    if (ferror(f))
        return -1;
    printf("%d\n", n);
    return 0;
}

int stream_count_lines(FILE *f)
{
    char *line = NULL;
    size_t cap = 0;
    int n = 0;

    while (getline(&line, &cap, f) != -1)
        n++;
    free(line);
    return ferror(f) ? -1 : n;
}

int stream_find_line_numbers(FILE *f, const char *pattern, int json,
                             int use_regex)
{
    char *line = NULL;
    size_t cap = 0;
    regex_t re;
    regex_t *rp = NULL;
    int n = 0, first = 1;

    if (!pattern || !*pattern)
        return 0;
    if (use_regex)
    {
        if (compile_pat(&re, pattern, 1) != 0)
        {
            fprintf(stderr, "iv: invalid regex pattern\n");
            return -1;
        }
        rp = &re;
    }
    if (json)
        printf("{\"lines\":[");
    while (getline(&line, &cap, f) != -1)
    {
        n++;
        if (!match_pat(line, pattern, rp))
            continue;
        if (json)
        {
            if (!first)
                printf(",");
            printf("%d", n);
            first = 0;
        }
        else
            printf("%d\n", n);
    }
    if (json)
        printf("]}\n");
    free(line);
    if (rp)
        regfree(rp);
    return ferror(f) ? -1 : 0;
}

int stream_find_matching_lines(FILE *f, const char *pattern, int no_numbers,
                               int use_regex)
{
    char *line = NULL;
    size_t cap = 0;
    regex_t re;
    regex_t *rp = NULL;
    int n = 0;

    if (!pattern || !*pattern)
        return 0;
    if (use_regex)
    {
        if (compile_pat(&re, pattern, 1) != 0)
        {
            fprintf(stderr, "iv: invalid regex pattern\n");
            return -1;
        }
        rp = &re;
    }
    while (getline(&line, &cap, f) != -1)
    {
        n++;
        if (!match_pat(line, pattern, rp))
            continue;
        if (no_numbers)
            fputs(line, stdout);
        else
            printf("%4d | %s", n, line);
    }
    free(line);
    if (rp)
        regfree(rp);
    return ferror(f) ? -1 : 0;
}
