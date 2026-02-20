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
#include <errno.h>
#include <limits.h>

/* ── Internal utilities ─────────────────────────────────────────────────── */

static const char *get_username(void)
{
    const char *u = getenv("USER");
    if (u && *u)
        return u;
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name)
        return pw->pw_name;
    return "unknown";
}

/* Create a directory and all missing parent directories (mkdir -p). */
static int mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '/')
        tmp[--len] = '\0';
    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/* Copy a file src → dst. Returns 0 on success. */
static int copy_file(const char *src, const char *dst)
{
    FILE *fsrc = fopen(src, "rb");
    if (!fsrc)
        return -1;
    FILE *fdst = fopen(dst, "wb");
    if (!fdst)
    {
        fclose(fsrc);
        return -1;
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fsrc)) > 0)
        fwrite(buf, 1, n, fdst);
    fclose(fsrc);
    fclose(fdst);
    return 0;
}

/* Move src → dst, trying rename first (same filesystem),
 * then copy+unlink if crossing filesystems. */
static int move_file(const char *src, const char *dst)
{
    if (rename(src, dst) == 0)
        return 0;
    if (errno != EXDEV)
        return -1;
    if (copy_file(src, dst) != 0)
        return -1;
    return unlink(src);
}

static int join_path2(char *dst, size_t dstsz, const char *a, const char *b)
{
    size_t alen = a ? strlen(a) : 0;
    size_t blen = b ? strlen(b) : 0;
    if (alen + 1 + blen + 1 > dstsz)
        return -1;
    if (alen)
        memcpy(dst, a, alen);
    dst[alen] = '/';
    if (blen)
        memcpy(dst + alen + 1, b, blen);
    dst[alen + 1 + blen] = '\0';
    return 0;
}

static int join_path_num(char *dst, size_t dstsz, const char *a,
                         int n, const char *suffix)
{
    char tail[64];
    int w;
    if (!suffix)
        suffix = "";
    w = snprintf(tail, sizeof(tail), "%d%s", n, suffix);
    if (w < 0 || (size_t)w >= sizeof(tail))
        return -1;
    return join_path2(dst, dstsz, a, tail);
}

/* ── Backup root ────────────────────────────────────────────────────────── */

const char *get_backup_root(int persisted)
{
    if (persisted)
    {
        const char *xdg = getenv("XDG_DATA_HOME");
        if (xdg && *xdg)
        {
            static char buf[PATH_MAX];
            if (join_path2(buf, sizeof(buf), xdg, "iv") != 0)
            {
                if (sizeof(buf))
                    buf[0] = '\0';
                return buf;
            }
            return buf;
        }
        const char *home = getenv("HOME");
        if (!home)
        {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }
        static char buf[PATH_MAX];
        if (join_path2(buf, sizeof(buf), home, ".local/share/iv") != 0)
        {
            if (sizeof(buf))
                buf[0] = '\0';
            return buf;
        }
        return buf;
    }
    /* Ephemeral */
    const char *env = getenv("IV_BACKUP_DIR");
    if (env && *env)
        return env;
    static char buf[PATH_MAX];
    const char *user = get_username();
    size_t pre = strlen("/tmp/iv_");
    size_t ulen = user ? strlen(user) : 0;
    if (pre + ulen + 1 > sizeof(buf))
    {
        if (sizeof(buf))
            buf[0] = '\0';
        return buf;
    }
    memcpy(buf, "/tmp/iv_", pre);
    if (ulen)
        memcpy(buf + pre, user, ulen);
    buf[pre + ulen] = '\0';
    return buf;
}

/* ── Per-file subdirectory ──────────────────────────────────────────────── */

/* Find the repository root directory by walking up from path:
 * the first directory that contains .git, or the highest reachable directory.
 * Copies the basename (not the full path) into repo_name. */
