#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "monitor.h"

int monitor_memory_usage(pid_t pid, unsigned long *rss_kb, unsigned long *vmsize_kb) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT)
            fprintf(stderr, "⚠️  Processo %d não encontrado (terminou?)\n", pid);
        else if (errno == EACCES)
            fprintf(stderr, "🔒 Sem permissão para ler /proc/%d/status\n", pid);
        *rss_kb = *vmsize_kb = 0;
        return -1;
    }

    char line[256];
    *rss_kb = *vmsize_kb = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "VmRSS: %lu", rss_kb) == 1)
            continue;
        if (sscanf(line, "VmSize: %lu", vmsize_kb) == 1)
            continue;
    }

    fclose(fp);

    // 🧩 Fallback: se não conseguiu nada, tenta /proc/<pid>/statm
    if (*rss_kb == 0 && *vmsize_kb == 0) {
        snprintf(path, sizeof(path), "/proc/%d/statm", pid);
        fp = fopen(path, "r");
        if (fp) {
            unsigned long pages_rss = 0, pages_total = 0;
            if (fscanf(fp, "%lu %lu", &pages_total, &pages_rss) == 2) {
                long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
                *rss_kb = pages_rss * page_size_kb;
                *vmsize_kb = pages_total * page_size_kb;
            }
            fclose(fp);
        }
    }

    return 0;
}
