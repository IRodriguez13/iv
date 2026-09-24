/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#include "iv.h"
#include <ctype.h>

/* Parse "1-5", "-3--1", "-5-", "5", "-2", "2-", "2--2"
 * into raw parts. Returns 0 on success, -1 on error.
 */
static int parse_range_parts(const char *spec, int *s, int *s_neg,
                             int *e, int *e_neg, int *single, int *open_end)
{
    const char *p;

    if (!spec || !*spec)
        return -1;

    p = spec;
    *s = 0;
    *e = 0;
    *s_neg = 0;
    *e_neg = 0;
    *single = 0;
    *open_end = 0;

    if (*p == '-')
    {
        p++;
        *s_neg = 1;
        if (!*p)
            return -1;
    }
    if (!isdigit((unsigned char)*p))
        return -1;
    while (*p && isdigit((unsigned char)*p))
    {
        *s = *s * 10 + (*p - '0');
        p++;
    }

    if (!*p)
    {
        *single = 1;
        *e = *s;
        *e_neg = *s_neg;
        return 0;
    }
    if (*p != '-')
        return -1;
    p++;
    if (!*p)
    {
        *open_end = 1;
        return 0;
    }
    if (*p == '-')
    {
        p++;
        *e_neg = 1;
        if (!*p)
            return -1;
    }
    if (!isdigit((unsigned char)*p))
        return -1;
    while (*p && isdigit((unsigned char)*p))
    {
        *e = *e * 10 + (*p - '0');
        p++;
    }
    if (*p)
        return -1;
    return 0;
}

int range_needs_total(const char *spec)
{
    int s, s_neg, e, e_neg, single, open_end;

    if (parse_range_parts(spec, &s, &s_neg, &e, &e_neg, &single, &open_end) != 0)
        return 0;
    if (s_neg)
        return 1;
    if (e_neg && e > 1)
        return 1;
    return 0;
}

int plan_range(const char *spec, IvRangePlan *p)
{
    int s, s_neg, e, e_neg, single, open_end;

    if (!p)
        return -1;
    memset(p, 0, sizeof(*p));
    if (parse_range_parts(spec, &s, &s_neg, &e, &e_neg, &single, &open_end) != 0)
        return -1;

    if (s_neg && s < 1)
        return -1;

    if (single && !s_neg)
    {
        p->kind = IV_RANGE_FORWARD;
        p->start = s;
        p->end = s;
        return 0;
    }
    if (single && s_neg)
    {
        p->kind = IV_RANGE_TAIL;
        p->window = s;
        p->from = 0;
        p->to = 0;
        return 0;
    }
    if (open_end)
    {
        if (s_neg)
        {
            p->kind = IV_RANGE_TAIL;
            p->window = s;
            p->from = 0;
            p->to = s - 1;
            return 0;
        }
        p->kind = IV_RANGE_FORWARD;
        p->start = s < 1 ? 1 : s;
        p->end = -1;
        return 0;
    }
    if (!s_neg && e_neg && e == 1)
    {
        p->kind = IV_RANGE_FORWARD;
        p->start = s < 1 ? 1 : s;
        p->end = -1;
        return 0;
    }
    if (!s_neg && e_neg && e > 1)
    {
        p->kind = IV_RANGE_HYBRID;
        p->start = s < 1 ? 1 : s;
        p->hold = e - 1;
        return 0;
    }
    if (s_neg && e_neg)
    {
        int w = s > e ? s : e;

        if (e < 1)
            return -1;
        p->kind = IV_RANGE_TAIL;
        p->window = w;
        p->from = w - s;
        p->to = w - e;
        if (p->from > p->to)
        {
            int t = p->from;
            p->from = p->to;
            p->to = t;
        }
        return 0;
    }
    if (!s_neg && !e_neg)
    {
        p->kind = IV_RANGE_FORWARD;
        p->start = s < 1 ? 1 : s;
        p->end = e;
        if (p->end >= 0 && p->start > p->end)
        {
            int t = p->start;
            p->start = p->end;
            p->end = t;
        }
        return 0;
    }
    return -1;
}

int parse_range(const char *spec, int count, int *start, int *end)
{
    IvRangePlan p;
    int a, b;

    if (plan_range(spec, &p) != 0)
        return -1;

    if (p.kind == IV_RANGE_FORWARD)
    {
        a = p.start;
        b = (p.end < 0) ? count : p.end;
    }
    else if (p.kind == IV_RANGE_TAIL)
    {
        a = count - p.window + 1 + p.from;
        b = count - p.window + 1 + p.to;
    }
    else
    {
        a = p.start;
        b = count - p.hold;
    }

    if (a < 1)
        a = 1;
    if (b > count)
        b = count;
    if (a > b)
    {
        *start = 1;
        *end = 0;
        return 0;
    }
    *start = a;
    *end = b;
    return 0;
}
