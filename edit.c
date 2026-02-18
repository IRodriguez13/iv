/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#include "iv.h"
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <regex.h>
#include <time.h>
#include <unistd.h>
#include <pwd.h>

static const char *get_backup_dir(void) {
    const char *d = getenv("IV_BACKUP_DIR");
    return d && *d ? d : "/tmp";
}

/* Write backup base name (e.g. "iv_filename") into buf; no directory, no .N.bak */
static void get_backup_base(const char *filename, char *buf, size_t size) {
    const char *p = filename;
    while (*p == '.' && p[1] == '/') p += 2;
    while (*p == '/') p++;
    char name[256];
    size_t j = 0;
    for (; *p && j < sizeof(name) - 1; p++)
        name[j++] = (*p == '/') ? '_' : *p;
    name[j] = '\0';
    if (j == 0) name[0] = 'f', name[1] = '\0';
    snprintf(buf, size, "iv_%s", name);
}

void get_backup_path(const char *filename, char *buf, size_t size) {
    char base[280];
    get_backup_base(filename, base, sizeof(base));
    snprintf(buf, size, "%s/%s.1.bak", get_backup_dir(), base);
}

void get_backup_path_n(const char *filename, int n, char *buf, size_t size) {
    char base[280];
    get_backup_base(filename, base, sizeof(base));
    snprintf(buf, size, "%s/%s.%d.bak", get_backup_dir(), base, n);
}

void get_backup_meta_path(const char *filename, int n, char *buf, size_t size) {
    char base[280];
    get_backup_base(filename, base, sizeof(base));
    snprintf(buf, size, "%s/%s.%d.meta", get_backup_dir(), base, n);
}

static const char *get_username(void) {
    const char *u = getenv("USER");
    if (u && *u) return u;
    {
        struct passwd *pw = getpwuid(getuid());
        if (pw && pw->pw_name) return pw->pw_name;
    }
    return "?";
}

void backup_file(const char *filename) {
    const char *dir = get_backup_dir();
    char base[280];
    get_backup_base(filename, base, sizeof(base));
    char path[512];
    /* Rotate .bak and .meta: .(k) -> .(k+1) */
    for (int k = MAX_BACKUPS - 1; k >= 1; k--) {
        snprintf(path, sizeof(path), "%s/%s.%d.bak", dir, base, k);
        char path_next[512];
        snprintf(path_next, sizeof(path_next), "%s/%s.%d.bak", dir, base, k + 1);
        (void) rename(path, path_next);
        snprintf(path, sizeof(path), "%s/%s.%d.meta", dir, base, k);
        snprintf(path_next, sizeof(path_next), "%s/%s.%d.meta", dir, base, k + 1);
        (void) rename(path, path_next);
    }
    FILE *src = fopen(filename, "r");
    if (!src) return;
    snprintf(path, sizeof(path), "%s/%s.1.bak", dir, base);
    FILE *dst = fopen(path, "w");
    if (!dst) { fclose(src); return; }
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, src) != -1)
        fputs(line, dst);
    free(line);
    fclose(src);
    fclose(dst);
    /* Write metadata: epoch and username */
    snprintf(path, sizeof(path), "%s/%s.1.meta", dir, base);
    FILE *meta = fopen(path, "w");
    if (meta) {
        fprintf(meta, "%ld %s\n", (long)time(NULL), get_username());
        fclose(meta);
    }
}

void write_with_escapes(FILE *f, const char *text) {
    for (const char *p = text; *p; p++) {
        if (*p == '\\' && *(p+1)) {
            p++;
            switch (*p) {
                case 'n':  fputc('\n', f); break;
                case 't':  fputc('\t', f); break;
                case '\\': fputc('\\', f); break;
                case 'r':  fputc('\r', f); break;
                default:   fputc('\\', f); fputc(*p, f); break;
            }
        } else {
            fputc(*p, f);
        }
    }
    fputc('\n', f);
}

