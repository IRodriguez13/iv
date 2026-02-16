#include "iv.h"

void backup_file(const char *filename) {
    char bakname[512];
    snprintf(bakname, sizeof(bakname), "%s.bak", filename);
    FILE *src = fopen(filename, "r");
    if (!src) return;
    FILE *dst = fopen(bakname, "w");
    if (!dst) { fclose(src); return; }
    char buffer[MAX_LEN];
    while (fgets(buffer, sizeof(buffer), src)) {
        fputs(buffer, dst);
    }
    fclose(src);
    fclose(dst);
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
    int do_backup = !opts->no_backup;
    int dry = opts->dry_run;

    if (do_backup && !dry)
        backup_file(filename);

    FILE *f = dry ? NULL : fopen(filename, "w");
    if (!dry && !f) { perror("Could not write file"); return -1; }

    int wrote_new = 0;

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

    if (f) fclose(f);
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

void write_lines_to_file(const char *filename, char *lines[], int count) {
    FILE *f = fopen(filename, "w");
    if (!f) { perror("Could not write file"); return; }
    for (int i = 0; i < count; i++)
        fputs(lines[i], f);
    fclose(f);
}
