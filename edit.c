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

/* Move src → dst, trying rename first (same filesystem),
 * then copy+unlink if crossing filesystems. */
static int move_file(const char *src, const char *dst)
{
    if (rename(src, dst) == 0)
        return 0;
    if (errno != EXDEV)
        return -1;
    if (iv_copy_file(src, dst) != 0)
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

int backup_file(const char *filename, int persisted)
{
    struct stat st;
    int slots;
    char src[PATH_MAX], dst[PATH_MAX];
    FILE *meta;

    if (stat(filename, &st) != 0)
        return (errno == ENOENT) ? 0 : -1;

    slots = count_backup_slots(filename, persisted);
    for (int k = slots; k >= IV_BACKUP_SLOTS; k--)
    {
        get_backup_path_n(filename, persisted, k, src, sizeof(src));
        unlink(src);
        get_backup_meta_path(filename, persisted, k, src, sizeof(src));
        unlink(src);
    }
    if (slots >= IV_BACKUP_SLOTS)
        slots = IV_BACKUP_SLOTS - 1;

    for (int k = slots; k >= 1; k--)
    {
        get_backup_path_n(filename, persisted, k, src, sizeof(src));
        get_backup_path_n(filename, persisted, k + 1, dst, sizeof(dst));
        if (rename(src, dst) != 0 && errno != ENOENT)
            return -1;

        get_backup_meta_path(filename, persisted, k, src, sizeof(src));
        get_backup_meta_path(filename, persisted, k + 1, dst, sizeof(dst));
        if (rename(src, dst) != 0 && errno != ENOENT)
            return -1;
    }

    get_backup_path_n(filename, persisted, 1, dst, sizeof(dst));
    if (iv_copy_file(filename, dst) != 0)
        return -1;

    get_backup_meta_path(filename, persisted, 1, dst, sizeof(dst));
    meta = fopen(dst, "w");
    if (meta)
    {
        fprintf(meta, "%ld %s\n", (long)time(NULL), get_username());
        if (fclose(meta) != 0)
            return -1;
    }
    return 0;
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

struct PatchCtx
{
    char **lines;
    int count;
    int start;
    int end;
    const char *new_text;
    int mode;
};

static int emit_patch(FILE *f, const struct PatchCtx *p, int *wrote_new)
{
    int i;

    *wrote_new = 0;
    if (p->mode == 4)
    {
        for (i = 0; i < p->count; i++)
        {
            if (i + 1 == p->start)
            {
                write_with_escapes(f, p->new_text);
                *wrote_new = 1;
            }
            if (fputs(p->lines[i], f) == EOF)
                return -1;
        }
        if (p->start > p->count || p->count == 0)
        {
            write_with_escapes(f, p->new_text);
            *wrote_new = 1;
        }
        return 0;
    }

    for (i = 0; i < p->count; i++)
    {
        if (i + 1 >= p->start && i + 1 <= p->end)
        {
            if (p->mode == 2)
                continue;
            if (p->mode == 3)
            {
                write_with_escapes(f, p->new_text);
                *wrote_new = 1;
            }
            else if (p->mode == 1)
            {
                write_with_escapes(f, p->new_text);
                if (fputs(p->lines[i], f) == EOF)
                    return -1;
                *wrote_new = 1;
            }
        }
        else if (fputs(p->lines[i], f) == EOF)
            return -1;
    }

    if ((p->mode == 1 || p->mode == 3) && (p->start > p->count || p->count == 0))
    {
        write_with_escapes(f, p->new_text);
        *wrote_new = 1;
    }
    return 0;
}

static int patch_write(FILE *out, void *ctx)
{
    int wrote_new = 0;
    return emit_patch(out, ctx, &wrote_new);
}

int apply_patch(const char *filename, char *lines[], int count,
                int start, int end, const char *new_text, int mode,
                const IvOpts *opts)
{
    struct PatchCtx ctx = {lines, count, start, end, new_text, mode};
    int do_backup = !opts->no_backup && !opts->to_stdout;
    int wrote_new = 0;

    if (do_backup && !opts->dry_run)
    {
        if (backup_file(filename, opts->persist) != 0)
        {
            fprintf(stderr, "iv: backup failed, aborting (original unchanged)\n");
            return -1;
        }
    }

    if (opts->dry_run)
        return 0;

    if (opts->to_stdout)
    {
        if (emit_patch(stdout, &ctx, &wrote_new) != 0 || iv_check_stream(stdout) != 0)
        {
            fprintf(stderr, "iv: write failed\n");
            return -1;
        }
        return (mode == 2 || wrote_new) ? 0 : -1;
    }

    if (iv_commit_stream(filename, patch_write, &ctx) != 0)
        return -1;
    return 0;
}

/* ── Search / replace ───────────────────────────────────────────────────── */

static int append_mem(char **out, size_t *len, size_t *cap, const char *s, size_t n)
{
    if (*len + n + 1 >= *cap)
    {
        size_t nc = *len + n + 256;
        char *tmp = realloc(*out, nc);
        if (!tmp)
            return -1;
        *out = tmp;
        *cap = nc;
    }
    memcpy(*out + *len, s, n);
    *len += n;
    return 0;
}

static char *replace_in_string(const char *line, const char *pat,
                               const char *repl, int global, int *n)
{
    size_t plen = strlen(pat);
    size_t rlen = strlen(repl);
    size_t cap;
    char *out;
    size_t len = 0;
    const char *cur = line;

    *n = 0;
    if (!pat || !*pat || !strstr(line, pat))
        return NULL;
    cap = strlen(line) + 256;
    out = malloc(cap);
    if (!out)
        return NULL;
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

#define IV_RE_NMATCH 10

static int expand_repl(char **out, size_t *len, size_t *cap, const char *repl,
                       const char *cur, const regmatch_t *pm, size_t nmatch)
{
    const char *p;

    for (p = repl; *p; p++)
    {
        if (*p == '\\' && p[1])
        {
            p++;
            if (*p == '\\')
            {
                if (append_mem(out, len, cap, "\\", 1) != 0)
                    return -1;
            }
            else if (*p == '&' || *p == '0')
            {
                if (pm[0].rm_so >= 0 &&
                    append_mem(out, len, cap, cur + pm[0].rm_so,
                               (size_t)(pm[0].rm_eo - pm[0].rm_so)) != 0)
                    return -1;
            }
            else if (*p >= '1' && *p <= '9')
            {
                int i = *p - '0';
                if (i < (int)nmatch && pm[i].rm_so >= 0 &&
                    append_mem(out, len, cap, cur + pm[i].rm_so,
                               (size_t)(pm[i].rm_eo - pm[i].rm_so)) != 0)
                    return -1;
            }
            else
            {
                char lit[2] = {'\\', *p};
                if (append_mem(out, len, cap, lit, 2) != 0)
                    return -1;
            }
        }
        else if (*p == '&')
        {
            if (pm[0].rm_so >= 0 &&
                append_mem(out, len, cap, cur + pm[0].rm_so,
                           (size_t)(pm[0].rm_eo - pm[0].rm_so)) != 0)
                return -1;
        }
        else if (append_mem(out, len, cap, p, 1) != 0)
            return -1;
    }
    return 0;
}

static char *replace_regex_in_string(const char *line, regex_t *re,
                                     const char *repl, int global, int *n)
{
    size_t cap = strlen(line) + 256;
    char *out = malloc(cap);
    size_t len = 0;
    const char *cur = line;
    regmatch_t pm[IV_RE_NMATCH];
    int eflags = 0;

    if (!out)
        return NULL;
    *n = 0;
    while (regexec(re, cur, IV_RE_NMATCH, pm, eflags) == 0)
    {
        size_t before = (size_t)pm[0].rm_so;
        if (append_mem(&out, &len, &cap, cur, before) != 0)
        {
            free(out);
            return NULL;
        }
        if (expand_repl(&out, &len, &cap, repl, cur, pm, IV_RE_NMATCH) != 0)
        {
            free(out);
            return NULL;
        }
        (*n)++;
        if (pm[0].rm_eo == 0)
        {
            if (!cur[0])
                break;
            if (append_mem(&out, &len, &cap, cur, 1) != 0)
            {
                free(out);
                return NULL;
            }
            cur += 1;
        }
        else
            cur += pm[0].rm_eo;
        if (!global)
        {
            if (append_mem(&out, &len, &cap, cur, strlen(cur)) != 0)
            {
                free(out);
                return NULL;
            }
            break;
        }
        eflags = (cur > line && cur[-1] != '\n') ? REG_NOTBOL : 0;
    }
    if (*n == 0)
    {
        free(out);
        return NULL;
    }
    if (global && append_mem(&out, &len, &cap, cur, strlen(cur)) != 0)
    {
        free(out);
        return NULL;
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

static int line_matches_filter(const char *line, const char *filter,
                               const regex_t *fre)
{
    if (!filter || !*filter)
        return 1;
    if (fre)
        return regexec(fre, line, 0, NULL, 0) == 0;
    return strstr(line, filter) != NULL;
}

int search_replace_filtered(char *lines[], int count, const char *pattern,
                            const char *replacement, int global,
                            const char *filter, int filter_regex)
{
    regex_t fre;
    regex_t *fp = NULL;
    int total = 0;

    if (!pattern || !*pattern)
        return 0;
    if (filter && *filter && filter_regex)
    {
        if (regcomp(&fre, filter, REG_EXTENDED | REG_NOSUB) != 0)
            return -1;
        fp = &fre;
    }
    for (int i = 0; i < count; i++)
    {
        int n;
        char *nl;
        if (!line_matches_filter(lines[i], filter, fp))
            continue;
        nl = replace_in_string(lines[i], pattern, replacement, global, &n);
        if (nl && n > 0)
        {
            free(lines[i]);
            lines[i] = nl;
            total += n;
        }
        else
            free(nl);
    }
    if (fp)
        regfree(fp);
    return total;
}

int search_replace_regex_filtered(char *lines[], int count, const char *pattern,
                                  const char *replacement, int global,
                                  const char *filter, int filter_regex)
{
    regex_t re, fre;
    regex_t *fp = NULL;
    int total = 0;

    if (!pattern || !*pattern)
        return 0;
    if (regcomp(&re, pattern, REG_EXTENDED) != 0)
        return -1;
    if (filter && *filter && filter_regex)
    {
        if (regcomp(&fre, filter, REG_EXTENDED | REG_NOSUB) != 0)
        {
            regfree(&re);
            return -1;
        }
        fp = &fre;
    }
    for (int i = 0; i < count; i++)
    {
        int n;
        char *nl;
        if (!line_matches_filter(lines[i], filter, fp))
            continue;
        nl = replace_regex_in_string(lines[i], &re, replacement, global, &n);
        if (nl && n > 0)
        {
            free(lines[i]);
            lines[i] = nl;
            total += n;
        }
        else
            free(nl);
    }
    if (fp)
        regfree(fp);
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

int iv_stream_subst(FILE *in, FILE *out, const char *(*pairs)[2],
                    int npairs, const IvOpts *opts, int *nrepl)
{
    char *line = NULL;
    size_t cap = 0;
    regex_t *res = NULL;
    regex_t fre;
    regex_t *fp = NULL;
    int total = 0;
    int i;
    ssize_t nread;

    if (nrepl)
        *nrepl = 0;
    if (opts->use_regex)
    {
        res = calloc((size_t)npairs, sizeof(*res));
        if (!res)
            return -1;
        for (i = 0; i < npairs; i++)
        {
            if (regcomp(&res[i], pairs[i][0], REG_EXTENDED) != 0)
            {
                while (--i >= 0)
                    regfree(&res[i]);
                free(res);
                return -1;
            }
        }
    }
    if (opts->multimatch && opts->use_regex)
    {
        if (regcomp(&fre, opts->multimatch, REG_EXTENDED | REG_NOSUB) != 0)
        {
            if (res)
            {
                for (i = 0; i < npairs; i++)
                    regfree(&res[i]);
                free(res);
            }
            return -1;
        }
        fp = &fre;
    }

    while ((nread = getline(&line, &cap, in)) != -1)
    {
        char *cur = line;
        int owned = 0;

        if (memchr(line, 0, (size_t)nread))
        {
            free(line);
            if (res)
            {
                for (i = 0; i < npairs; i++)
                    regfree(&res[i]);
                free(res);
            }
            if (fp)
                regfree(fp);
            fprintf(stderr, "iv: refusing to edit binary file\n");
            return -1;
        }

        if (line_matches_filter(cur, opts->multimatch, fp))
        {
            for (i = 0; i < npairs; i++)
            {
                int n = 0;
                char *nl = opts->use_regex
                               ? replace_regex_in_string(cur, &res[i], pairs[i][1],
                                                         opts->global_replace, &n)
                               : replace_in_string(cur, pairs[i][0], pairs[i][1],
                                                   opts->global_replace, &n);
                if (nl && n > 0)
                {
                    if (owned)
                        free(cur);
                    cur = nl;
                    owned = 1;
                    total += n;
                }
                else
                    free(nl);
            }
        }
        if (fputs(cur, out) == EOF)
        {
            if (owned)
                free(cur);
            free(line);
            if (res)
            {
                for (i = 0; i < npairs; i++)
                    regfree(&res[i]);
                free(res);
            }
            if (fp)
                regfree(fp);
            return -1;
        }
        if (owned)
            free(cur);
    }
    free(line);
    if (res)
    {
        for (i = 0; i < npairs; i++)
            regfree(&res[i]);
        free(res);
    }
    if (fp)
        regfree(fp);
    if (ferror(in))
        return -1;
    if (nrepl)
        *nrepl = total;
    return 0;
}

int iv_stream_fields(FILE *in, FILE *out, char delim, int field_num,
                     const char *value)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;

    while ((nread = getline(&line, &cap, in)) != -1)
    {
        char *nl;
        if (memchr(line, 0, (size_t)nread))
        {
            free(line);
            fprintf(stderr, "iv: refusing to edit binary file\n");
            return -1;
        }
        nl = replace_field_in_line(line, delim, field_num, value);
        if (!nl || fputs(nl, out) == EOF)
        {
            free(nl);
            free(line);
            return -1;
        }
        free(nl);
    }
    free(line);
    return ferror(in) ? -1 : 0;
}

/* ── Ring buffer + streaming ranges ─────────────────────────────────────── */

typedef struct
{
    char **v;
    int cap;
    int n;
    int i;
} LineRing;

static int ring_init(LineRing *r, int cap)
{
    memset(r, 0, sizeof(*r));
    if (cap < 1)
        return 0;
    r->v = calloc((size_t)cap, sizeof(char *));
    if (!r->v)
        return -1;
    r->cap = cap;
    return 0;
}

static void ring_free(LineRing *r)
{
    int k;

    if (!r->v)
        return;
    for (k = 0; k < r->n; k++)
        free(r->v[(r->i + k) % r->cap]);
    free(r->v);
    memset(r, 0, sizeof(*r));
}

static char *ring_push(LineRing *r, char *owned)
{
    char *evicted;

    if (r->cap < 1)
        return owned;
    if (r->n == r->cap)
    {
        evicted = r->v[r->i];
        r->v[r->i] = owned;
        r->i = (r->i + 1) % r->cap;
        return evicted;
    }
    r->v[(r->i + r->n) % r->cap] = owned;
    r->n++;
    return NULL;
}

static char *ring_at(const LineRing *r, int idx)
{
    return r->v[(r->i + idx) % r->cap];
}

static int emit_line(FILE *out, const char *line, int lineno, int numbered)
{
    if (numbered)
        return (fprintf(out, "%4d | %s", lineno, line) < 0) ? -1 : 0;
    return (fputs(line, out) == EOF) ? -1 : 0;
}

static int emit_repl(FILE *out, const char *text)
{
    if (!text)
        text = "";
    write_with_escapes(out, text);
    return ferror(out) ? -1 : 0;
}

static int in_forward(int n, const IvRangePlan *p)
{
    if (n < p->start)
        return 0;
    if (p->end >= 0 && n > p->end)
        return 0;
    return 1;
}

static void tail_slice(const LineRing *r, const IvRangePlan *p,
                       int *af, int *at)
{
    int virt0 = p->window - r->n;

    *af = p->from - virt0;
    *at = p->to - virt0;
    if (*af < 0)
        *af = 0;
    if (*at > r->n - 1)
        *at = r->n - 1;
}

static int flush_tail(FILE *out, const LineRing *r, const IvRangePlan *p,
                      int op, const char *text, int no_numbers, int total)
{
    int af, at, k, lineno;

    tail_slice(r, p, &af, &at);
    for (k = 0; k < r->n; k++)
    {
        int act = (af <= at && k >= af && k <= at);

        lineno = total - r->n + 1 + k;
        if (op == IV_STREAM_VIEW)
        {
            if (act && emit_line(out, ring_at(r, k), lineno, !no_numbers) != 0)
                return -1;
        }
        else if (op == IV_STREAM_DELETE)
        {
            if (!act && emit_line(out, ring_at(r, k), 0, 0) != 0)
                return -1;
        }
        else if (op == IV_STREAM_REPLACE)
        {
            if (act)
            {
                if (emit_repl(out, text) != 0)
                    return -1;
            }
            else if (emit_line(out, ring_at(r, k), 0, 0) != 0)
                return -1;
        }
    }
    return 0;
}

int iv_stream_by_plan(FILE *in, FILE *out, const IvRangePlan *p, int op,
                      const char *text, int no_numbers)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    int n = 0;
    LineRing ring;
    int hold;

    if (!in || !out || !p)
        return -1;
    hold = (p->kind == IV_RANGE_TAIL) ? p->window
           : (p->kind == IV_RANGE_HYBRID) ? p->hold
                                          : 0;
    if (ring_init(&ring, hold) != 0)
        return -1;

    while ((nread = getline(&line, &cap, in)) != -1)
    {
        char *owned;
        char *evicted;

        n++;
        if (memchr(line, 0, (size_t)nread))
        {
            free(line);
            ring_free(&ring);
            fprintf(stderr, "iv: refusing to edit binary file\n");
            return -1;
        }

        if (p->kind == IV_RANGE_FORWARD)
        {
            int hit = in_forward(n, p);

            if (op == IV_STREAM_VIEW)
            {
                if (p->end >= 0 && n > p->end)
                    break;
                if (hit && emit_line(out, line, n, !no_numbers) != 0)
                    goto fail;
            }
            else if (op == IV_STREAM_DELETE)
            {
                if (!hit && emit_line(out, line, 0, 0) != 0)
                    goto fail;
            }
            else if (hit)
            {
                if (emit_repl(out, text) != 0)
                    goto fail;
            }
            else if (emit_line(out, line, 0, 0) != 0)
                goto fail;
            continue;
        }

        if (p->kind == IV_RANGE_HYBRID && n < p->start)
        {
            if (op == IV_STREAM_VIEW)
                continue;
            if (emit_line(out, line, 0, 0) != 0)
                goto fail;
            continue;
        }

        owned = malloc((size_t)nread + 1);
        if (!owned)
            goto fail;
        memcpy(owned, line, (size_t)nread + 1);
        evicted = ring_push(&ring, owned);
        if (!evicted)
            continue;
        if (p->kind == IV_RANGE_TAIL)
        {
            if (op == IV_STREAM_VIEW)
                free(evicted);
            else if (emit_line(out, evicted, 0, 0) != 0)
            {
                free(evicted);
                goto fail;
            }
            else
                free(evicted);
        }
        else
        {
            /* hybrid: evicted is inside the range */
            if (op == IV_STREAM_VIEW)
            {
                if (emit_line(out, evicted, n - hold, !no_numbers) != 0)
                {
                    free(evicted);
                    goto fail;
                }
                free(evicted);
            }
            else if (op == IV_STREAM_DELETE)
                free(evicted);
            else
            {
                if (emit_repl(out, text) != 0)
                {
                    free(evicted);
                    goto fail;
                }
                free(evicted);
            }
        }
    }
    free(line);
    line = NULL;

    if (p->kind == IV_RANGE_TAIL)
    {
        if (flush_tail(out, &ring, p, op, text, no_numbers, n) != 0)
            goto fail;
    }
    else if (p->kind == IV_RANGE_HYBRID)
    {
        int k;

        for (k = 0; k < ring.n; k++)
        {
            if (op == IV_STREAM_VIEW)
                continue;
            if (emit_line(out, ring_at(&ring, k), 0, 0) != 0)
                goto fail;
        }
    }
    ring_free(&ring);
    return ferror(in) ? -1 : 0;

fail:
    free(line);
    ring_free(&ring);
    return -1;
}

int iv_stream_delete_match(FILE *in, FILE *out, const char *filter,
                           int use_regex)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    regex_t re;
    regex_t *fp = NULL;

    if (filter && *filter && use_regex)
    {
        if (regcomp(&re, filter, REG_EXTENDED | REG_NOSUB) != 0)
            return -1;
        fp = &re;
    }
    while ((nread = getline(&line, &cap, in)) != -1)
    {
        if (memchr(line, 0, (size_t)nread))
        {
            free(line);
            if (fp)
                regfree(fp);
            fprintf(stderr, "iv: refusing to edit binary file\n");
            return -1;
        }
        if (line_matches_filter(line, filter, fp))
            continue;
        if (fputs(line, out) == EOF)
        {
            free(line);
            if (fp)
                regfree(fp);
            return -1;
        }
    }
    free(line);
    if (fp)
        regfree(fp);
    return ferror(in) ? -1 : 0;
}

int iv_stream_replace_match(FILE *in, FILE *out, const char *filter,
                            int use_regex, const char *text)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    regex_t re;
    regex_t *fp = NULL;

    if (filter && *filter && use_regex)
    {
        if (regcomp(&re, filter, REG_EXTENDED | REG_NOSUB) != 0)
            return -1;
        fp = &re;
    }
    while ((nread = getline(&line, &cap, in)) != -1)
    {
        if (memchr(line, 0, (size_t)nread))
        {
            free(line);
            if (fp)
                regfree(fp);
            fprintf(stderr, "iv: refusing to edit binary file\n");
            return -1;
        }
        if (line_matches_filter(line, filter, fp))
        {
            if (emit_repl(out, text) != 0)
            {
                free(line);
                if (fp)
                    regfree(fp);
                return -1;
            }
            continue;
        }
        if (fputs(line, out) == EOF)
        {
            free(line);
            if (fp)
                regfree(fp);
            return -1;
        }
    }
    free(line);
    if (fp)
        regfree(fp);
    return ferror(in) ? -1 : 0;
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