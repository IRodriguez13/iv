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
