/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#include "iv.h"
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <sys/stat.h>

static void usage(FILE *out, const char *prog)
{
    fprintf(out, "Usage: %s COMMAND FILE [ARGS]\n", prog);
    fprintf(out, "       %s -h | -V\n", prog);
    fputs("\n"
          "Commands:\n"
          "  -v  FILE                 print with line numbers\n"
          "  -va RANGE FILE           print a line range\n"
          "  -s  FILE PAT REPL        substitute (first match per line)\n"
          "  -d  FILE [RANGE]         delete lines\n"
          "  -r  FILE [RANGE] TEXT    replace lines\n"
          "  -i  FILE [RANGE] TEXT    insert before RANGE start (else append)\n"
          "  -a  FILE TEXT            append\n"
          "  -p  FILE... [RANGE] TEXT replace RANGE (else append)\n"
          "  -pi FILE... LINE TEXT    insert before LINE\n"
          "\n"
          "Options:\n"
          "  -m PAT        only matching lines (-s, -d, -r)\n"
          "  -e PAT REPL   extra substitute pair\n"
          "  -F D N VAL    replace field N (byte delimiter)\n"
          "  -E            POSIX ERE for -s and -m\n"
          "  -g            every match on the line\n"
          "  -q            no tee / no Replaced N\n"
          "  --stdout      write result to stdout\n"
          "  --dry-run     print result; do not write\n"
          "  -b            GNU backup (existing)\n"
          "  --backup[=M]  GNU backup (none|numbered|existing|simple)\n"
          "  -S SUFFIX     backup suffix (also enables backup)\n"
          "  --no-numbers  no line numbers (-v, -va)\n"
          "\n"
          "Text: \"-\" = stdin, existing path = file, else literal.\n"
          "Ranges: 1-5, -3--1, -5-, 2-.\n",
          out);
}

static int parse_opts(int argc, char *argv[], IvOpts *opts)
{
    *opts = (IvOpts){0};
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "--dry-run") == 0)
            opts->dry_run = 1;
        else if (strcmp(argv[i], "-b") == 0)
            opts->backup = IV_BACKUP_EXISTING;
        else if (strcmp(argv[i], "--backup") == 0)
        {
            int t = iv_backup_from_env();

            if (t < 0)
            {
                fprintf(stderr, "iv: invalid VERSION_CONTROL\n");
                return -1;
            }
            opts->backup = t;
        }
        else if (strncmp(argv[i], "--backup=", 9) == 0)
        {
            int t = iv_parse_backup_method(argv[i] + 9);

            if (t < 0)
            {
                fprintf(stderr, "iv: invalid --backup method\n");
                return -1;
            }
            opts->backup = t;
        }
        else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc)
        {
            opts->backup_suffix = argv[++i];
            if (opts->backup == IV_BACKUP_NONE)
            {
                int t = iv_backup_from_env();

                opts->backup = (t < 0) ? IV_BACKUP_EXISTING : t;
            }
        }
        else if (strcmp(argv[i], "--suffix") == 0 && i + 1 < argc)
        {
            opts->backup_suffix = argv[++i];
            if (opts->backup == IV_BACKUP_NONE)
            {
                int t = iv_backup_from_env();

                opts->backup = (t < 0) ? IV_BACKUP_EXISTING : t;
            }
        }
        else if (strncmp(argv[i], "--suffix=", 9) == 0)
        {
            opts->backup_suffix = argv[i] + 9;
            if (opts->backup == IV_BACKUP_NONE)
            {
                int t = iv_backup_from_env();

                opts->backup = (t < 0) ? IV_BACKUP_EXISTING : t;
            }
        }
        else if (strcmp(argv[i], "--no-numbers") == 0)
            opts->no_numbers = 1;
        else if (strcmp(argv[i], "-g") == 0)
            opts->global_replace = 1;
        else if (strcmp(argv[i], "-E") == 0 ||
                 strcmp(argv[i], "--regex") == 0)
            opts->use_regex = 1;
        else if (strcmp(argv[i], "-q") == 0)
            opts->quiet = 1;
        else if (strcmp(argv[i], "--stdout") == 0)
            opts->to_stdout = 1;
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
            opts->multimatch = argv[++i];
        else if (strcmp(argv[i], "-F") == 0 && i + 2 < argc)
        {
            opts->field_delim = argv[i + 1][0];
            opts->field_num = atoi(argv[i + 2]);
            i += 2;
        }
    }
    return 0;
}

