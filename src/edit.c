/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "iv.h"
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <regex.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

/* ── GNU-style backup (Coreutils --backup / -S) ──────────────────────── */

int iv_parse_backup_method(const char *s)
{
    static const struct
    {
        const char *name;
        int type;
    } tab[] = {
        {"none", IV_BACKUP_NONE},
        {"off", IV_BACKUP_NONE},
        {"numbered", IV_BACKUP_NUMBERED},
        {"t", IV_BACKUP_NUMBERED},
        {"existing", IV_BACKUP_EXISTING},
        {"nil", IV_BACKUP_EXISTING},
        {"simple", IV_BACKUP_SIMPLE},
        {"never", IV_BACKUP_SIMPLE},
    };
    size_t n;
    int type = -1;
    size_t i;

    if (!s || !*s)
        return -1;
    n = strlen(s);
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
    {
        size_t ln = strlen(tab[i].name);

        if (n > ln || strncmp(s, tab[i].name, n) != 0)
            continue;
        if (type >= 0 && type != tab[i].type)
            return -1;
        type = tab[i].type;
    }
    return type;
}

int iv_backup_from_env(void)
{
    const char *vc = getenv("VERSION_CONTROL");

    if (vc && *vc)
        return iv_parse_backup_method(vc);
    return IV_BACKUP_EXISTING;
}

static const char *file_basename(const char *path)
{
    const char *sl = strrchr(path, '/');

    return sl ? sl + 1 : path;
}

/* Highest N in dir for basename.~N~, or 0 if none. */
static int max_numbered_backup(const char *path)
{
    char dirbuf[PATH_MAX];
    const char *base = file_basename(path);
    const char *slash = strrchr(path, '/');
    const char *dir;
    size_t blen = strlen(base);
    DIR *d;
    struct dirent *e;
    int maxn = 0;

    if (slash)
    {
        size_t dlen = (size_t)(slash - path);

        if (dlen == 0)
        {
            dir = "/";
        }
        else
        {
            if (dlen >= sizeof(dirbuf))
                return 0;
            memcpy(dirbuf, path, dlen);
            dirbuf[dlen] = '\0';
            dir = dirbuf;
        }
    }
    else
        dir = ".";
    d = opendir(dir);
    if (!d)
        return 0;
    while ((e = readdir(d)))
    {
        const char *nm = e->d_name;
        const char *p;
        int n = 0;

        if (strncmp(nm, base, blen) != 0 || nm[blen] != '.' || nm[blen + 1] != '~')
            continue;
        p = nm + blen + 2;
        if (!isdigit((unsigned char)*p))
            continue;
        while (isdigit((unsigned char)*p))
        {
            n = n * 10 + (*p - '0');
            p++;
        }
        if (p[0] == '~' && p[1] == '\0' && n > maxn)
            maxn = n;
    }
    closedir(d);
    return maxn;
}