static void find_repo_root_name(const char *abspath, char *repo_name, size_t size)
{
    char dir[PATH_MAX];
    if (!abspath)
        abspath = "";
    {
        size_t alen = strlen(abspath);
        if (alen >= sizeof(dir))
            alen = sizeof(dir) - 1;
        memcpy(dir, abspath, alen);
        dir[alen] = '\0';
    }

    /* Walk up until we find .git or reach filesystem root */
    char best[PATH_MAX];
    {
        size_t dlen = strlen(dir);
        if (dlen >= sizeof(best))
            dlen = sizeof(best) - 1;
        memcpy(best, dir, dlen);
        best[dlen] = '\0';
    }

    for (;;)
    {
        char probe[PATH_MAX];
        if (join_path2(probe, sizeof(probe), dir, ".git") != 0)
            break;
        struct stat st;
        if (stat(probe, &st) == 0)
        {
            /* Found .git: repo root is dir */
            {
                size_t dlen = strlen(dir);
                if (dlen >= sizeof(best))
                    dlen = sizeof(best) - 1;
                memcpy(best, dir, dlen);
                best[dlen] = '\0';
            }
            break;
        }
        /* Go up one level */
        char *slash = strrchr(dir, '/');

        if (!slash || slash == dir)
            break;

        *slash = '\0';
    }

    /* Keep only the basename of the root directory */
    const char *base = strrchr(best, '/');
    const char *src = base ? base + 1 : best;
    if (size == 0)
        return;
    {
        size_t slen = strlen(src);
        if (slen >= size)
            slen = size - 1;
        memcpy(repo_name, src, slen);
        repo_name[slen] = '\0';
    }
    if (!repo_name[0])
        snprintf(repo_name, size, "root");
}

void get_backup_subdir(const char *filename, char *buf, size_t size)
{
    /* Resolve absolute path */
    char abspath[PATH_MAX];
    if (!realpath(filename, abspath))
    {
        /* If the file doesn't exist yet, build it manually */
        if (filename[0] == '/')
        {
            snprintf(abspath, sizeof(abspath), "%s", filename);
        }
        else
        {
            char cwd[PATH_MAX];
            if (!getcwd(cwd, sizeof(cwd)))
                snprintf(cwd, sizeof(cwd), ".");
            if (join_path2(abspath, sizeof(abspath), cwd, filename) != 0)
            {
                if (sizeof(abspath))
                    abspath[0] = '\0';
                return;
            }
        }
    }

    char repo_name[256];
    find_repo_root_name(abspath, repo_name, sizeof(repo_name));

    /* Relative path from the repo root to the file.
     * If we didn't find a repo root with .git, we use the sanitized full path. */
    /* Sanitize: replace '/' with '%' in the file's full path,
     * prefixed with the repo name. */
    char sanitized[PATH_MAX];
    size_t j = 0;
    /* Skip the leading '/' in abspath */
    const char *p = abspath[0] == '/' ? abspath + 1 : abspath;
    /* Look for the repo prefix in the absolute path so we can omit it */
    /* Simplification: we only use the file basename + repo as prefix */
    /* Build: repo_name % sanitized_relative_path */
    for (; *p && j < sizeof(sanitized) - 1; p++)
        sanitized[j++] = (*p == '/') ? '%' : *p;
    sanitized[j] = '\0';

    /* Final subdir: repo_name%rest_of_path (do not repeat repo_name if already present) */
    /* Check whether sanitized starts with repo_name% */
    size_t rlen = strlen(repo_name);
    if (strncmp(sanitized, repo_name, rlen) == 0 &&
        (sanitized[rlen] == '%' || sanitized[rlen] == '\0'))
    {
        if (size == 0)
            return;
        strncpy(buf, sanitized, size - 1);
        buf[size - 1] = '\0';
    }
    else
    {
        size_t slen = strlen(sanitized);
        if (size == 0)
            return;
        if (rlen + 1 + slen + 1 > size)
        {
            buf[0] = '\0';
            return;
        }
        memcpy(buf, repo_name, rlen);
        buf[rlen] = '%';
        memcpy(buf + rlen + 1, sanitized, slen);
        buf[rlen + 1 + slen] = '\0';
    }
}

void get_backup_dir_for_file(const char *filename, int persisted,
                             char *buf, size_t size)
{
    char subdir[PATH_MAX];
    get_backup_subdir(filename, subdir, sizeof(subdir));
    if (join_path2(buf, size, get_backup_root(persisted), subdir) != 0)
    {
        if (size)
            buf[0] = '\0';
        return;
    }
    mkdir_p(buf);
}

