/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Iván Ezequiel Rodriguez */

#include "iv.h"

#define CMD_VIEW      1
#define CMD_VIEW_RANGE 2
#define CMD_INSERT    3
#define CMD_DELETE    4
#define CMD_REPLACE   5
#define CMD_APPEND    6
#define CMD_WC        7
#define CMD_SEARCH    8

static void usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s -h|--help\n", prog);
    fprintf(stderr, "  %s -V|--version\n", prog);
    fprintf(stderr, "  %s -v [--no-numbers] file\n", prog);
    fprintf(stderr, "  %s -va [--no-numbers] start-end file\n", prog);
    fprintf(stderr, "  %s -wc file\n", prog);
    fprintf(stderr, "  %s -n file \"pattern\" [--json]  (line numbers; --json for {\"lines\":[...]})\n", prog);
    fprintf(stderr, "  %s -u file  (undo: restore from .bak)\n", prog);
    fprintf(stderr, "  %s -diff [-u] file  (anterior .bak vs actual; -u = unified diff)\n", prog);
    fprintf(stderr, "  %s -i|-insert file [start-end] \"text\" [-q] [--dry-run] [--no-backup]\n", prog);
    fprintf(stderr, "  %s -a file \"text\" [-q]  (append)\n", prog);
    fprintf(stderr, "  %s -p file [file...] [range] content [-q]  (patch, múltiples archivos)\n", prog);
    fprintf(stderr, "  %s -pi file [file...] line content [-q]  (patch insert: insert before line, no replace)\n", prog);
    fprintf(stderr, "  %s -s file pattern replacement [-e ...] [-m pattern] [-F delim N value] [-E] [-g] [-q] [--dry-run] [--no-backup]\n", prog);
    fprintf(stderr, "  %s -l [file]  (listar backups)\n", prog);
    fprintf(stderr, "  %s -z [file]  (limpiar backups antiguos)\n", prog);
    fprintf(stderr, "  %s -d|-delete file [start-end] [-m pattern] [--dry-run] [--no-backup]\n", prog);
    fprintf(stderr, "  %s -r|-replace file [start-end] \"text\" [-m pattern] [-q] [--dry-run] [--no-backup]\n", prog);
    fprintf(stderr, "\n  -e = more replacements. -m = only lines matching pattern. -F delim N = field N (CSV). --stdout = no file write.\n");
    fprintf(stderr, "  Text: \"-\" = stdin, path = file content, else literal. -q = no tee output.\n");
    fprintf(stderr, "  Ranges: 1-5, -3--1, -5-. Backups: IV_BACKUP_DIR (default /tmp).\n");
}

static void parse_opts(int argc, char *argv[], IvOpts *opts) {
    opts->dry_run = 0;
    opts->no_backup = 0;
    opts->no_numbers = 0;
    opts->global_replace = 0;
    opts->use_regex = 0;
    opts->quiet = 0;
    opts->to_stdout = 0;
    opts->json = 0;
    opts->multimatch = NULL;
    opts->field_delim = 0;
    opts->field_num = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) opts->dry_run = 1;
        else if (strcmp(argv[i], "--no-backup") == 0) opts->no_backup = 1;
        else if (strcmp(argv[i], "--no-numbers") == 0) opts->no_numbers = 1;
        else if (strcmp(argv[i], "-g") == 0) opts->global_replace = 1;
        else if (strcmp(argv[i], "-E") == 0 || strcmp(argv[i], "--regex") == 0) opts->use_regex = 1;
        else if (strcmp(argv[i], "-q") == 0) opts->quiet = 1;
        else if (strcmp(argv[i], "--stdout") == 0) opts->to_stdout = 1;
        else if (strcmp(argv[i], "--json") == 0) opts->json = 1;
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) opts->multimatch = argv[++i];
        else if (strcmp(argv[i], "-F") == 0 && i + 2 < argc) {
            opts->field_delim = argv[i+1][0];
            opts->field_num = atoi(argv[i+2]);
            i += 2;
        }
    }
}

