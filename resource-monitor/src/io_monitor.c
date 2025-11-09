#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int monitor_io_usage(pid_t pid, unsigned long long *read_bytes, unsigned long long *write_bytes) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/io", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[256];
    *read_bytes = *write_bytes = 0;

    while (fgets(line, sizeof(line), fp)) {
        sscanf(line, "read_bytes: %llu", read_bytes);
        sscanf(line, "write_bytes: %llu", write_bytes);
    }

    fclose(fp);
    return 0;
}