int apply_patch(const char *filename, char *lines[], int count,
                int start, int end, const char *new_text, int mode,
                const IvOpts *opts) {
    int do_backup = !opts->no_backup && !opts->to_stdout;
    int dry = opts->dry_run;

    if (do_backup && !dry)
        backup_file(filename);

    FILE *f;
    if (dry) f = NULL;
    else if (opts->to_stdout) f = stdout;
    else { f = fopen(filename, "w"); if (!f) { perror("Could not write file"); return -1; } }

    int wrote_new = 0;

    /* mode 4: patch insert — insert new line before start, shift line and rest down */
    if (mode == 4) {
        for (int i = 0; i < count; i++) {
            if (i + 1 == start) {
                if (f) write_with_escapes(f, new_text);
                wrote_new = 1;
                if (f) fputs(lines[i], f);
            } else {
                if (f) fputs(lines[i], f);
            }
        }
        if (start > count || count == 0) {
            if (f) write_with_escapes(f, new_text);
            wrote_new = 1;
        }
        if (f && f != stdout) fclose(f);
        return wrote_new ? 0 : -1;
    }

    for (int i = 0; i < count; i++) {
        if (i+1 >= start && i+1 <= end) {
            if (mode == 2) continue;
            if (mode == 3) {
                if (f) write_with_escapes(f, new_text);
                wrote_new = 1;
            } else if (mode == 1) {
                if (f) write_with_escapes(f, new_text);
                if (f) fputs(lines[i], f);
                wrote_new = 1;
            }
        } else {
            if (f) fputs(lines[i], f);
        }
    }

    /* Append when insert at end, or when file is empty */
    if ((mode == 1 || mode == 3) && (start > count || count == 0)) {
        if (f) write_with_escapes(f, new_text);
        wrote_new = 1;
    }

    if (f && f != stdout) fclose(f);
    return wrote_new ? 0 : -1;
}

static char *replace_in_string(const char *line, const char *pat,
                               const char *repl, int global, int *n) {
    size_t plen = strlen(pat);
    size_t rlen = strlen(repl);
    size_t cap = strlen(line) + 256;
    char *out = malloc(cap);
    if (!out) return NULL;
    out[0] = '\0';
    size_t len = 0;
    const char *cur = line;
    *n = 0;
    while (*cur) {
        const char *p = strstr(cur, pat);
        if (!p) {
            while (*cur) {
                if (len + 1 >= cap) {
                    cap *= 2;
                    char *tmp = realloc(out, cap);
                    if (!tmp) { free(out); return NULL; }
                    out = tmp;
                }
                out[len++] = *cur++;
            }
            break;
        }
        if (len + (p - cur) + rlen + 1 >= cap) {
            cap = len + (p - cur) + rlen + 256;
            char *tmp = realloc(out, cap);
            if (!tmp) { free(out); return NULL; }
            out = tmp;
        }
        memcpy(out + len, cur, p - cur);
        len += p - cur;
        memcpy(out + len, repl, rlen);
        len += rlen;
        cur = p + plen;
        (*n)++;
        if (!global) {
            while (*cur) {
                if (len + 1 >= cap) { cap *= 2; char *t = realloc(out, cap); if (!t) { free(out); return NULL; } out = t; }
                out[len++] = *cur++;
            }
            break;
        }
    }
    out[len] = '\0';
    return out;
}

int search_replace(char *lines[], int count, const char *pattern,
                   const char *replacement, int global) {
    if (!pattern || !*pattern) return 0;
    int total = 0;
    for (int i = 0; i < count; i++) {
        int n;
        char *new_line = replace_in_string(lines[i], pattern, replacement, global, &n);
        if (new_line && n > 0) {
            free(lines[i]);
            lines[i] = new_line;
            total += n;
        } else if (new_line) {
            free(new_line);
        }
    }
    return total;
}

static char *replace_regex_in_string(const char *line, regex_t *re,
        const char *repl, int global, int *n) {
    size_t rlen = strlen(repl);
    size_t cap = strlen(line) + 256;
    char *out = malloc(cap);
    if (!out) return NULL;
    out[0] = '\0';
    size_t len = 0;
    const char *cur = line;
    regmatch_t m;
    *n = 0;
    while (regexec(re, cur, 1, &m, 0) == 0) {
        size_t before = m.rm_so;
        if (len + before + rlen + 1 >= cap) {
            cap = len + before + rlen + 256;
            char *tmp = realloc(out, cap);
            if (!tmp) { free(out); return NULL; }
            out = tmp;
        }
        memcpy(out + len, cur, before);
        len += before;
        memcpy(out + len, repl, rlen);
        len += rlen;
        cur += m.rm_eo;
        (*n)++;
        if (!global) {
            size_t rest = strlen(cur);
            if (len + rest + 1 >= cap) { cap = len + rest + 256; char *t = realloc(out, cap); if (!t) { free(out); return NULL; } out = t; }
            memcpy(out + len, cur, rest + 1);
            len += rest;
            break;
        }
    }
    if (*n == 0) {
        strcpy(out, line);
        len = strlen(line);
    }
    out[len] = '\0';
    return out;
}

