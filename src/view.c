/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#include "iv.h"

static int emit_view(const char *line, int n, int no_numbers)
{
    if (no_numbers)
        return iv_fputs(stdout, line);
    if (printf("%4d | %s", n, line) >= 0)
        return 0;
    return (iv_out_status(stdout) == 1) ? 1 : -1;
}

int stream_show_file(FILE *f, int no_numbers)
{
    char *line = NULL;
    size_t cap = 0;
    int n = 0;

    while (getline(&line, &cap, f) != -1)
    {
        int pr;

        n++;
        pr = emit_view(line, n, no_numbers);
        if (pr > 0)
        {
            free(line);
            return 0;
        }
        if (pr < 0)
        {
            free(line);
            return -1;
        }
    }
    free(line);
    return ferror(f) ? -1 : 0;
}
