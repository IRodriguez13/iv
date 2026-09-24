/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#include "iv.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * In-place commit (same-directory):
 *   open dirfd → exclusive temp via openat → write → fsync → renameat
 * A failed commit leaves the original path byte-for-byte intact.
 *
 * Symlinks: the referent is replaced; the symlink inode is kept.
 * Hardlinks: this pathname gets a new inode; other names keep the old bytes.
 * Mode and uid/gid of the referent are copied. xattrs/ACLs are not.
 *
 * Stdout EPIPE (downstream closed): not an error. SIGPIPE is ignored so the
 * process can exit 0 without a diagnostic.
 */

static volatile sig_atomic_t tmp_live;
static char tmp_guard[PATH_MAX];
static int stdout_closed;

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

static int basename_copy(const char *path, char *base, size_t basesz)
{
    const char *slash = strrchr(path, '/');
    const char *src = slash ? slash + 1 : path;

    if (!src[0] || strlen(src) >= basesz)
        return -1;
    memcpy(base, src, strlen(src) + 1);
    return 0;
}

static int resolve_commit_path(const char *path, char *out, size_t outsz,
                               struct stat *st_out, int *existed, int *dangling)
{
    struct stat lst;

    *dangling = 0;
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
        {
            *dangling = 1;
            return -1;
        }
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

static void report_io(const char *what)
{
    fprintf(stderr, "iv: %s: %s\n", what, errno ? strerror(errno) : "I/O error");
}

static void note_stdout_closed(void)
{
    stdout_closed = 1;
}

int iv_stdout_closed(void)
{
    return stdout_closed;
}

int iv_out_status(FILE *out)
{
    if (!out)
        return -1;
    if (!ferror(out))
        return 0;
    if (out == stdout && errno == EPIPE)
    {
        note_stdout_closed();
        return 1;
    }
    return -1;
}

int iv_fputs(FILE *out, const char *s)
{
    if (fputs(s, out) != EOF)
        return 0;
    return (iv_out_status(out) == 1) ? 1 : -1;
}

int iv_fwrite(FILE *out, const void *p, size_t n)
{
    if (!n)
        return 0;
    if (fwrite(p, 1, n, out) == n)
        return 0;
    return (iv_out_status(out) == 1) ? 1 : -1;
}

void iv_enlarge_buf(FILE *f)
{
    if (!f || f == stderr)
        return;
    (void)setvbuf(f, NULL, _IOFBF, 256 * 1024);
}

int iv_check_stream(FILE *f)
{
    int save;

    if (!f)
        return -1;
    if (f == stdout && (stdout_closed || (ferror(f) && errno == EPIPE)))
    {
        note_stdout_closed();
        return 0;
    }
    if (fflush(f) == 0 && !ferror(f))
        return 0;
    save = errno;
    if (f == stdout && save == EPIPE)
    {
        note_stdout_closed();
        return 0;
    }
    errno = save;
    return -1;
}

static void on_fatal_signal(int sig)
{
    if (tmp_live)
        unlink(tmp_guard);
    signal(sig, SIG_DFL);
    raise(sig);
}

void iv_init_stdio(void)
{
    struct sigaction sa;

    signal(SIGPIPE, SIG_IGN);
    if (!isatty(STDOUT_FILENO))
        iv_enlarge_buf(stdout);
    if (!isatty(STDIN_FILENO))
        iv_enlarge_buf(stdin);
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_fatal_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

static void commit_tmp_set(const char *path)
{
    size_t n = strlen(path);

    if (n >= sizeof(tmp_guard))
        return;
    memcpy(tmp_guard, path, n + 1);
    tmp_live = 1;
}

static void commit_tmp_clear(void)
{
    tmp_live = 0;
}

static int open_tmp_at(int dirfd, char *name, size_t namesz)
{
    unsigned int seed = (unsigned)getpid() ^ (unsigned)(uintptr_t)&seed;
    int i, fd;

    for (i = 0; i < 256; i++)
    {
        seed = seed * 1664525u + 1013904223u + (unsigned)i;
        {
            int n = snprintf(name, namesz, ".iv.%08x", seed);

            if (n < 0 || (size_t)n >= namesz)
                return -1;
        }
        fd = openat(dirfd, name, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0 || errno != EEXIST)
            return fd;
    }
    errno = EEXIST;
    return -1;
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

static int write_lines_cb(FILE *out, void *ctx)
{
    struct
    {
        char **lines;
        int count;
    } *c = ctx;
    int i, pr;

    for (i = 0; i < c->count; i++)
    {
        pr = iv_fputs(out, c->lines[i]);
        if (pr != 0)
            return (pr > 0) ? 0 : -1;
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
    char base[PATH_MAX];
    char tmpname[64];
    char tmpfull[PATH_MAX];
    struct stat st;
    int existed = 0, dangling = 0;
    int dirfd = -1;
    int fd = -1;
    FILE *out = NULL;
    int wr;
    mode_t mask;

    if (!path || !write_fn)
    {
        errno = EINVAL;
        return -1;
    }
    if (resolve_commit_path(path, real, sizeof(real), &st, &existed, &dangling) != 0)
    {
        if (dangling)
            fprintf(stderr, "iv: dangling symlink: %s\n", path);
        else
            perror(path);
        return -1;
    }
    if (dirname_copy(real, dir, sizeof(dir)) != 0 ||
        basename_copy(real, base, sizeof(base)) != 0)
        return -1;

    dirfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd < 0)
    {
        report_io("open directory");
        return -1;
    }
    fd = open_tmp_at(dirfd, tmpname, sizeof(tmpname));
    if (fd < 0)
    {
        report_io("mkstemp");
        close(dirfd);
        return -1;
    }
    if ((size_t)snprintf(tmpfull, sizeof(tmpfull), "%s/%s", dir, tmpname) >= sizeof(tmpfull))
    {
        close(fd);
        unlinkat(dirfd, tmpname, 0);
        close(dirfd);
        return -1;
    }
    commit_tmp_set(tmpfull);

    if (existed)
    {
        if (fchmod(fd, st.st_mode & 07777) != 0)
        {
            report_io("fchmod");
            goto fail;
        }
        if (fchown(fd, st.st_uid, st.st_gid) != 0 && errno != EPERM)
        {
            report_io("fchown");
            goto fail;
        }
    }
    else
    {
        mask = umask(0);
        umask(mask);
        if (fchmod(fd, 0666 & ~mask) != 0)
        {
            report_io("fchmod");
            goto fail;
        }
    }

    out = fdopen(fd, "w");
    if (!out)
    {
        report_io("fdopen");
        close(fd);
        goto fail_opened;
    }
    fd = -1;
    iv_enlarge_buf(out);

    wr = write_fn(out, ctx);
    if (wr > 0)
    {
        fclose(out);
        unlinkat(dirfd, tmpname, 0);
        commit_tmp_clear();
        close(dirfd);
        return 0;
    }
    if (wr != 0 || fflush(out) != 0 || ferror(out) || fsync(fileno(out)) != 0)
    {
        if (wr == 0)
            report_io("write");
        fclose(out);
        goto fail_opened;
    }
    if (fclose(out) != 0)
    {
        report_io("fclose");
        goto fail_opened;
    }
    out = NULL;

    if (existed)
    {
        struct stat now;

        if (fstatat(dirfd, base, &now, AT_SYMLINK_NOFOLLOW) != 0)
        {
            report_io("destination vanished");
            goto fail_opened;
        }
        if (now.st_dev != st.st_dev || now.st_ino != st.st_ino)
        {
            fprintf(stderr, "iv: destination changed during write\n");
            goto fail_opened;
        }
    }
    if (renameat(dirfd, tmpname, dirfd, base) != 0)
    {
        report_io("rename");
        goto fail_opened;
    }
    commit_tmp_clear();
    (void)fsync(dirfd);
    close(dirfd);
    return 0;

fail:
    if (fd >= 0)
        close(fd);
fail_opened:
    unlinkat(dirfd, tmpname, 0);
    commit_tmp_clear();
    if (dirfd >= 0)
        close(dirfd);
    return -1;
}

int write_lines_to_file(const char *filename, char *lines[], int count)
{
    return iv_commit_lines(filename, lines, count);
}

int write_lines_to_stream(FILE *f, char *lines[], int count)
{
    int i, pr;

    for (i = 0; i < count; i++)
    {
        pr = iv_fputs(f, lines[i]);
        if (pr > 0)
            return 0;
        if (pr < 0)
            return -1;
    }
    return iv_check_stream(f);
}
