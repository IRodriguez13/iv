/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#include "iv.h"
#include <ctype.h>

/* Parse range: "1-5", "-3--1", "-5-", "5", "-2"
 * count = total lines. start,end are 1-based.
 * -1 means "from start", -2 means "2nd from end", etc.
 * Returns 0 on success, -1 on error.
 */
int parse_range(const char *spec, int count, int *start, int *end)
{
    if (!spec || !*spec)
        return -1;

    const char *p = spec;
    int s = 0, e = 0;
    int s_neg = 0, e_neg = 0;

    /* Parse start */
    if (*p == '-')
    {
        p++;
        s_neg = 1;
        if (!*p)
            return -1;
    }
    while (*p && isdigit(*p))
    {
        s = s * 10 + (*p - '0');
        p++;
    }

    if (!*p)
    {
        /* Single number: "5" or "-3" */
        if (s_neg)
        {
            *start = count - s + 1;
            *end = *start;
        }
        else
        {
            *start = s;
            *end = s;
        }
        if (*start < 1)
            *start = 1;
        if (*end > count)
            *end = count;
        
        return 0;
    }

    if (*p != '-')
        return -1;
    p++;

    /* Parse end */
    if (*p == '-')
    {
        p++;
        e_neg = 1;
    }
    while (*p && isdigit(*p))
    {
        e = e * 10 + (*p - '0');
        p++;
    }
    if (*p)
        return -1;

    if (s_neg)
        *start = (s == 0) ? 1 : count - s + 1;
    else
        *start = s;

    if (e_neg)
        *end = (e == 0) ? count : count - e + 1;
    else
        *end = e;

    if (*start < 1)
        *start = 1;
    if (*end > count)
        *end = count;
    if (*start > *end)
    {
        int t = *start;
        *start = *end;
        *end = t;
    }
    return 0;
}