int iv_backup_file(const char *path, const IvOpts *opts)
{
    struct stat st;
    char dst[PATH_MAX];
    const char *suffix;
    int type;
    int n;

    if (!opts || opts->backup == IV_BACKUP_NONE)
        return 0;
    if (stat(path, &st) != 0)
        return (errno == ENOENT) ? 0 : -1;
    type = opts->backup;
    if (type == IV_BACKUP_EXISTING)
        type = max_numbered_backup(path) > 0 ? IV_BACKUP_NUMBERED
                                             : IV_BACKUP_SIMPLE;
    suffix = opts->backup_suffix;
    if (!suffix || !*suffix)
        suffix = getenv("SIMPLE_BACKUP_SUFFIX");
    if (!suffix || !*suffix)
        suffix = "~";
    if (type == IV_BACKUP_NUMBERED)
    {
        n = max_numbered_backup(path) + 1;
        if (snprintf(dst, sizeof(dst), "%s.~%d~", path, n) >= (int)sizeof(dst))
            return -1;
    }
    else if (snprintf(dst, sizeof(dst), "%s%s", path, suffix) >= (int)sizeof(dst))
        return -1;
    return iv_copy_file(path, dst);
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
    int wrote_new = 0;

    if (opts->backup != IV_BACKUP_NONE && !opts->to_stdout && !opts->dry_run)
    {
        if (iv_backup_file(filename, opts) != 0)
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
    if (!pat || !*pat)
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
        return -1;
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
        return -1;
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
        return -1;
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
        return -1;
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

#define IV_CHUNK (256 * 1024)

typedef struct
{
    FILE *in;
    char *buf;
    size_t cap;
    size_t len;
    size_t pos;
    int eof;
    int binary;
} IvBlk;

static int put_ul(FILE *out, const void *p, size_t n)
{
    if (!n)
        return 0;
    if (fwrite_unlocked(p, 1, n, out) == n)
        return 0;
    return (iv_out_status(out) == 1) ? 1 : -1;
}

static int blk_init(IvBlk *b, FILE *in)
{
    int fd;

    b->in = in;
    b->cap = IV_CHUNK;
    b->len = 0;
    b->pos = 0;
    b->eof = 0;
    b->binary = 0;
    b->buf = malloc(b->cap);
    if (!b->buf)
        return -1;
    fd = fileno(in);
    if (fd >= 0)
        (void)posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    return 0;
}

static void blk_free(IvBlk *b)
{
    free(b->buf);
    b->buf = NULL;
}

static int blk_refill(IvBlk *b)
{
    size_t keep = b->len - b->pos;
    size_t got;

    if (keep)
        memmove(b->buf, b->buf + b->pos, keep);
    b->len = keep;
    b->pos = 0;
    if (b->eof)
        return 0;
    if (b->len == b->cap)
    {
        size_t nc = b->cap * 2;
        char *nb = realloc(b->buf, nc);

        if (!nb)
            return -1;
        b->buf = nb;
        b->cap = nc;
    }
    got = fread_unlocked(b->buf + b->len, 1, b->cap - b->len, b->in);
    if (got && memchr(b->buf + b->len, 0, got))
        b->binary = 1;
    b->len += got;
    if (got == 0)
        b->eof = 1;
    if (ferror(b->in))
        return -1;
    return 0;
}

/* 1 = line, 0 = EOF, -1 = I/O, -2 = NUL in input. *line lives until next call. */
static int blk_line(IvBlk *b, const char **line, size_t *nlen)
{
    for (;;)
    {
        if (b->binary)
            return -2;
        if (b->pos < b->len)
        {
            char *nl = memchr(b->buf + b->pos, '\n', b->len - b->pos);

            if (nl)
            {
                *line = b->buf + b->pos;
                *nlen = (size_t)(nl - (b->buf + b->pos)) + 1;
                b->pos += *nlen;
                return 1;
            }
            if (b->eof)
            {
                if (b->pos < b->len)
                {
                    *line = b->buf + b->pos;
                    *nlen = b->len - b->pos;
                    b->pos = b->len;
                    return 1;
                }
                return 0;
            }
        }
        else if (b->eof)
            return 0;
        if (blk_refill(b) != 0)
            return -1;
    }
}

static int stream_subst_literal(FILE *in, FILE *out, const char *pat,
                                const char *repl, int global,
                                const char *filter, int *nrepl)
{
    IvBlk blk;
    size_t plen;
    size_t rlen;
    size_t flen;
    int total = 0;
    int pr = 0;
    int rc;

    if (nrepl)
        *nrepl = 0;
    if (!pat)
        pat = "";
    if (!repl)
        repl = "";
    plen = strlen(pat);
    rlen = strlen(repl);
    flen = (filter && *filter) ? strlen(filter) : 0;
    if (out != stdout)
        iv_enlarge_buf(out);
    if (blk_init(&blk, in) != 0)
        return -1;

    flockfile(out);
    for (;;)
    {
        const char *line;
        const char *hit;
        const char *cur;
        size_t nread;
        size_t left;
        int n = 0;

        rc = blk_line(&blk, &line, &nread);
        if (rc == 0)
            break;
        if (rc == -2)
        {
            funlockfile(out);
            blk_free(&blk);
            fprintf(stderr, "iv: refusing to edit binary file\n");
            return -1;
        }
        if (rc < 0)
        {
            funlockfile(out);
            blk_free(&blk);
            return -1;
        }
        if (flen && !memmem(line, nread, filter, flen))
        {
            pr = put_ul(out, line, nread);
            if (pr != 0)
                break;
            continue;
        }
        hit = (plen && nread) ? memmem(line, nread, pat, plen) : NULL;
        if (!plen || !hit)
        {
            pr = put_ul(out, line, nread);
            if (pr != 0)
                break;
            continue;
        }
        if (rlen == plen)
        {
            char *w = (char *)line;
            size_t rest = nread;

            while (hit)
            {
                memcpy((char *)hit, repl, rlen);
                n++;
                rest -= (size_t)(hit - w) + plen;
                w = (char *)hit + plen;
                if (!global || !rest)
                    break;
                hit = memmem(w, rest, pat, plen);
            }
            pr = put_ul(out, line, nread);
            if (pr != 0)
                break;
            total += n;
            continue;
        }
        cur = line;
        left = nread;
        while (hit)
        {
            pr = put_ul(out, cur, (size_t)(hit - cur));
            if (pr == 0)
                pr = put_ul(out, repl, rlen);
            if (pr != 0)
                goto done;
            n++;
            left -= (size_t)(hit - cur) + plen;
            cur = hit + plen;
            if (!global || !plen || !left)
                break;
            hit = memmem(cur, left, pat, plen);
        }
        pr = put_ul(out, cur, left);
        if (pr != 0)
            break;
        total += n;
    }
done:
    funlockfile(out);
    blk_free(&blk);
    if (pr != 0)
        return (pr > 0) ? 0 : -1;
    if (ferror(in))
        return -1;
    if (nrepl)
        *nrepl = total;
    return 0;
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
    for (i = 0; i < npairs; i++)
    {
        if (!pairs[i][0] || !pairs[i][0][0])
        {
            fprintf(stderr, "iv: empty pattern\n");
            return -1;
        }
    }
    if (!opts->use_regex && npairs == 1)
        return stream_subst_literal(in, out, pairs[0][0], pairs[0][1],
                                    opts->global_replace, opts->multimatch,
                                    nrepl);
    if (in != stdin)
        iv_enlarge_buf(in);
    if (out != stdout)
        iv_enlarge_buf(out);
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
        {
            int pr = owned ? iv_fputs(out, cur)
                           : iv_fwrite(out, line, (size_t)nread);

            if (pr != 0)
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
                return (pr > 0) ? 0 : -1;
            }
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
    IvBlk blk;
    size_t vlen;
    int pr = 0;
    int rc;

    if (!value)
        value = "";
    vlen = strlen(value);
    if (out != stdout)
        iv_enlarge_buf(out);
    if (blk_init(&blk, in) != 0)
        return -1;

    flockfile(out);
    for (;;)
    {
        const char *line;
        const char *p;
        const char *end;
        const char *field_start;
        size_t nread;
        int f;

        rc = blk_line(&blk, &line, &nread);
        if (rc == 0)
            break;
        if (rc == -2)
        {
            funlockfile(out);
            blk_free(&blk);
            fprintf(stderr, "iv: refusing to edit binary file\n");
            return -1;
        }
        if (rc < 0)
        {
            funlockfile(out);
            blk_free(&blk);
            return -1;
        }
        p = line;
        end = line + nread;
        field_start = line;
        f = 1;
        while (f < field_num && p < end)
        {
            const char *comma = memchr(p, delim, (size_t)(end - p));

            if (!comma)
                break;
            f++;
            p = comma + 1;
            field_start = p;
        }
        if (f != field_num)
            pr = put_ul(out, line, nread);
        else
        {
            const char *stop = memchr(p, delim, (size_t)(end - p));

            if (!stop)
            {
                /* Drop trailing newline from the field, keep it after value. */
                stop = end;
                if (stop > p && stop[-1] == '\n')
                    stop--;
            }
            pr = put_ul(out, line, (size_t)(field_start - line));
            if (pr == 0)
                pr = put_ul(out, value, vlen);
            if (pr == 0)
                pr = put_ul(out, stop, (size_t)(end - stop));
        }
        if (pr != 0)
            break;
    }
    funlockfile(out);
    blk_free(&blk);
    if (pr != 0)
        return (pr > 0) ? 0 : -1;
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
    {
        if (fprintf(out, "%4d | %s", lineno, line) >= 0)
            return 0;
        return (iv_out_status(out) == 1) ? 1 : -1;
    }
    return iv_fputs(out, line);
}

static int emit_repl(FILE *out, const char *text)
{
    if (!text)
        text = "";
    write_with_escapes(out, text);
    return iv_out_status(out);
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
            if (act)
            {
                int pr = emit_line(out, ring_at(r, k), lineno, !no_numbers);

                if (pr != 0)
                    return pr;
            }
        }
        else if (op == IV_STREAM_DELETE)
        {
            if (!act)
            {
                int pr = emit_line(out, ring_at(r, k), 0, 0);

                if (pr != 0)
                    return pr;
            }
        }
        else if (op == IV_STREAM_REPLACE)
        {
            int pr = act ? emit_repl(out, text)
                         : emit_line(out, ring_at(r, k), 0, 0);

            if (pr != 0)
                return pr;
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
                if (hit)
                {
                    int pr = emit_line(out, line, n, !no_numbers);

                    if (pr > 0)
                        goto done;
                    if (pr < 0)
                        goto fail;
                }
            }
            else if (op == IV_STREAM_DELETE)
            {
                if (!hit)
                {
                    int pr = emit_line(out, line, 0, 0);

                    if (pr > 0)
                        goto done;
                    if (pr < 0)
                        goto fail;
                }
            }
            else
            {
                int pr = hit ? emit_repl(out, text)
                             : emit_line(out, line, 0, 0);

                if (pr > 0)
                    goto done;
                if (pr < 0)
                    goto fail;
            }
            continue;
        }

        if (p->kind == IV_RANGE_HYBRID && n < p->start)
        {
            if (op == IV_STREAM_VIEW)
                continue;
            {
                int pr = emit_line(out, line, 0, 0);

                if (pr > 0)
                    goto done;
                if (pr < 0)
                    goto fail;
            }
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
            else
            {
                int pr = emit_line(out, evicted, 0, 0);

                free(evicted);
                if (pr > 0)
                    goto done;
                if (pr < 0)
                    goto fail;
            }
        }
        else
        {
            /* hybrid: evicted is inside the range */
            if (op == IV_STREAM_VIEW)
            {
                int pr = emit_line(out, evicted, n - hold, !no_numbers);

                free(evicted);
                if (pr > 0)
                    goto done;
                if (pr < 0)
                    goto fail;
            }
            else if (op == IV_STREAM_DELETE)
                free(evicted);
            else
            {
                int pr = emit_repl(out, text);

                free(evicted);
                if (pr > 0)
                    goto done;
                if (pr < 0)
                    goto fail;
            }
        }
    }
    free(line);
    line = NULL;

    if (p->kind == IV_RANGE_TAIL)
    {
        int pr = flush_tail(out, &ring, p, op, text, no_numbers, n);

        if (pr > 0)
            goto done;
        if (pr < 0)
            goto fail;
    }
    else if (p->kind == IV_RANGE_HYBRID)
    {
        int k;

        for (k = 0; k < ring.n; k++)
        {
            int pr;

            if (op == IV_STREAM_VIEW)
                continue;
            pr = emit_line(out, ring_at(&ring, k), 0, 0);
            if (pr > 0)
                goto done;
            if (pr < 0)
                goto fail;
        }
    }
    ring_free(&ring);
    return ferror(in) ? -1 : 0;

