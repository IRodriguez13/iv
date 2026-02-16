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
    fprintf(stderr, "  %s -v [--no-numbers] file\n", prog);
    fprintf(stderr, "  %s -va [--no-numbers] start-end file\n", prog);
    fprintf(stderr, "  %s -wc file\n", prog);
    fprintf(stderr, "  %s -i|-insert file [start-end] \"text\" [--dry-run] [--no-backup]\n", prog);
    fprintf(stderr, "  %s -a file \"text\"   (append)\n", prog);
    fprintf(stderr, "  %s -d|-delete file [start-end] [--dry-run] [--no-backup]\n", prog);
    fprintf(stderr, "  %s -r|-replace file [start-end] \"text\" [--dry-run] [--no-backup]\n", prog);
    fprintf(stderr, "  %s -s file \"pattern\" \"replacement\" [-g] [--dry-run] [--no-backup]\n", prog);
    fprintf(stderr, "\n  Use \"-\" as text to read from stdin. Ranges: 1-5, -3--1, -5-\n");
}

static void parse_opts(int argc, char *argv[], IvOpts *opts) {
    opts->dry_run = 0;
    opts->no_backup = 0;
    opts->no_numbers = 0;
    opts->global_replace = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) opts->dry_run = 1;
        else if (strcmp(argv[i], "--no-backup") == 0) opts->no_backup = 1;
        else if (strcmp(argv[i], "--no-numbers") == 0) opts->no_numbers = 1;
        else if (strcmp(argv[i], "-g") == 0) opts->global_replace = 1;
    }
}

/* Next non-flag arg from index i */
static int next_arg(int argc, char *argv[], int i) {
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") && strcmp(argv[i], "--no-backup") &&
            strcmp(argv[i], "--no-numbers") && strcmp(argv[i], "-g"))
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
    /* Trim trailing newline if single line? No - keep as-is for pipes */
    return buf;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    IvOpts opts = {0};
    parse_opts(argc, argv, &opts);

    char *flag = argv[1];
    char *filename;
    if (strcmp(flag, "-va") == 0 && argc >= 4) {
        int ri = next_arg(argc, argv, 2);
        int fi = (ri >= 0) ? next_arg(argc, argv, ri + 1) : -1;
        filename = (fi >= 0) ? argv[fi] : argv[2];
    } else {
        filename = argv[2];
    }

    FILE *f = fopen(filename, "r");
    if (!f) {
        f = fopen(filename, "w");
        if (f) fclose(f);
        f = fopen(filename, "r");
    }
    if (!f) { perror(filename); return 1; }

    char *lines[MAX_LINES];
    int count = 0;
    while (count < MAX_LINES) {
        char buffer[MAX_LEN];
        if (!fgets(buffer, sizeof(buffer), f)) break;
        lines[count] = strdup(buffer);
        count++;
    }
    fclose(f);

    /* -v view */
    if (strcmp(flag, "-v") == 0) {
        show_file(lines, count, opts.no_numbers);
        goto done;
    }

    /* -va view range (order: -va range file) */
    if (strcmp(flag, "-va") == 0) {
        int ri = next_arg(argc, argv, 2);
        if (ri < 0) { fprintf(stderr, "Missing range\n"); goto done; }
        int start, end;
        if (parse_range(argv[ri], count, &start, &end) < 0) {
            fprintf(stderr, "Invalid range\n");
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

    /* -i / -insert */
    if (strcmp(flag, "-i") == 0 || strcmp(flag, "-insert") == 0) {
        int start = count + 1, end = count + 1;
        char *new_text = NULL;
        int a = next_arg(argc, argv, 3);
        int b = (a >= 0) ? next_arg(argc, argv, a + 1) : -1;
        if (a < 0) {
            new_text = strdup("");
        } else if (b < 0) {
            new_text = strcmp(argv[a], "-") == 0 ? read_stdin() : strdup(argv[a]);
        } else {
            parse_range(argv[a], count, &start, &end);
            new_text = strcmp(argv[b], "-") == 0 ? read_stdin() : strdup(argv[b]);
        }
        if (!new_text) new_text = strdup("");
        if (apply_patch(filename, lines, count, start, end, new_text, 1, &opts) == 0 && !opts.dry_run) {
            printf("%s", new_text);
            if (new_text[strlen(new_text)-1] != '\n') putchar('\n');
        }
        free(new_text);
        goto done;
    }

    /* -a append */
    if (strcmp(flag, "-a") == 0) {
        int a = next_arg(argc, argv, 3);
        char *new_text = (a >= 0) ? (strcmp(argv[a], "-") == 0 ? read_stdin() : strdup(argv[a])) : strdup("");
        if (!new_text) new_text = strdup("");
        if (apply_patch(filename, lines, count, count+1, count+1, new_text, 1, &opts) == 0 && !opts.dry_run) {
            printf("%s", new_text);
            if (new_text[strlen(new_text)-1] != '\n') putchar('\n');
        }
        free(new_text);
        goto done;
    }

    /* -d / -delete */
    if (strcmp(flag, "-d") == 0 || strcmp(flag, "-delete") == 0) {
        int start = 1, end = count;
        int a = next_arg(argc, argv, 3);
        if (a >= 0) parse_range(argv[a], count, &start, &end);
        apply_patch(filename, lines, count, start, end, "", 2, &opts);
        goto done;
    }

    /* -r / -replace */
    if (strcmp(flag, "-r") == 0 || strcmp(flag, "-replace") == 0) {
        int start = 1, end = 1;
        char *new_text = NULL;
        int a = next_arg(argc, argv, 3);
        int b = (a >= 0) ? next_arg(argc, argv, a + 1) : -1;
        if (a < 0) {
            new_text = strdup("");
        } else if (b < 0) {
            new_text = strcmp(argv[a], "-") == 0 ? read_stdin() : strdup(argv[a]);
        } else {
            parse_range(argv[a], count, &start, &end);
            new_text = strcmp(argv[b], "-") == 0 ? read_stdin() : strdup(argv[b]);
        }
        if (!new_text) new_text = strdup("");
        if (apply_patch(filename, lines, count, start, end, new_text, 3, &opts) == 0 && !opts.dry_run) {
            printf("%s", new_text);
            if (new_text[strlen(new_text)-1] != '\n') putchar('\n');
        }
        free(new_text);
        goto done;
    }

    /* -s search/replace */
    if (strcmp(flag, "-s") == 0) {
        int a = next_arg(argc, argv, 3);
        int b = (a >= 0) ? next_arg(argc, argv, a + 1) : -1;
        if (a < 0 || b < 0) { fprintf(stderr, "Usage: -s file pattern replacement\n"); goto done; }
        char *pattern = argv[a];
        char *replacement = argv[b];
        int n = search_replace(lines, count, pattern, replacement, opts.global_replace);
        if (!opts.dry_run && n > 0) {
            if (!opts.no_backup) backup_file(filename);
            write_lines_to_file(filename, lines, count);
        }
        if (n > 0) fprintf(stderr, "Replaced %d occurrence(s)\n", n);
        goto done;
    }

    fprintf(stderr, "Unknown flag: %s\n", flag);
    usage(argv[0]);

done:
    for (int i = 0; i < count; i++) free(lines[i]);
    return 0;
}
