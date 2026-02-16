#include "iv.h"

void show_file(char *lines[], int count, int no_numbers) {
    for (int i = 0; i < count; i++) {
        if (no_numbers)
            printf("%s", lines[i]);
        else
            printf("%4d | %s", i+1, lines[i]);
    }
}

void show_range(char *lines[], int count, int start, int end, int no_numbers) {
    if (start < 1) start = 1;
    if (end > count) end = count;
    for (int i = start-1; i < end; i++) {
        if (no_numbers)
            printf("%s", lines[i]);
        else
            printf("%4d | %s", i+1, lines[i]);
    }
}

int wc_lines(char *lines[], int count) {
    return count;
}

void find_line_numbers(char *lines[], int count, const char *pattern) {
    if (!pattern || !*pattern) return;
    for (int i = 0; i < count; i++) {
        if (strstr(lines[i], pattern))
            printf("%d\n", i + 1);
    }
}

int stream_file_with_numbers(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char buf[MAX_LEN];
    int n = 0;
    while (fgets(buf, sizeof(buf), f)) {
        printf("%4d | %s", ++n, buf);
    }
    fclose(f);
    return 0;
}