int search_replace_regex(char *lines[], int count, const char *pattern,
                         const char *replacement, int global) {
    if (!pattern || !*pattern) return 0;
    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED) != 0) return -1;
    int total = 0;
    for (int i = 0; i < count; i++) {
        int n;
        char *new_line = replace_regex_in_string(lines[i], &re, replacement, global, &n);
        if (new_line && n > 0) {
            free(lines[i]);
            lines[i] = new_line;
            total += n;
        } else if (new_line) {
            free(new_line);
        }
    }
    regfree(&re);
    return total;
}

int search_replace_filtered(char *lines[], int count, const char *pattern,
                            const char *replacement, int global, const char *filter) {
    if (!pattern || !*pattern) return 0;
    int total = 0;
    for (int i = 0; i < count; i++) {
        if (filter && !strstr(lines[i], filter)) continue;
        int n;
        char *new_line = replace_in_string(lines[i], pattern, replacement, global, &n);
        if (new_line && n > 0) {
            free(lines[i]);
            lines[i] = new_line;
            total += n;
        } else if (new_line) {
            free(new_line);
        }
    }
    return total;
}

int search_replace_regex_filtered(char *lines[], int count, const char *pattern,
                                   const char *replacement, int global, const char *filter) {
    if (!pattern || !*pattern) return 0;
    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED) != 0) return -1;
    int total = 0;
    for (int i = 0; i < count; i++) {
        if (filter && !strstr(lines[i], filter)) continue;
        int n;
        char *new_line = replace_regex_in_string(lines[i], &re, replacement, global, &n);
        if (new_line && n > 0) {
            free(lines[i]);
            lines[i] = new_line;
            total += n;
        } else if (new_line) {
            free(new_line);
        }
    }
    regfree(&re);
    return total;
}

static char *replace_field_in_line(const char *line, char delim, int field_num, const char *value) {
    size_t vlen = strlen(value);
    size_t linelen = strlen(line);
    char *out = malloc(linelen + vlen + 64);
    if (!out) return NULL;
    const char *p = line, *field_start = line;
    int f = 1;
    while (f < field_num && *p) {
        if (*p == delim) { f++; p++; field_start = p; }
        else p++;
    }
    if (f != field_num) {
        strcpy(out, line);
        return out;
    }
    size_t len = field_start - line;
    memcpy(out, line, len);
    memcpy(out + len, value, vlen + 1);
    len += vlen;
    while (*p && *p != delim && *p != '\n') p++;
    strcpy(out + len, p);
    return out;
}

int replace_field(char *lines[], int count, char delim, int field_num, const char *value) {
    if (!delim || field_num < 1) return 0;
    for (int i = 0; i < count; i++) {
        char *new_line = replace_field_in_line(lines[i], delim, field_num, value);
        if (new_line) {
            free(lines[i]);
            lines[i] = new_line;
        }
    }
    return count;
}

void write_lines_to_file(const char *filename, char *lines[], int count) {
    FILE *f = fopen(filename, "w");
    if (!f) { perror("Could not write file"); return; }
    for (int i = 0; i < count; i++)
        fputs(lines[i], f);
    fclose(f);
}

void write_lines_to_stream(FILE *f, char *lines[], int count) {
    for (int i = 0; i < count; i++)
        fputs(lines[i], f);
}

/* Read .meta file (path_bak -> path_meta), fill out_ts and out_user; return 0 on success */
static int read_backup_meta(const char *path_bak, time_t *out_ts, char *out_user, size_t user_size) {
    size_t len = strlen(path_bak);
    if (len < 5 || strcmp(path_bak + len - 4, ".bak") != 0) return -1;
    char path_meta[512];
    snprintf(path_meta, sizeof(path_meta), "%.*smeta", (int)(len - 3), path_bak);
    FILE *f = fopen(path_meta, "r");
    if (!f) return -1;
    long epoch = 0;
    int n = fscanf(f, "%ld %255s", &epoch, out_user);
    fclose(f);
    if (n >= 1) { *out_ts = (time_t)epoch; if (n < 2) out_user[0] = '\0'; return 0; }
    return -1;
}

