/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#include "iv.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * In-place commit:
 *   write temp in the same directory → fflush/ferror/fsync → rename
 * A failed commit leaves the original path byte-for-byte intact.
 *
 * Symlinks: the referent is replaced; the symlink inode is kept.
 * Hardlinks: this pathname gets a new inode; other names keep the old bytes.
 * Mode and uid/gid of the referent are copied. xattrs/ACLs are not.
 */

static int dirname_copy(const char *path, char *dir, size_t dirsz)
{
    const char *slash = strrchr(path, '/');

    if (!slash)
    {
        if (dirsz < 2)
            return -1;
        dir[0] = '.';
        dir[1] = '\0';
        return 0;
    }
    if (slash == path)
    {
        if (dirsz < 2)
            return -1;
        dir[0] = '/';
        dir[1] = '\0';
        return 0;
    }
    {
        size_t n = (size_t)(slash - path);
        if (n >= dirsz)
            return -1;
        memcpy(dir, path, n);
        dir[n] = '\0';
    }
    return 0;
}

static int resolve_commit_path(const char *path, char *out, size_t outsz,
                               struct stat *st_out, int *existed)
{
    struct stat lst;

    if (!path || !*path)
    {
        errno = EINVAL;
        return -1;
    }
    if (lstat(path, &lst) != 0)
    {
        if (errno != ENOENT)
            return -1;
        *existed = 0;
        if (strlen(path) >= outsz)
            return -1;
        memcpy(out, path, strlen(path) + 1);
        return 0;
    }
    *existed = 1;
    if (S_ISLNK(lst.st_mode))
    {
        if (!realpath(path, out))
            return -1;
        if (stat(out, st_out) != 0)
            return -1;
        if (!S_ISREG(st_out->st_mode))
        {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }
    if (!S_ISREG(lst.st_mode))
    {
        errno = EINVAL;
        return -1;
    }
    if (strlen(path) >= outsz)
        return -1;
    memcpy(out, path, strlen(path) + 1);
    *st_out = lst;
    return 0;
}

static void fsync_dir_best_effort(const char *dir)
{
    int dfd = open(dir, O_RDONLY | O_DIRECTORY);

    if (dfd < 0)
        return;
    (void)fsync(dfd);
    close(dfd);
}

static void report_io(const char *what)
{
    fprintf(stderr, "iv: %s: %s\n", what, errno ? strerror(errno) : "I/O error");
}

int iv_check_stream(FILE *f)
{
    if (!f)
        return -1;
    if (fflush(f) != 0)
        return -1;
    if (ferror(f))
        return -1;
    return 0;
}

int iv_copy_file(const char *src, const char *dst)
{
    FILE *in;
    FILE *out;
    char buf[8192];
    size_t n;
    int err = 0;
    int fd;

    in = fopen(src, "rb");
    if (!in)
        return -1;
    out = fopen(dst, "wb");
    if (!out)
    {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
        {
            err = 1;
            break;
        }
    }
    if (!err && ferror(in))
        err = 1;
    if (!err && fflush(out) != 0)
        err = 1;
    if (!err && ferror(out))
        err = 1;
    fd = fileno(out);
    if (!err && fd >= 0 && fsync(fd) != 0)
        err = 1;
    if (fclose(out) != 0)
        err = 1;
    fclose(in);
    if (err)
    {
        unlink(dst);
        return -1;
    }
    return 0;
}

static int copy_write(FILE *out, void *ctx)
{
    const char *src = ctx;
    FILE *in = fopen(src, "rb");
    char buf[8192];
    size_t n;
    int err = 0;

    if (!in)
        return -1;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
        {
            err = 1;
            break;
        }
    }
    if (!err && ferror(in))
        err = 1;
    fclose(in);
    return err ? -1 : 0;
}

int iv_restore_file(const char *src, const char *dst)
{
    return iv_commit_stream(dst, copy_write, (void *)src);
}

static int write_lines_cb(FILE *out, void *ctx)
{
    struct
    {
        char **lines;
        int count;
    } *c = ctx;
    int i;

    for (i = 0; i < c->count; i++)
    {
        if (fputs(c->lines[i], out) == EOF)
            return -1;
    }
    return 0;
}

int iv_commit_lines(const char *path, char *lines[], int count)
{
    struct
    {
        char **lines;
        int count;
    } ctx = {lines, count};

    return iv_commit_stream(path, write_lines_cb, &ctx);
}

int iv_commit_stream(const char *path, IvWriteFn write_fn, void *ctx)
{
    char real[PATH_MAX];
    char dir[PATH_MAX];
    char tmp[PATH_MAX];
    struct stat st;
    int existed = 0;
    int fd = -1;
    FILE *out = NULL;
    int wr;
    mode_t mask;

    if (!path || !write_fn)
    {
        errno = EINVAL;
        return -1;
    }
    if (resolve_commit_path(path, real, sizeof(real), &st, &existed) != 0)
    {
        perror(path);
        return -1;
    }
    if (dirname_copy(real, dir, sizeof(dir)) != 0)
        return -1;
    if ((size_t)snprintf(tmp, sizeof(tmp), "%s/.iv.XXXXXX", dir) >= sizeof(tmp))
        return -1;

    fd = mkstemp(tmp);
    if (fd < 0)
    {
        report_io("mkstemp");
        return -1;
    }
    if (existed)
    {
        if (fchmod(fd, st.st_mode & 07777) != 0)
        {
            report_io("fchmod");
            close(fd);
            unlink(tmp);
            return -1;
        }
        if (fchown(fd, st.st_uid, st.st_gid) != 0 && errno != EPERM)
        {
            report_io("fchown");
            close(fd);
            unlink(tmp);
            return -1;
        }
    }
    else
    {
        mask = umask(0);
        umask(mask);
        if (fchmod(fd, 0666 & ~mask) != 0)
        {
            report_io("fchmod");
            close(fd);
            unlink(tmp);
            return -1;
        }
    }

    out = fdopen(fd, "w");
    if (!out)
    {
        report_io("fdopen");
        close(fd);
        unlink(tmp);
        return -1;
    }

    wr = write_fn(out, ctx);
    if (wr > 0)
    {
        fclose(out);
        unlink(tmp);
        return 0;
    }
    if (wr != 0 || fflush(out) != 0 || ferror(out) || fsync(fd) != 0)
    {
        if (wr == 0)
            report_io("write");
        fclose(out);
        unlink(tmp);
        return -1;
    }
    if (fclose(out) != 0)
    {
        report_io("fclose");
        unlink(tmp);
        return -1;
    }
    /* fclose closed fd */

    if (rename(tmp, real) != 0)
    {
        report_io("rename");
        unlink(tmp);
        return -1;
    }
    fsync_dir_best_effort(dir);
    return 0;
}

int write_lines_to_file(const char *filename, char *lines[], int count)
{
    return iv_commit_lines(filename, lines, count);
}

int write_lines_to_stream(FILE *f, char *lines[], int count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        if (fputs(lines[i], f) == EOF)
            return -1;
    }
    return iv_check_stream(f);
}
