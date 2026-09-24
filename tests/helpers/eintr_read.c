/* SPDX-License-Identifier: GPL-3.0-or-later */
/* LD_PRELOAD: first few read()s fail with EINTR, then pass through. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>

static int n;

ssize_t read(int fd, void *buf, size_t count)
{
    static ssize_t (*real_read)(int, void *, size_t);

    if (!real_read)
    {
        real_read = (ssize_t (*)(int, void *, size_t))dlsym(RTLD_NEXT, "read");
        if (!real_read)
            return -1;
    }
    if (n < 4)
    {
        n++;
        errno = EINTR;
        return -1;
    }
    return real_read(fd, buf, count);
}