done:
    free(line);
    ring_free(&ring);
    return 0;

fail:
    free(line);
    ring_free(&ring);
    return -1;
}

int iv_stream_delete_match(FILE *in, FILE *out, const char *filter,
                           int use_regex)
{
    IvBlk blk;
    size_t flen = 0;
    regex_t re;
    int pr = 0;
    int rc = 0;

    if (out != stdout)
        iv_enlarge_buf(out);
    if (filter && *filter && use_regex)
    {
        char *line = NULL;
        size_t cap = 0;
        ssize_t nread;

        if (regcomp(&re, filter, REG_EXTENDED | REG_NOSUB) != 0)
            return -1;
        flockfile(out);
        while ((nread = getline(&line, &cap, in)) != -1)
        {
            if (memchr(line, 0, (size_t)nread))
            {
                funlockfile(out);
                free(line);
                regfree(&re);
                fprintf(stderr, "iv: refusing to edit binary file\n");
                return -1;
            }
            if (regexec(&re, line, 0, NULL, 0) == 0)
                continue;
            pr = put_ul(out, line, (size_t)nread);
            if (pr != 0)
                break;
        }
        funlockfile(out);
        free(line);
        regfree(&re);
        if (pr != 0)
            return (pr > 0) ? 0 : -1;
        return ferror(in) ? -1 : 0;
    }
    if (filter && *filter)
        flen = strlen(filter);
    if (blk_init(&blk, in) != 0)
        return -1;

    flockfile(out);
    for (;;)
    {
        const char *line;
        size_t nread;
        int keep;

        rc = blk_line(&blk, &line, &nread);
        if (rc == 0)
            break;
        if (rc == -2)
        {
            funlockfile(out);
            blk_free(&blk);
            fprintf(stderr, "iv: refusing to edit binary file\n");
            return -1;
        }
        if (rc < 0)
        {
            funlockfile(out);
            blk_free(&blk);
            return -1;
        }
        keep = flen ? (memmem(line, nread, filter, flen) == NULL) : 0;
        if (!keep)
            continue;
        pr = put_ul(out, line, nread);
        if (pr != 0)
            break;
    }
    funlockfile(out);
    blk_free(&blk);
    if (pr != 0)
        return (pr > 0) ? 0 : -1;
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
        {
            int pr = iv_fputs(out, line);

            if (pr != 0)
            {
                free(line);
                if (fp)
                    regfree(fp);
                return (pr > 0) ? 0 : -1;
            }
        }
    }
    free(line);
    if (fp)
        regfree(fp);
    return ferror(in) ? -1 : 0;
}