/* Next non-flag arg from index i */
static int next_arg(int argc, char *argv[], int i) {
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") && strcmp(argv[i], "--no-backup") &&
            strcmp(argv[i], "--no-numbers") && strcmp(argv[i], "-g") &&
            strcmp(argv[i], "-E") && strcmp(argv[i], "--regex") &&
            strcmp(argv[i], "-q") && strcmp(argv[i], "-z") && strcmp(argv[i], "-u") &&
            strcmp(argv[i], "-e") && strcmp(argv[i], "--stdout") &&
            strcmp(argv[i], "-m") && strcmp(argv[i], "-F") && strcmp(argv[i], "--json"))
            return i;
    }
    return -1;
}

char *read_stdin(void) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    while (fgets(buf + len, (int)(cap - len), stdin)) {
        len += strlen(buf + len);
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
    }
    return buf;
}

int is_binary_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++)
            if (buf[i] == 0) { fclose(f); return 1; }
    }
    fclose(f);
    return 0;
}

char *read_file_content(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) { fclose(f); return NULL; }
    while (fgets(buf + len, (int)(cap - len), f)) {
        len += strlen(buf + len);
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); fclose(f); return NULL; }
            buf = tmp;
        }
    }
    fclose(f);
    return buf;
}

/* Load lines from file using getline (no line length limit). Returns array (caller frees),
 * sets *out_count. Returns NULL on error. */
static char **load_lines(FILE *f, int *out_count) {
    size_t cap = INITIAL_LINES;
    char **lines = malloc(cap * sizeof(char *));
    if (!lines) return NULL;
    int count = 0;
    char *line = NULL;
    size_t linecap = 0;
    while (getline(&line, &linecap, f) != -1) {
        if ((size_t)count >= cap) {
            cap *= 2;
            char **tmp = realloc(lines, cap * sizeof(char *));
            if (!tmp) {
                for (int i = 0; i < count; i++) free(lines[i]);
                free(lines);
                free(line);
                return NULL;
            }
            lines = tmp;
        }
        lines[count] = strdup(line);
        if (!lines[count]) {
            for (int i = 0; i < count; i++) free(lines[i]);
            free(lines);
            free(line);
            return NULL;
        }
        count++;
    }
    free(line);
    *out_count = count;
    return lines;
}