/* Returns 1 if argv[i] is a flag (i.e., not a positional argument). */
static int is_flag(const char *s)
{
    return strcmp(s, "--dry-run") == 0 ||
           strcmp(s, "-b") == 0 ||
           strcmp(s, "--backup") == 0 ||
           strncmp(s, "--backup=", 9) == 0 ||
           strcmp(s, "-S") == 0 ||
           strcmp(s, "--suffix") == 0 ||
           strncmp(s, "--suffix=", 9) == 0 ||
           strcmp(s, "--no-numbers") == 0 ||
           strcmp(s, "-g") == 0 ||
           strcmp(s, "-E") == 0 ||
           strcmp(s, "--regex") == 0 ||
           strcmp(s, "-q") == 0 ||
           strcmp(s, "--stdout") == 0 ||
           strcmp(s, "-e") == 0 ||
           strcmp(s, "-m") == 0 ||
           strcmp(s, "-F") == 0;
}

/* Index of the next positional argument starting at i (inclusive). */
static int next_arg(int argc, char *argv[], int i)
{
    for (; i < argc; i++)
        if (!is_flag(argv[i]))
            return i;
    return -1;
}

/* Collects all positional argument indices from start.
 * Returns a heap array (caller frees) and writes count to *n. */
static int *collect_args(int argc, char *argv[], int start, int *n)
{
    int cap = 8, count = 0;
    int *arr = malloc(cap * sizeof(int));
    if (!arr)
        return NULL;
    for (int i = start; i < argc; i++)
    {
        /* -m and -F consume the following token(s); skip them */
        if (strcmp(argv[i], "-m") == 0)
        {
            i++;
            continue;
        }
        if (strcmp(argv[i], "-F") == 0)
        {
            i += 2;
            continue;
        }
        if (strcmp(argv[i], "-e") == 0)
        {
            i += 2;
            continue;
        }
        if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--suffix") == 0)
        {
            i++;
            continue;
        }
        if (is_flag(argv[i]))
            continue;
        if (count >= cap)
        {
            cap *= 2;
            int *tmp = realloc(arr, cap * sizeof(int));
            if (!tmp)
            {
                free(arr);
                return NULL;
            }
            arr = tmp;
        }
        arr[count++] = i;
    }
    *n = count;
    return arr;
}

char *read_stdin(void)
{
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;
    while (fgets(buf + len, (int)(cap - len), stdin))
    {
        len += strlen(buf + len);
        if (len + 1 >= cap)
        {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp)
            {
                free(buf);
                return NULL;
            }
            buf = tmp;
        }
    }
    return buf;
}

char *read_file_content(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    while (fgets(buf + len, (int)(cap - len), f))
    {
        len += strlen(buf + len);
        if (len + 1 >= cap)
        {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp)
            {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = tmp;
        }
    }
    fclose(f);
    return buf;
}

static char *resolve_text(const char *arg)
{
    if (!arg || !*arg)
        return strdup("");
    if (strcmp(arg, "-") == 0)
        return read_stdin();
    char *content = read_file_content(arg);
    if (content)
        return content;
    return strdup(arg);
}

struct SubstCommit
{
    const char *path;
    const char *(*pairs)[2];
    int npairs;
    const IvOpts *opts;
    int *nrepl;
};

struct FieldCommit
{
    const char *path;
    char delim;
    int field_num;
    const char *value;
};

static int subst_commit_write(FILE *out, void *v)
{
    struct SubstCommit *c = v;
    FILE *in = fopen(c->path, "r");
    int rc;

    if (!in)
        return -1;
    rc = iv_stream_subst(in, out, c->pairs, c->npairs, c->opts, c->nrepl);
    fclose(in);
    if (rc != 0)
        return -1;
    if (c->nrepl && *c->nrepl == 0)
        return 1;
    if (iv_backup_file(c->path, c->opts) != 0)
    {
        fprintf(stderr, "iv: backup failed, aborting (original unchanged)\n");
        return -1;
    }
    return 0;
}

static int field_commit_write(FILE *out, void *v)
{
    struct FieldCommit *c = v;
    FILE *in = fopen(c->path, "r");
    int rc;

    if (!in)
        return -1;
    rc = iv_stream_fields(in, out, c->delim, c->field_num, c->value);
    fclose(in);
    return rc;
}

struct PlanCommit
{
    const char *path;
    IvRangePlan plan;
    int op;
    const char *text;
    const char *filter;
    int use_regex;
    const IvOpts *opts;
};