void get_backup_path_n(const char *filename, int persisted, int n,
                       char *buf, size_t size)
{
    char dir[PATH_MAX];
    get_backup_dir_for_file(filename, persisted, dir, sizeof(dir));
    if (join_path_num(buf, size, dir, n, ".bak") != 0)
    {
        if (size)
            buf[0] = '\0';
        return;
    }
}

void get_backup_meta_path(const char *filename, int persisted, int n,
                          char *buf, size_t size)
{
    char dir[PATH_MAX];
    get_backup_dir_for_file(filename, persisted, dir, sizeof(dir));
    if (join_path_num(buf, size, dir, n, ".meta") != 0)
    {
        if (size)
            buf[0] = '\0';
        return;
    }
}

/* ── Backup: create ─────────────────────────────────────────────────────── */

/* Count how many backup slots exist for filename. */
static int count_backup_slots(const char *filename, int persisted)
{
    char path[PATH_MAX];
    int n = 1;
    while (1)
    {
        get_backup_path_n(filename, persisted, n, path, sizeof(path));
        struct stat st;
        if (stat(path, &st) != 0)
            break;
        n++;
    }
    return n - 1;
}

void backup_file(const char *filename, int persisted)
{
    int slots = count_backup_slots(filename, persisted);

    /* Rotate all existing slots upward (no fixed limit) */
    char src[PATH_MAX], dst[PATH_MAX];
    for (int k = slots; k >= 1; k--)
    {
        get_backup_path_n(filename, persisted, k, src, sizeof(src));
        get_backup_path_n(filename, persisted, k + 1, dst, sizeof(dst));
        rename(src, dst);

        get_backup_meta_path(filename, persisted, k, src, sizeof(src));
        get_backup_meta_path(filename, persisted, k + 1, dst, sizeof(dst));
        rename(src, dst);
    }

    /* Write slot 1 */
    get_backup_path_n(filename, persisted, 1, dst, sizeof(dst));
    FILE *fsrc = fopen(filename, "r");
    if (!fsrc)
        return;
    FILE *fdst = fopen(dst, "w");
    if (!fdst)
    {
        fclose(fsrc);
        return;
    }
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, fsrc) != -1)
        fputs(line, fdst);
    free(line);
    fclose(fsrc);
    fclose(fdst);

    /* Write metadata */
    get_backup_meta_path(filename, persisted, 1, dst, sizeof(dst));
    FILE *meta = fopen(dst, "w");
    if (meta)
    {
        fprintf(meta, "%ld %s\n", (long)time(NULL), get_username());
        fclose(meta);
    }
}

/* ── persist / unpersist ────────────────────────────────────────────────── */

int transfer_backup_repo(const char *filename, int to_persist)
{
    char src_dir[PATH_MAX], dst_dir[PATH_MAX];
    get_backup_dir_for_file(filename, !to_persist, src_dir, sizeof(src_dir));
    get_backup_dir_for_file(filename, to_persist, dst_dir, sizeof(dst_dir));

    /* Try atomic rename first */
    if (rename(src_dir, dst_dir) == 0)
        return 0;
    if (errno != EXDEV)
    {
        perror("iv: transfer_backup_repo rename");
        return -1;
    }

    /* Cross-filesystem: copy file by file */
    DIR *d = opendir(src_dir);
    if (!d)
    {
        perror(src_dir);
        return -1;
    }

    struct dirent *e;

    int ok = 0;

    while ((e = readdir(d)))
    {
        if (e->d_name[0] == '.')
            continue;

        char s[PATH_MAX * 2], t[PATH_MAX * 2];

        if (join_path2(s, sizeof(s), src_dir, e->d_name) != 0)
        {
            if (sizeof(s))
                s[0] = '\0';
            continue;
        }
        if (join_path2(t, sizeof(t), dst_dir, e->d_name) != 0)
        {
            if (sizeof(t))
                t[0] = '\0';
            continue;
        }

        if (move_file(s, t) != 0)
        {
            fprintf(stderr, "iv: failed to move %s → %s\n", s, t);
            ok = -1;
        }
    }
    closedir(d);

    if (ok == 0)
        rmdir(src_dir);

    return ok;
}