/* Resolve text arg: "-" -> stdin, existing file path -> file content, else literal */
static char *resolve_text(const char *arg) {
    if (!arg || !*arg) return strdup("");
    if (strcmp(arg, "-") == 0) return read_stdin();
    {
        char *content = read_file_content(arg);
        if (content) return content;
    }
    return strdup(arg);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    char *flag = argv[1];
    if (strcmp(flag, "-h") == 0 || strcmp(flag, "--help") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (strcmp(flag, "--version") == 0 || strcmp(flag, "-V") == 0) {
        printf("iv %s\n", IV_VERSION);
        printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
        printf("This is free software: you are free to change and redistribute it.\n");
        printf("There is NO WARRANTY, to the extent permitted by law.\n");
        printf("\n");
        printf("Written by Iván Ezequiel Rodriguez.\n");
        return 0;
    }
    if (strcmp(flag, "-l") == 0 || strcmp(flag, "-lb") == 0) {
        char *filter = (argc >= 3) ? argv[2] : NULL;
        list_backups(filter);
        return 0;
    }
    if (strcmp(flag, "-z") == 0) {
        char *filter = (argc >= 3) ? argv[2] : NULL;
        clean_backups(filter);
        return 0;
    }

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    IvOpts opts = {0};
    parse_opts(argc, argv, &opts);
    char *filename;
    if (strcmp(flag, "-va") == 0 && argc >= 4) {
        int ri = next_arg(argc, argv, 2);
        int fi = (ri >= 0) ? next_arg(argc, argv, ri + 1) : -1;
        filename = (fi >= 0) ? argv[fi] : argv[2];
    } else {
        filename = argv[2];
    }

    /* -diff: show backup (anterior) and file (actual) in our format, streaming */
    if (strcmp(flag, "-diff") == 0) {
        int fi = next_arg(argc, argv, 2);
        if (fi < 0) { fprintf(stderr, "iv: -diff needs a file\n"); return 1; }
        filename = argv[fi];
        int unified = 0;
        for (int i = 2; i < fi; i++)
            if (strcmp(argv[i], "-u") == 0) { unified = 1; break; }
        char bakname[512];
        get_backup_path(filename, bakname, sizeof(bakname));
        FILE *bak = fopen(bakname, "r");
        if (!bak) {
            fprintf(stderr, "iv: no backup found for %s\n", filename);
            return 0;
        }
        fclose(bak);
        if (unified) {
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "diff -u \"%s\" \"%s\"", bakname, filename);
            FILE *p = popen(cmd, "r");
            if (p) {
                char buf[4096];
                while (fgets(buf, sizeof(buf), p)) fputs(buf, stdout);
                pclose(p);
            }
        } else {
            fprintf(stdout, "--- %s (previous)\n", bakname);
            stream_file_with_numbers(bakname);
            fprintf(stdout, "\n--- %s (current)\n", filename);
            stream_file_with_numbers(filename);
        }
        return 0;
    }

    /* -u undo: restore from backup in /tmp (before loading file) */
    if (strcmp(flag, "-u") == 0) {
        char bakname[512];
        get_backup_path(filename, bakname, sizeof(bakname));
        FILE *src = fopen(bakname, "r");
        if (!src) { fprintf(stderr, "iv: no backup found (%s)\n", bakname); return 1; }
        FILE *dst = fopen(filename, "w");
        if (!dst) { fclose(src); perror(filename); return 1; }
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);
        fclose(src);
        fclose(dst);
        return 0;
    }

    FILE *f;
    if (strcmp(filename, "-") == 0) {
        f = stdin;
    } else {
        f = fopen(filename, "r");
        if (!f) {
            /* Only create for edit commands that write to new files */
            if (strcmp(flag, "-i") == 0 || strcmp(flag, "-insert") == 0 ||
                strcmp(flag, "-a") == 0 || strcmp(flag, "-p") == 0 || strcmp(flag, "-pi") == 0) {
                f = fopen(filename, "w");
                if (f) fclose(f);
                f = fopen(filename, "r");
            }
        }
    }
    if (!f) { perror(filename); return 1; }

    int count = 0;
    char **lines = load_lines(f, &count);
    if (f != stdin) fclose(f);
    if (!lines) { perror("load_lines"); return 1; }
    int ret = 0;

    /* -v view */
    if (strcmp(flag, "-v") == 0) {
        show_file(lines, count, opts.no_numbers);
        goto done;
    }

    /* -va view range (order: -va range file) */
    if (strcmp(flag, "-va") == 0) {
        int ri = next_arg(argc, argv, 2);
        if (ri < 0) { fprintf(stderr, "Missing range\n"); ret = 1; goto done; }
        int start, end;
        if (parse_range(argv[ri], count, &start, &end) < 0) {
            fprintf(stderr, "Invalid range\n");
            ret = 1;
            goto done;
        }
        show_range(lines, count, start, end, opts.no_numbers);
        goto done;
    }

    /* -wc */
    if (strcmp(flag, "-wc") == 0) {
        printf("%d\n", wc_lines(lines, count));
        goto done;
    }

    /* -n find line numbers for pattern */
    if (strcmp(flag, "-n") == 0) {
        int a = next_arg(argc, argv, 3);
        if (a < 0) { fprintf(stderr, "Usage: -n file pattern [--json]\n"); ret = 1; goto done; }
        find_line_numbers(lines, count, argv[a], opts.json);
        goto done;
    }

    /* -i / -insert */
    if (strcmp(flag, "-i") == 0 || strcmp(flag, "-insert") == 0) {
        if (is_binary_file(filename)) { fprintf(stderr, "iv: refusing to edit binary file\n"); ret = 1; goto done; }
        int start = count + 1, end = count + 1;
        char *new_text = NULL;
        int a = next_arg(argc, argv, 3);
        int b = (a >= 0) ? next_arg(argc, argv, a + 1) : -1;
        if (a < 0) {
            new_text = strdup("");
        } else if (b < 0) {
            new_text = resolve_text(argv[a]);
        } else {
            if (parse_range(argv[a], count, &start, &end) < 0) { fprintf(stderr, "Invalid range\n"); ret = 1; goto done; }
            new_text = resolve_text(argv[b]);
        }
        if (!new_text) new_text = strdup("");
        if (apply_patch(filename, lines, count, start, end, new_text, 1, &opts) == 0 && !opts.dry_run && !opts.quiet) {
            printf("%s", new_text);
            if (new_text[strlen(new_text)-1] != '\n') putchar('\n');
        }
        free(new_text);
        goto done;
    }

    /* -a append */
    if (strcmp(flag, "-a") == 0) {
        if (is_binary_file(filename)) { fprintf(stderr, "iv: refusing to edit binary file\n"); ret = 1; goto done; }
        int a = next_arg(argc, argv, 3);
        char *new_text = (a >= 0) ? resolve_text(argv[a]) : strdup("");
        if (!new_text) new_text = strdup("");
        if (apply_patch(filename, lines, count, count+1, count+1, new_text, 1, &opts) == 0 && !opts.dry_run && !opts.quiet) {
            printf("%s", new_text);
            if (new_text[strlen(new_text)-1] != '\n') putchar('\n');
        }
        free(new_text);
        goto done;
    }

    /* -p patch: uno o más archivos, [range], content. Sin range = append. */
    if (strcmp(flag, "-p") == 0) {
        /* Collect non-flag args from index 2 */
        int args[64], nargs = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--dry-run") && strcmp(argv[i], "--no-backup") &&
                strcmp(argv[i], "--no-numbers") && strcmp(argv[i], "-g") &&
                strcmp(argv[i], "-E") && strcmp(argv[i], "--regex") &&
                strcmp(argv[i], "-q") && strcmp(argv[i], "-z") && strcmp(argv[i], "-u") &&
                strcmp(argv[i], "-e") && strcmp(argv[i], "--stdout") &&
                strcmp(argv[i], "-m") && strcmp(argv[i], "-F") && strcmp(argv[i], "--json")) {
                if (nargs < 64) args[nargs++] = i;
            }
        }
        if (nargs == 0) { fprintf(stderr, "iv: -p needs at least file and content\n"); ret = 1; goto done; }
        char *content_arg = argv[args[nargs-1]];
        char *new_text = strcmp(content_arg, "-") == 0 ? read_stdin() : resolve_text(content_arg);
        if (!new_text) new_text = strdup("");
        int start = 0, end = 0;
        int has_range = 0;
        int nfiles = nargs - 1;
        if (nargs >= 2) {
            int s, e;
            if (parse_range(argv[args[nargs-2]], 10000, &s, &e) == 0) {
                start = s; end = e;
                has_range = 1;
                nfiles = nargs - 2;
            }
        }
        if (nfiles == 0) { fprintf(stderr, "iv: -p needs at least one file\n"); free(new_text); ret = 1; goto done; }
        int use_replace = has_range && (start != end);
        int mode = use_replace ? 3 : 1;
        /* Process each file */
        for (int f = 0; f < nfiles; f++) {
            char *fname = argv[args[f]];
            if (is_binary_file(fname)) { fprintf(stderr, "iv: refusing to edit binary file %s\n", fname); ret = 1; continue; }
            FILE *fp = fopen(fname, "r");
            if (!fp) { fp = fopen(fname, "w"); if (fp) fclose(fp); fp = fopen(fname, "r"); }
            if (!fp) { perror(fname); continue; }
            int fcount = 0;
            char **flines = load_lines(fp, &fcount);
            fclose(fp);
            if (!flines) { perror(fname); continue; }
            int fstart, fend;
            if (!has_range) {
                fstart = fcount + 1;
                fend = fcount + 1;
            } else {
                fstart = start;
                fend = end;
                if (fstart > fcount && !use_replace) fstart = fcount + 1;
                if (fend > fcount) fend = use_replace ? fcount : fcount + 1;
                if (fstart < 1) fstart = 1;
            }
            if (apply_patch(fname, flines, fcount, fstart, fend, new_text, mode, &opts) == 0 && !opts.dry_run && !opts.quiet) {
                printf("%s", new_text);
                if (new_text[0] && new_text[strlen(new_text)-1] != '\n') putchar('\n');
            }
            for (int i = 0; i < fcount; i++) free(flines[i]);
            free(flines);
        }
        free(new_text);
        goto done;
    }

    /* -pi patch insert: insert new line before given line (no replace), shift rest down. Line required for insert. */
    if (strcmp(flag, "-pi") == 0) {
        int args[64], nargs = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--dry-run") && strcmp(argv[i], "--no-backup") &&
                strcmp(argv[i], "--no-numbers") && strcmp(argv[i], "-g") &&
                strcmp(argv[i], "-E") && strcmp(argv[i], "--regex") &&
                strcmp(argv[i], "-q") && strcmp(argv[i], "-z") && strcmp(argv[i], "-u") &&
                strcmp(argv[i], "-e") && strcmp(argv[i], "--stdout") &&
                strcmp(argv[i], "-m") && strcmp(argv[i], "-F") && strcmp(argv[i], "--json")) {
                if (nargs < 64) args[nargs++] = i;
            }
        }
        if (nargs == 0) { fprintf(stderr, "iv: -pi needs at least file and content\n"); ret = 1; goto done; }
        char *content_arg = argv[args[nargs-1]];
        char *new_text = strcmp(content_arg, "-") == 0 ? read_stdin() : resolve_text(content_arg);
        if (!new_text) new_text = strdup("");
        int insert_line = 0;
        int nfiles = nargs - 1;
        if (nargs >= 2) {
            int s, e;
            if (parse_range(argv[args[nargs-2]], 10000, &s, &e) == 0) {
                insert_line = s;
                nfiles = nargs - 2;
            }
        }
        if (nfiles == 0) { fprintf(stderr, "iv: -pi needs at least one file\n"); free(new_text); ret = 1; goto done; }
        for (int f = 0; f < nfiles; f++) {
            char *fname = argv[args[f]];
            if (is_binary_file(fname)) { fprintf(stderr, "iv: refusing to edit binary file %s\n", fname); ret = 1; continue; }
            FILE *fp = fopen(fname, "r");
            if (!fp) { fp = fopen(fname, "w"); if (fp) fclose(fp); fp = fopen(fname, "r"); }
            if (!fp) { perror(fname); continue; }
            int fcount = 0;
            char **flines = load_lines(fp, &fcount);
            fclose(fp);
            if (!flines) { perror(fname); continue; }
            int fstart = insert_line > 0 ? insert_line : fcount + 1;
            if (fstart < 1) fstart = 1;
            if (apply_patch(fname, flines, fcount, fstart, fstart, new_text, 4, &opts) == 0 && !opts.dry_run && !opts.quiet) {
                printf("%s", new_text);
                if (new_text[0] && new_text[strlen(new_text)-1] != '\n') putchar('\n');
            }
            for (int i = 0; i < fcount; i++) free(flines[i]);
            free(flines);
        }
        free(new_text);
        goto done;
    }

    /* -d / -delete; -m = delete only lines matching pattern */
    if (strcmp(flag, "-d") == 0 || strcmp(flag, "-delete") == 0) {
        if (is_binary_file(filename)) { fprintf(stderr, "iv: refusing to edit binary file\n"); ret = 1; goto done; }
        int start = 1, end = count;
        int a = next_arg(argc, argv, 3);
        if (a >= 0 && !opts.multimatch) parse_range(argv[a], count, &start, &end);
        if (opts.multimatch) {
            int new_count = 0;
            for (int i = 0; i < count; i++) {
                if (!strstr(lines[i], opts.multimatch)) {
                    if (new_count != i) lines[new_count] = lines[i];
                    new_count++;
                } else {
                    free(lines[i]);
                }
            }
            count = new_count;
            if (!opts.dry_run && !opts.to_stdout) {
                if (!opts.no_backup) backup_file(filename);
                write_lines_to_file(filename, lines, count);
            } else if (opts.to_stdout) {
                write_lines_to_stream(stdout, lines, count);
            }
        } else {
            apply_patch(filename, lines, count, start, end, "", 2, &opts);
        }
        goto done;
    }

    /* -r / -replace; -m = replace only lines matching pattern */
    if (strcmp(flag, "-r") == 0 || strcmp(flag, "-replace") == 0) {
        if (is_binary_file(filename)) { fprintf(stderr, "iv: refusing to edit binary file\n"); ret = 1; goto done; }
        int start = 1, end = 1;
        char *new_text = NULL;
        int a = next_arg(argc, argv, 3);
        int b = (a >= 0) ? next_arg(argc, argv, a + 1) : -1;
        if (a < 0) {
            new_text = strdup("");
        } else if (b < 0) {
            new_text = resolve_text(argv[a]);
        } else {
            if (!opts.multimatch && parse_range(argv[a], count, &start, &end) < 0) { fprintf(stderr, "Invalid range\n"); ret = 1; goto done; }
            new_text = resolve_text(argv[b]);
        }
        if (!new_text) new_text = strdup("");
        if (opts.multimatch) {
            for (int i = 0; i < count; i++) {
                if (strstr(lines[i], opts.multimatch)) {
                    free(lines[i]);
                    size_t n = strlen(new_text);
                    lines[i] = malloc(n + 2);
                    if (lines[i]) {
                        strcpy(lines[i], new_text);
                        if (n == 0 || new_text[n-1] != '\n') strcat(lines[i], "\n");
                    } else lines[i] = strdup("\n");
                }
            }
            if (!opts.dry_run && !opts.to_stdout) {
                if (!opts.no_backup) backup_file(filename);
                write_lines_to_file(filename, lines, count);
            } else if (opts.to_stdout) {
                write_lines_to_stream(stdout, lines, count);
            }
            if (!opts.quiet) { printf("%s", new_text); if (new_text[strlen(new_text)-1] != '\n') putchar('\n'); }
        } else if (apply_patch(filename, lines, count, start, end, new_text, 3, &opts) == 0 && !opts.dry_run && !opts.quiet) {
            printf("%s", new_text);
            if (new_text[strlen(new_text)-1] != '\n') putchar('\n');
        }
        free(new_text);
        goto done;
    }

    /* -s search/replace; -e adds more; -m = only matching lines; -F = field replace */
    if (strcmp(flag, "-s") == 0) {
        if (is_binary_file(filename)) { fprintf(stderr, "iv: refusing to edit binary file\n"); ret = 1; goto done; }
        int total = 0;
        if (opts.field_delim && opts.field_num > 0) {
            int vi = -1;
            for (int i = 2; i < argc - 1; i++)
                if (strcmp(argv[i], "-F") == 0 && i + 3 < argc) { vi = i + 3; break; }
            if (vi < 0) { fprintf(stderr, "Usage: -s file -F delim N value\n"); ret = 1; goto done; }
            char *val = resolve_text(argv[vi]);
            if (!val) val = strdup("");
            replace_field(lines, count, opts.field_delim, opts.field_num, val);
            total = count;
            free(val);
        } else {
            int a = next_arg(argc, argv, 3);
            int b = (a >= 0) ? next_arg(argc, argv, a + 1) : -1;
            if (a < 0 || b < 0) { fprintf(stderr, "Usage: -s file pattern replacement [-e ...] [-m pattern]\n"); ret = 1; goto done; }
            int pairs[64][2], npairs = 0;
            pairs[0][0] = a; pairs[0][1] = b; npairs = 1;
            for (int i = 2; i < argc - 2; i++) {
                if (strcmp(argv[i], "-e") == 0 && i + 2 < argc) {
                    pairs[npairs][0] = i + 1; pairs[npairs][1] = i + 2;
                    npairs++;
                }
            }
            for (int p = 0; p < npairs; p++) {
                char *pat = argv[pairs[p][0]], *repl = argv[pairs[p][1]];
                int n;
                if (opts.use_regex) {
                    n = opts.multimatch ? search_replace_regex_filtered(lines, count, pat, repl, opts.global_replace, opts.multimatch)
                        : search_replace_regex(lines, count, pat, repl, opts.global_replace);
                } else {
                    n = opts.multimatch ? search_replace_filtered(lines, count, pat, repl, opts.global_replace, opts.multimatch)
                        : search_replace(lines, count, pat, repl, opts.global_replace);
                }
                if (n < 0) { fprintf(stderr, "iv: invalid regex pattern\n"); ret = 1; goto done; }
                total += n;
            }
        }
        if (!opts.dry_run && total > 0) {
            if (!opts.to_stdout) {
                if (!opts.no_backup) backup_file(filename);
                write_lines_to_file(filename, lines, count);
            } else {
                write_lines_to_stream(stdout, lines, count);
            }
        }
        if (total > 0) fprintf(stderr, "Replaced %d occurrence(s)\n", total);
        goto done;
    }

    fprintf(stderr, "Unknown flag: %s\n", flag);
    usage(argv[0]);
    ret = 1;

done:
    if (lines) {
        for (int i = 0; i < count; i++) free(lines[i]);
        free(lines);
    }
    return ret;
}