static int plan_commit_write(FILE *out, void *v)
{
    struct PlanCommit *c = v;
    FILE *in = fopen(c->path, "r");
    int rc;

    if (!in)
        return -1;
    if (c->filter)
    {
        rc = (c->op == IV_STREAM_REPLACE)
                 ? iv_stream_replace_match(in, out, c->filter, c->use_regex, c->text)
                 : iv_stream_delete_match(in, out, c->filter, c->use_regex);
    }
    else
        rc = iv_stream_by_plan(in, out, &c->plan, c->op, c->text, 1);
    fclose(in);
    if (rc != 0)
        return -1;
    if (iv_backup_file(c->path, c->opts) != 0)
    {
        fprintf(stderr, "iv: backup failed, aborting (original unchanged)\n");
        return -1;
    }
    return 0;
}

static int plan_is_single(const IvRangePlan *p)
{
    if (p->kind == IV_RANGE_FORWARD)
        return p->end >= 0 && p->start == p->end;
    if (p->kind == IV_RANGE_TAIL)
        return p->from == p->to;
    return 0;
}

static FILE *open_edit_src(const char *path, int create)
{
    struct stat st;
    FILE *f;

    if (strcmp(path, "-") == 0)
        return stdin;
    if (lstat(path, &st) == 0 && !S_ISLNK(st.st_mode) && !S_ISREG(st.st_mode))
    {
        fprintf(stderr, "iv: not a regular file: %s\n", path);
        return NULL;
    }
    f = fopen(path, "r");
    if (!f && create)
    {
        f = fopen(path, "w");
        if (f)
        {
            fclose(f);
            f = fopen(path, "r");
        }
    }
    if (!f)
        perror(path);
    return f;
}