/* ── Write with escapes ─────────────────────────────────────────────────── */

void write_with_escapes(FILE *f, const char *text)
{
    for (const char *p = text; *p; p++)
    {
        if (*p == '\\' && *(p + 1))
        {
            p++;
            switch (*p)
            {
            case 'n':
                fputc('\n', f);
                break;
            case 't':
                fputc('\t', f);
                break;
            case '\\':
                fputc('\\', f);
                break;
            case 'r':
                fputc('\r', f);
                break;
            default:
                fputc('\\', f);
                fputc(*p, f);
                break;
            }
        }
        else
        {
            fputc(*p, f);
        }
    }
    fputc('\n', f);
}

/* ── apply_patch ────────────────────────────────────────────────────────── */

int apply_patch(const char *filename, char *lines[], int count,
                int start, int end, const char *new_text, int mode,
                const IvOpts *opts)
{
    int do_backup = !opts->no_backup && !opts->to_stdout;
    int dry = opts->dry_run;

    if (do_backup && !dry)
        backup_file(filename, 0); /* ephemeral backups by default */

    FILE *f;
    if (dry)
        f = NULL;
    else if (opts->to_stdout)
        f = stdout;
    else
    {
        f = fopen(filename, "w");
        if (!f)
        {
            perror("Could not write file");
            return -1;
        }
    }

    int wrote_new = 0;

    /* mode 4: patch insert — insert before start, shift the rest down */
    if (mode == 4)
    {
        for (int i = 0; i < count; i++)
        {
            if (i + 1 == start)
            {
                if (f)
                    write_with_escapes(f, new_text);
                wrote_new = 1;
            }
            if (f)
                fputs(lines[i], f);
        }
        if (start > count || count == 0)
        {
            if (f)
                write_with_escapes(f, new_text);
            wrote_new = 1;
        }
        if (f && f != stdout)
            fclose(f);
        return wrote_new ? 0 : -1;
    }

    for (int i = 0; i < count; i++)
    {
        if (i + 1 >= start && i + 1 <= end)
        {
            if (mode == 2)
                continue; /* delete */
            if (mode == 3)
            { /* replace */
                if (f)
                    write_with_escapes(f, new_text);
                wrote_new = 1;
            }
            else if (mode == 1)
            { /* insert before */
                if (f)
                    write_with_escapes(f, new_text);
                if (f)
                    fputs(lines[i], f);
                wrote_new = 1;
            }
        }
        else
        {
            if (f)
                fputs(lines[i], f);
        }
    }

    if ((mode == 1 || mode == 3) && (start > count || count == 0))
    {
        if (f)
            write_with_escapes(f, new_text);
        wrote_new = 1;
    }

    if (f && f != stdout)
        fclose(f);
    return wrote_new ? 0 : -1;
}

/* ── Search / replace ───────────────────────────────────────────────────── */

static char *replace_in_string(const char *line, const char *pat,
                               const char *repl, int global, int *n)
{
    size_t plen = strlen(pat);
    size_t rlen = strlen(repl);
    size_t cap = strlen(line) + 256;
    char *out = malloc(cap);
    if (!out)
        return NULL;
    size_t len = 0;
    const char *cur = line;
    *n = 0;
    while (*cur)
    {
        const char *p = strstr(cur, pat);
        if (!p)
        {
            size_t rest = strlen(cur);
            if (len + rest + 1 >= cap)
            {
                cap = len + rest + 1;
                char *tmp = realloc(out, cap);
                if (!tmp)
                {
                    free(out);
                    return NULL;
                }
                out = tmp;
            }
            memcpy(out + len, cur, rest);
            len += rest;
            break;
        }
        size_t before = (size_t)(p - cur);
        if (len + before + rlen + 1 >= cap)
        {
            cap = len + before + rlen + 256;
            char *tmp = realloc(out, cap);
            if (!tmp)
            {
                free(out);
                return NULL;
            }
            out = tmp;
        }
        memcpy(out + len, cur, before);
        len += before;
        memcpy(out + len, repl, rlen);
        len += rlen;
        cur = p + plen;
        (*n)++;
        if (!global)
        {
            size_t rest = strlen(cur);
            if (len + rest + 1 >= cap)
            {
                cap = len + rest + 256;
                char *tmp = realloc(out, cap);
                if (!tmp)
                {
                    free(out);
                    return NULL;
                }
                out = tmp;
            }
            memcpy(out + len, cur, rest);
            len += rest;
            break;
        }
    }
    out[len] = '\0';
    return out;
}

