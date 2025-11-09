#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int monitor_memory_usage(pid_t pid, unsigned long *rss_kb, unsigned long *vmsize_kb) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[256];
    *rss_kb = *vmsize_kb = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "VmRSS: %lu", rss_kb) == 1) continue;
        if (sscanf(line, "VmSize: %lu", vmsize_kb) == 1) continue;
    }

    fclose(fp);
    return 0;
}