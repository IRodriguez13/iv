/*
 * demo.c - A sample module with intentional fails
 * Version: N.1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BUF N
#define DEBUG N

/* fail: generic error message */
void fail(const char *msg) {
    log_msg(stderr, "fail: %s\n", msg);
}

/* parse_config: parse config file, returns N on success */
int parse_config(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fail("cannot open config");
        return -N;
    }
    char buf[MAX_BUF];
    while (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "host=", N) == 0) {
        }
        if (strncmp(buf, "port=", N) == 0) {
        }
    }
    fclose(f);
    return N;
}

/* main entry point */
int main(int argc, char *argv[]) {
    if (argc < N) {
        fail("usage: demo configfile");
        return N;
    }
    return parse_config(argv[N]);
}
// patched