int search_replace(char *lines[], int count, const char *pattern,
                   const char *replacement, int global)
{
    if (!pattern || !*pattern)
        return 0;
    int total = 0;
    for (int i = 0; i < count; i++)
    {
        int n;
        char *nl = replace_in_string(lines[i], pattern, replacement, global, &n);
        if (nl && n > 0)
        {
            free(lines[i]);
            lines[i] = nl;
            total += n;
        }
        else
            free(nl);
    }
    return total;
}

static char *replace_regex_in_string(const char *line, regex_t *re,
                                     const char *repl, int global, int *n)
{
    size_t rlen = strlen(repl);
    size_t cap = strlen(line) + 256;
    char *out = malloc(cap);
    if (!out)
        return NULL;
    size_t len = 0;
    const char *cur = line;
    regmatch_t m;
    *n = 0;
    while (regexec(re, cur, 1, &m, 0) == 0)
    {
        size_t before = (size_t)m.rm_so;
        if (len + before + rlen + 1 >= cap)
        {
            cap = len + before + rlen + 256;
            char *tmp = realloc(out, cap);
            if (!tmp)
            {
                free(out);
                return NULL;
            }
            out = tmp;
        }
        memcpy(out + len, cur, before);
        len += before;
        memcpy(out + len, repl, rlen);
        len += rlen;
        cur += m.rm_eo;
        (*n)++;
        if (!global)
        {
            size_t rest = strlen(cur);
            if (len + rest + 1 >= cap)
            {
                cap = len + rest + 256;
                char *tmp = realloc(out, cap);
                if (!tmp)
                {
                    free(out);
                    return NULL;
                }
                out = tmp;
            }
            memcpy(out + len, cur, rest);
            len += rest;
            break;
        }
    }
    if (*n == 0)
    {
        size_t l = strlen(line);
        if (l + 1 > cap)
        {
            char *tmp = realloc(out, l + 1);
            if (tmp)
                out = tmp;
        }
        strcpy(out, line);
        len = l;
    }
    out[len] = '\0';
    return out;
}

int search_replace_regex(char *lines[], int count, const char *pattern,
                         const char *replacement, int global)
{
    if (!pattern || !*pattern)
        return 0;
    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED) != 0)
        return -1;
    int total = 0;
    for (int i = 0; i < count; i++)
    {
        int n;
        char *nl = replace_regex_in_string(lines[i], &re, replacement, global, &n);
        if (nl && n > 0)
        {
            free(lines[i]);
            lines[i] = nl;
            total += n;
        }
        else
            free(nl);
    }
    regfree(&re);
    return total;
}

int search_replace_filtered(char *lines[], int count, const char *pattern,
                            const char *replacement, int global,
                            const char *filter)
{
    if (!pattern || !*pattern)
        return 0;
    int total = 0;
    for (int i = 0; i < count; i++)
    {
        if (filter && !strstr(lines[i], filter))
            continue;
        int n;
        char *nl = replace_in_string(lines[i], pattern, replacement, global, &n);
        if (nl && n > 0)
        {
            free(lines[i]);
            lines[i] = nl;
            total += n;
        }
        else
            free(nl);
    }
    return total;
}

int search_replace_regex_filtered(char *lines[], int count, const char *pattern,
                                  const char *replacement, int global,
                                  const char *filter)
{
    if (!pattern || !*pattern)
        return 0;
    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED) != 0)
        return -1;
    int total = 0;
    for (int i = 0; i < count; i++)
    {
        if (filter && !strstr(lines[i], filter))
            continue;
        int n;
        char *nl = replace_regex_in_string(lines[i], &re, replacement, global, &n);
        if (nl && n > 0)
        {
            free(lines[i]);
            lines[i] = nl;
            total += n;
        }
        else
            free(nl);
    }
    regfree(&re);
    return total;
}