static int finish_stream_edit(const char *path, FILE **src, struct PlanCommit *job,
                              const IvOpts *opts, char *new_text, int tee)
{
    int rc;
    FILE *out;

    job->path = path;
    job->opts = opts;
    job->text = new_text;
    job->filter = NULL;

    if (opts->dry_run || opts->to_stdout)
    {
        out = opts->dry_run ? fopen("/dev/null", "w") : stdout;
        if (!out)
        {
            perror("/dev/null");
            return -1;
        }
        rc = iv_stream_by_plan(*src, out, &job->plan, job->op, job->text, 1);
        if (!opts->dry_run && iv_check_stream(out) != 0)
            rc = -1;
        if (opts->dry_run)
            fclose(out);
    }
    else
    {
        if (*src && *src != stdin)
            fclose(*src);
        *src = NULL;
        rc = iv_commit_stream(path, plan_commit_write, job);
    }
    if (rc == 0 && tee && !opts->dry_run && !opts->quiet)
    {
        printf("%s", new_text);
        if (new_text[0] && new_text[strlen(new_text) - 1] != '\n')
            putchar('\n');
    }
    return rc;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "C");
    iv_init_stdio();

    if (argc < 2)
    {
        usage(stderr, argv[0]);
        return 1;
    }

    const char *flag = argv[1];

    if (strcmp(flag, "-h") == 0 || strcmp(flag, "--help") == 0)
    {
        usage(stdout, argv[0]);
        return 0;
    }
    if (strcmp(flag, "-V") == 0 || strcmp(flag, "--version") == 0)
    {
        printf("iv %s\n", IV_VERSION);
        printf("Copyright (C) 2026 Iván Ezequiel Rodriguez\n");
        printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
        printf("This is free software: you are free to change and redistribute it.\n");
        printf("There is NO WARRANTY, to the extent permitted by law.\n");
        printf("\nWritten by Iván Ezequiel Rodriguez.\n");
        return 0;
    }

    IvOpts opts;
    if (parse_opts(argc, argv, &opts) != 0)
        return 1;

    if (argc < 3)
    {
        usage(stderr, argv[0]);
        return 1;
    }

    /* Determine the main filename (for -va the order is different) */
    const char *filename;
    if (strcmp(flag, "-va") == 0 && argc >= 4)
    {
        int ri = next_arg(argc, argv, 2);
        int fi = (ri >= 0) ? next_arg(argc, argv, ri + 1) : -1;
        filename = (fi >= 0) ? argv[fi] : argv[2];
    }
    else
    {
        filename = argv[2];
    }

    if (strcmp(filename, "-") == 0)
        opts.to_stdout = 1;

    /* ── Streamable views and substitute (O(line) memory) ── */
    if (strcmp(flag, "-v") == 0 || strcmp(flag, "-va") == 0 ||
        strcmp(flag, "-s") == 0 ||
        strcmp(flag, "-d") == 0 || strcmp(flag, "-delete") == 0 ||
        strcmp(flag, "-r") == 0 || strcmp(flag, "-replace") == 0)
    {
        FILE *src;
        int rc = 0;

        if (strcmp(filename, "-") == 0)
            src = stdin;
        else
        {
            /* Edits must not open FIFOs/devices (fopen would block or clobber). */
            if (strcmp(flag, "-s") == 0 ||
                strcmp(flag, "-d") == 0 || strcmp(flag, "-delete") == 0 ||
                strcmp(flag, "-r") == 0 || strcmp(flag, "-replace") == 0)
            {
                struct stat st;

                if (lstat(filename, &st) == 0 && !S_ISLNK(st.st_mode) &&
                    !S_ISREG(st.st_mode))
                {
                    fprintf(stderr, "iv: not a regular file: %s\n", filename);
                    return 1;
                }
            }
            src = fopen(filename, "r");
            if (!src)
            {
                perror(filename);
                return 1;
            }
        }

        if (strcmp(flag, "-v") == 0)
        {
            rc = stream_show_file(src, opts.no_numbers);
        }
        else if (strcmp(flag, "-va") == 0)
        {
            int ri = next_arg(argc, argv, 2);
            IvRangePlan plan;

            if (ri < 0)
            {
                fprintf(stderr, "Missing range\n");
                if (src != stdin)
                    fclose(src);
                return 1;
            }
            if (plan_range(argv[ri], &plan) != 0)
            {
                fprintf(stderr, "Invalid range\n");
                if (src != stdin)
                    fclose(src);
                return 1;
            }
            rc = iv_stream_by_plan(src, stdout, &plan, IV_STREAM_VIEW, NULL,
                                   opts.no_numbers);
        }
        else if (strcmp(flag, "-d") == 0 || strcmp(flag, "-delete") == 0 ||
                 strcmp(flag, "-r") == 0 || strcmp(flag, "-replace") == 0)
        {
            int is_repl = (strcmp(flag, "-r") == 0 ||
                           strcmp(flag, "-replace") == 0);
            struct PlanCommit job;
            char *new_text = NULL;
            FILE *out;

            memset(&job, 0, sizeof(job));
            job.path = filename;
            job.opts = &opts;
            job.use_regex = opts.use_regex;
            job.filter = opts.multimatch;
            job.op = is_repl ? IV_STREAM_REPLACE : IV_STREAM_DELETE;
            job.plan.kind = IV_RANGE_FORWARD;
            job.plan.start = is_repl ? 1 : 1;
            job.plan.end = is_repl ? 1 : -1;

            if (is_repl)
            {
                int a = next_arg(argc, argv, 3);
                int b = (a >= 0) ? next_arg(argc, argv, a + 1) : -1;

                if (a < 0)
                    new_text = strdup("");
                else if (b < 0)
                    new_text = resolve_text(argv[a]);
                else
                {
                    if (!opts.multimatch && plan_range(argv[a], &job.plan) != 0)
                    {
                        fprintf(stderr, "Invalid range\n");
                        if (src != stdin)
                            fclose(src);
                        return 1;
                    }
                    new_text = resolve_text(argv[b]);
                }
                if (!new_text)
                    new_text = strdup("");
                job.text = new_text;
            }
            else if (!opts.multimatch)
            {
                int a = next_arg(argc, argv, 3);

                if (a >= 0 && plan_range(argv[a], &job.plan) != 0)
                {
                    fprintf(stderr, "Invalid range\n");
                    if (src != stdin)
                        fclose(src);
                    return 1;
                }
            }

            if (opts.dry_run || opts.to_stdout)
            {
                out = opts.dry_run ? fopen("/dev/null", "w") : stdout;
                if (!out)
                {
                    perror("/dev/null");
                    free(new_text);
                    if (src != stdin)
                        fclose(src);
                    return 1;
                }
                if (job.filter)
                    rc = is_repl
                             ? iv_stream_replace_match(src, out, job.filter,
                                                       opts.use_regex, job.text)
                             : iv_stream_delete_match(src, out, job.filter,
                                                      opts.use_regex);
                else
                    rc = iv_stream_by_plan(src, out, &job.plan, job.op,
                                           job.text, 1);
                if (!opts.dry_run && iv_check_stream(out) != 0)
                    rc = -1;
                if (opts.dry_run)
                    fclose(out);
            }
            else
            {
                fclose(src);
                src = NULL;
                rc = iv_commit_stream(filename, plan_commit_write, &job);
            }
            if (is_repl && rc == 0 && !opts.dry_run && !opts.quiet)
            {
                printf("%s", new_text);
                if (new_text[0] && new_text[strlen(new_text) - 1] != '\n')
                    putchar('\n');
            }
            free(new_text);
            if (src && src != stdin)
                fclose(src);
            return rc == 0 ? 0 : 1;
        }
        else /* -s */
        {
            struct
            {
                const char *path;
                const char *(*pairs)[2];
                int npairs;
                const IvOpts *opts;
                int nrepl;
                int fields;
                char delim;
                int field_num;
                const char *field_val;
                char *val_owned;
            } job;
            memset(&job, 0, sizeof(job));
            job.path = filename;
            job.opts = &opts;
            job.pairs = calloc((size_t)argc, sizeof(*job.pairs));
            if (!job.pairs)
            {
                if (src != stdin)
                    fclose(src);
                return 1;
            }

            if (opts.field_delim && opts.field_num > 0)
            {
                int vi = -1;
                for (int i = 2; i < argc - 1; i++)
                    if (strcmp(argv[i], "-F") == 0 && i + 3 < argc)
                    {
                        vi = i + 3;
                        break;
                    }
                if (vi < 0)
                {
                    fprintf(stderr, "Usage: -s file -F delim N value\n");
                    if (src != stdin)
                        fclose(src);
                    free(job.pairs);
                    return 1;
                }
                job.fields = 1;
                job.delim = opts.field_delim;
                job.field_num = opts.field_num;
                job.val_owned = resolve_text(argv[vi]);
                job.field_val = job.val_owned ? job.val_owned : "";
            }
            else
            {
                int a = next_arg(argc, argv, 3);
                int b = (a >= 0) ? next_arg(argc, argv, a + 1) : -1;
                if (a < 0 || b < 0)
                {
                    fprintf(stderr, "Usage: -s file pattern replacement [-e ...]\n");
                    if (src != stdin)
                        fclose(src);
                    free(job.pairs);
                    return 1;
                }
                job.pairs[job.npairs][0] = argv[a];
                job.pairs[job.npairs][1] = argv[b];
                job.npairs++;
                for (int i = 2; i < argc - 2; i++)
                {
                    if (strcmp(argv[i], "-e") == 0 && i + 2 < argc)
                    {
                        job.pairs[job.npairs][0] = argv[i + 1];
                        job.pairs[job.npairs][1] = argv[i + 2];
                        job.npairs++;
                    }
                }
            }

            if (opts.dry_run || opts.to_stdout)
            {
                FILE *out = opts.dry_run ? fopen("/dev/null", "w") : stdout;
                if (!out)
                {
                    perror("/dev/null");
                    if (src != stdin)
                        fclose(src);
                    free(job.val_owned);
                    free(job.pairs);
                    return 1;
                }
                if (job.fields)
                    rc = iv_stream_fields(src, out, job.delim, job.field_num,
                                          job.field_val);
                else
                    rc = iv_stream_subst(src, out, job.pairs, job.npairs, &opts,
                                         &job.nrepl);
                if (!opts.dry_run && iv_check_stream(out) != 0)
                    rc = -1;
                if (opts.dry_run)
                    fclose(out);
            }
            else
            {
                fclose(src);
                src = NULL;
                if (job.fields)
                {
                    struct FieldCommit fc = {filename, job.delim, job.field_num,
                                             job.field_val};
                    if (iv_backup_file(filename, &opts) != 0)
                    {
                        fprintf(stderr, "iv: backup failed, aborting (original unchanged)\n");
                        free(job.val_owned);
                        free(job.pairs);
                        return 1;
                    }
                    rc = iv_commit_stream(filename, field_commit_write, &fc);
                    if (rc == 0)
                        job.nrepl = 1;
                }
                else
                {
                    struct SubstCommit sc = {filename, job.pairs, job.npairs,
                                             &opts, &job.nrepl};
                    rc = iv_commit_stream(filename, subst_commit_write, &sc);
                }
            }

            if (job.nrepl > 0 && !job.fields && !opts.quiet && !iv_stdout_closed())
                fprintf(stderr, "Replaced %d occurrence(s)\n", job.nrepl);
            free(job.val_owned);
            free(job.pairs);
            if (src && src != stdin)
                fclose(src);
            if (rc != 0)
                return 1;
            return 0;
        }

        if (src != stdin)
            fclose(src);
        return rc == 0 ? 0 : 1;
    }

    /* ── -i / -a / -p / -pi (streamed) ── */
    if (strcmp(flag, "-i") == 0 || strcmp(flag, "-insert") == 0 ||
        strcmp(flag, "-a") == 0 || strcmp(flag, "-p") == 0 ||
        strcmp(flag, "-pi") == 0)
    {
        struct PlanCommit job;
        char *new_text = NULL;
        FILE *src = NULL;
        int rc = 0;

        memset(&job, 0, sizeof(job));
        job.plan.kind = IV_RANGE_FORWARD;
        job.plan.start = 1;
        job.plan.end = -1;
        job.op = IV_STREAM_APPEND;

        if (strcmp(flag, "-a") == 0)
        {
            int a = next_arg(argc, argv, 3);

            new_text = (a >= 0) ? resolve_text(argv[a]) : strdup("");
            if (!new_text)
                new_text = strdup("");
            src = open_edit_src(filename, 1);
            if (!src)
            {
                free(new_text);
                return 1;
            }
            rc = finish_stream_edit(filename, &src, &job, &opts, new_text, 1);
            free(new_text);
            if (src && src != stdin)
                fclose(src);
            return rc == 0 ? 0 : 1;
        }

        if (strcmp(flag, "-i") == 0 || strcmp(flag, "-insert") == 0)
        {
            int a = next_arg(argc, argv, 3);
            int b = (a >= 0) ? next_arg(argc, argv, a + 1) : -1;

            if (a < 0)
                new_text = strdup("");
            else if (b < 0)
                new_text = resolve_text(argv[a]);
            else
            {
                if (plan_range(argv[a], &job.plan) != 0)
                {
                    fprintf(stderr, "Invalid range\n");
                    return 1;
                }
                job.op = IV_STREAM_INSERT;
                new_text = resolve_text(argv[b]);
            }
            if (!new_text)
                new_text = strdup("");
            src = open_edit_src(filename, 1);
            if (!src)
            {
                free(new_text);
                return 1;
            }
            rc = finish_stream_edit(filename, &src, &job, &opts, new_text, 1);
            free(new_text);
            if (src && src != stdin)
                fclose(src);
            return rc == 0 ? 0 : 1;
        }

        /* -p / -pi: one or more files, optional range, content */
        {
            int nargs = 0;
            int *args = collect_args(argc, argv, 2, &nargs);
            int nfiles;
            int has_range = 0;
            int fi;
            const char *need = (strcmp(flag, "-pi") == 0)
                                   ? "iv: -pi needs at least file and content\n"
                                   : "iv: -p needs at least file and content\n";

            if (!args || nargs == 0)
            {
                free(args);
                fprintf(stderr, "%s", need);
                return 1;
            }
            {
                char *content_arg = argv[args[nargs - 1]];

                new_text = strcmp(content_arg, "-") == 0
                               ? read_stdin()
                               : resolve_text(content_arg);
            }
            if (!new_text)
                new_text = strdup("");
            nfiles = nargs - 1;
            if (nargs >= 2 &&
                plan_range(argv[args[nargs - 2]], &job.plan) == 0)
            {
                has_range = 1;
                nfiles = nargs - 2;
            }
            if (nfiles == 0)
            {
                fprintf(stderr, "iv: %s needs at least one file\n", flag);
                free(new_text);
                free(args);
                return 1;
            }
            if (!has_range)
                job.op = IV_STREAM_APPEND;
            else if (strcmp(flag, "-pi") == 0)
                job.op = IV_STREAM_INSERT_ONCE;
            else
                job.op = plan_is_single(&job.plan) ? IV_STREAM_INSERT
                                                   : IV_STREAM_REPLACE;

            for (fi = 0; fi < nfiles; fi++)
            {
                const char *fname = argv[args[fi]];
                FILE *fp = open_edit_src(fname, 1);

                if (!fp)
                {
                    rc = -1;
                    continue;
                }
                if (finish_stream_edit(fname, &fp, &job, &opts, new_text, 1) != 0)
                    rc = -1;
                if (fp && fp != stdin)
                    fclose(fp);
            }
            free(new_text);
            free(args);
            return rc == 0 ? 0 : 1;
        }
    }

    fprintf(stderr, "Unknown flag: %s\n", flag);
    usage(stderr, argv[0]);
    return 1;
}