void list_backups(const char *filter) {
    const char *dir = get_backup_dir();
    DIR *d = opendir(dir);
    if (!d) { perror(dir); return; }
    char base[280];
    if (filter && *filter)
        get_backup_base(filter, base, sizeof(base));
    size_t base_len = filter && *filter ? strlen(base) : 0;
    char path[512];
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || strncmp(e->d_name, "iv_", 3) != 0) continue;
        size_t len = strlen(e->d_name);
        if (len < 5 || strcmp(e->d_name + len - 4, ".bak") != 0) continue;
        if (base_len && (strncmp(e->d_name, base, base_len) != 0 || e->d_name[base_len] != '.'))
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) == 0)
            printf("%s  %zu bytes\n", path, (size_t)st.st_size);
    }
    closedir(d);
}

void list_backups_with_meta(const char *filter) {
    const char *dir = get_backup_dir();
    DIR *d = opendir(dir);
    if (!d) { perror(dir); return; }
    char base[280];
    if (filter && *filter)
        get_backup_base(filter, base, sizeof(base));
    size_t base_len = filter && *filter ? strlen(base) : 0;
    char path[512];
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || strncmp(e->d_name, "iv_", 3) != 0) continue;
        size_t len = strlen(e->d_name);
        if (len < 5 || strcmp(e->d_name + len - 4, ".bak") != 0) continue;
        if (base_len && (strncmp(e->d_name, base, base_len) != 0 || e->d_name[base_len] != '.'))
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        time_t ts = 0;
        char user[256] = "";
        int has_meta = (read_backup_meta(path, &ts, user, sizeof(user)) == 0);
        printf("%s  %zu bytes", path, (size_t)st.st_size);
        if (has_meta) {
            char buf[64];
            struct tm *tm = localtime(&ts);
            if (tm && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm) > 0)
                printf("  %s  %s", buf, user[0] ? user : "?");
        }
        printf("\n");
    }
    closedir(d);
}

/* Print metadata and content of backup slot N for file; return 0 on success */
int show_backup_slot(const char *filename, int n) {
    char path_bak[512], path_meta[512];
    get_backup_path_n(filename, n, path_bak, sizeof(path_bak));
    get_backup_meta_path(filename, n, path_meta, sizeof(path_meta));
    FILE *f = fopen(path_bak, "r");
    if (!f) { fprintf(stderr, "iv: no backup %d found for %s\n", n, filename); return -1; }
    time_t ts = 0;
    char user[256] = "";
    if (read_backup_meta(path_bak, &ts, user, sizeof(user)) == 0) {
        char buf[64];
        struct tm *tm = localtime(&ts);
        if (tm && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm) > 0)
            fprintf(stderr, "# backup %d  %s  user: %s\n", n, buf, user[0] ? user : "?");
    }
    char line[4096];
    while (fgets(line, sizeof(line), f))
        fputs(line, stdout);
    fclose(f);
    return 0;
}

void clean_backups(const char *filter) {
    const char *dir = get_backup_dir();
    DIR *d = opendir(dir);
    if (!d) { perror(dir); return; }
    char base[280];
    if (filter && *filter)
        get_backup_base(filter, base, sizeof(base));
    size_t base_len = filter && *filter ? strlen(base) : 0;
    char path[512];
    struct dirent *e;
    int removed = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || strncmp(e->d_name, "iv_", 3) != 0) continue;
        size_t len = strlen(e->d_name);
        if (len < 5 || strcmp(e->d_name + len - 4, ".bak") != 0) continue;
        if (base_len && (strncmp(e->d_name, base, base_len) != 0 || e->d_name[base_len] != '.'))
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (remove(path) == 0) removed++;
        /* Remove corresponding .meta */
        size_t plen = strlen(path);
        if (plen >= 4 && strcmp(path + plen - 4, ".bak") == 0) {
            char path_meta[512];
            snprintf(path_meta, sizeof(path_meta), "%.*smeta", (int)(plen - 3), path);
            (void) remove(path_meta);
        }
    }
    closedir(d);
    if (removed > 0)
        fprintf(stderr, "iv: removed %d backup(s)\n", removed);
}