static char *replace_field_in_line(const char *line, char delim,
                                   int field_num, const char *value)
{
    size_t vlen = strlen(value);
    size_t linelen = strlen(line);
    char *out = malloc(linelen + vlen + 64);
    if (!out)
        return NULL;
    const char *p = line, *field_start = line;
    int f = 1;
    while (f < field_num && *p)
    {
        if (*p == delim)
        {
            f++;
            p++;
            field_start = p;
        }
        else
            p++;
    }
    if (f != field_num)
    {
        strcpy(out, line);
        return out;
    }
    size_t len = (size_t)(field_start - line);
    memcpy(out, line, len);
    memcpy(out + len, value, vlen + 1);
    len += vlen;
    while (*p && *p != delim && *p != '\n')
        p++;
    strcpy(out + len, p);
    return out;
}

int replace_field(char *lines[], int count, char delim, int field_num,
                  const char *value)
{
    if (!delim || field_num < 1)
        return 0;
    for (int i = 0; i < count; i++)
    {
        char *nl = replace_field_in_line(lines[i], delim, field_num, value);
        if (nl)
        {
            free(lines[i]);
            lines[i] = nl;
        }
    }
    return count;
}

/* ── Write lines ────────────────────────────────────────────────────────── */

void write_lines_to_file(const char *filename, char *lines[], int count)
{
    FILE *f = fopen(filename, "w");
    if (!f)
    {
        perror("Could not write file");
        return;
    }
    for (int i = 0; i < count; i++)
        fputs(lines[i], f);
    fclose(f);
}

void write_lines_to_stream(FILE *f, char *lines[], int count)
{
    for (int i = 0; i < count; i++)
        fputs(lines[i], f);
}

/* ── Metadata ───────────────────────────────────────────────────────────── */

static int read_backup_meta(const char *path_meta, time_t *out_ts,
                            char *out_user, size_t user_size __attribute__((unused)))
{
    FILE *f = fopen(path_meta, "r");
    if (!f)
        return -1;
    long epoch = 0;
    int n = fscanf(f, "%ld %255s", &epoch, out_user);
    fclose(f);
    if (n >= 1)
    {
        *out_ts = (time_t)epoch;
        if (n < 2)
            out_user[0] = '\0';
        return 0;
    }
    return -1;
}

/* ── Backup listing ─────────────────────────────────────────────────────── */

void list_backups(const char *filter, int persisted)
{
    const char *root = get_backup_root(persisted);
    DIR *d = opendir(root);
    if (!d)
    {
        perror(root);
        return;
    }

    struct dirent *e;
    while ((e = readdir(d)))
    {
        if (e->d_name[0] == '.')
            continue;

        /* If there is a filter, verify that the subdir matches the file */
        if (filter && *filter)
        {
            char subdir[PATH_MAX];
            get_backup_subdir(filter, subdir, sizeof(subdir));
            if (strcmp(e->d_name, subdir) != 0)
                continue;
        }

        char subpath[PATH_MAX];
        if (join_path2(subpath, sizeof(subpath), root, e->d_name) != 0)
            continue;

        /* List slots inside the subdirectory */
        DIR *sd = opendir(subpath);
        if (!sd)
            continue;
        struct dirent *se;
        while ((se = readdir(sd)))
        {
            if (se->d_name[0] == '.')
                continue;
            size_t len = strlen(se->d_name);
            if (len < 5 || strcmp(se->d_name + len - 4, ".bak") != 0)
                continue;
            char spath[PATH_MAX];
            if (join_path2(spath, sizeof(spath), subpath, se->d_name) != 0)
                continue;
            struct stat st;
            if (stat(spath, &st) == 0)
                printf("%s  %zu bytes\n", spath, (size_t)st.st_size);
        }
        closedir(sd);
    }
    closedir(d);
}

void list_backups_with_meta(const char *filter, int persisted)
{
    const char *root = get_backup_root(persisted);
    DIR *d = opendir(root);
    if (!d)
    {
        perror(root);
        return;
    }

    struct dirent *e;
    while ((e = readdir(d)))
    {
        if (e->d_name[0] == '.')
            continue;

        if (filter && *filter)
        {
            char subdir[PATH_MAX];
            get_backup_subdir(filter, subdir, sizeof(subdir));
            if (strcmp(e->d_name, subdir) != 0)
                continue;
        }

        char subpath[PATH_MAX];
        if (join_path2(subpath, sizeof(subpath), root, e->d_name) != 0)
            continue;

        DIR *sd = opendir(subpath);
        if (!sd)
            continue;
        struct dirent *se;
        while ((se = readdir(sd)))
        {
            if (se->d_name[0] == '.')
                continue;
            size_t len = strlen(se->d_name);
            if (len < 5 || strcmp(se->d_name + len - 4, ".bak") != 0)
                continue;

            char spath[PATH_MAX];
            if (join_path2(spath, sizeof(spath), subpath, se->d_name) != 0)
                continue;
            struct stat st;
            if (stat(spath, &st) != 0)
                continue;

            /* Read corresponding .meta */
            char mpath[PATH_MAX];
            snprintf(mpath, sizeof(mpath), "%.*smeta",
                     (int)(strlen(spath) - 3), spath);
            time_t ts = 0;
            char user[256] = "";
            int has_meta = (read_backup_meta(mpath, &ts, user, sizeof(user)) == 0);

            printf("%s  %zu bytes", spath, (size_t)st.st_size);
            if (has_meta)
            {
                char tbuf[64];
                struct tm *tm = localtime(&ts);
                if (tm && strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", tm) > 0)
                    printf("  %s  %s", tbuf, user[0] ? user : "?");
            }
            printf("\n");
        }
        closedir(sd);
    }
    closedir(d);
}

int show_backup_slot(const char *filename, int persisted, int n)
{
    char path_bak[PATH_MAX], path_meta[PATH_MAX];
    get_backup_path_n(filename, persisted, n, path_bak, sizeof(path_bak));
    get_backup_meta_path(filename, persisted, n, path_meta, sizeof(path_meta));

    FILE *f = fopen(path_bak, "r");
    if (!f)
    {
        fprintf(stderr, "iv: no backup %d found for %s\n", n, filename);
        return -1;
    }

    time_t ts = 0;
    char user[256] = "";
    if (read_backup_meta(path_meta, &ts, user, sizeof(user)) == 0)
    {
        char buf[64];
        struct tm *tm = localtime(&ts);
        if (tm && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm) > 0)
            fprintf(stderr, "# backup %d  %s  user: %s\n", n, buf,
                    user[0] ? user : "?");
    }

    char line[4096];
    while (fgets(line, sizeof(line), f))
        fputs(line, stdout);
    fclose(f);
    return 0;
}

/* ── Backup cleanup ─────────────────────────────────────────────────────── */

void clean_backups(const char *filter, int persisted)
{
    const char *root = get_backup_root(persisted);
    DIR *d = opendir(root);
    if (!d)
    {
        perror(root);
        return;
    }

    struct dirent *e;
    int removed = 0;
    while ((e = readdir(d)))
    {
        if (e->d_name[0] == '.')
            continue;

        if (filter && *filter)
        {
            char subdir[PATH_MAX * 2];

            get_backup_subdir(filter, subdir, sizeof(subdir));
            if (strcmp(e->d_name, subdir) != 0)
                continue;
        }

        char subpath[PATH_MAX * 2];
        snprintf(subpath, sizeof(subpath), "%s/%s", root, e->d_name);

        DIR *sd = opendir(subpath);
        if (!sd)
            continue;
        struct dirent *se;
        while ((se = readdir(sd)))
        {
            if (se->d_name[0] == '.')
                continue;

            char spath[PATH_MAX * 2];

            if (strlen(subpath) + strlen(se->d_name) + 2 > sizeof(spath))
                continue;

            if (join_path2(spath, sizeof(spath), subpath, se->d_name) != 0)
                continue;

            if (remove(spath) == 0)
                removed++;
        }
        closedir(sd);
        rmdir(subpath);
    }
    closedir(d);

    if (removed > 0)
        fprintf(stderr, "iv: removed %d file(s)\n", removed);
}