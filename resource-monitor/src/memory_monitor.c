#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "monitor.h"

/**
 * Coleta RSS, VSZ, Page Faults e Swap.
 */
int monitor_memory_usage(
    pid_t pid,
    unsigned long *rss_kb,
    unsigned long *vmsize_kb,
    unsigned long *minflt,
    unsigned long *majflt,
    unsigned long *swap_kb
) {
    char path[128];
    FILE *fp;
    char line[256];

    *rss_kb = 0;
    *vmsize_kb = 0;
    *swap_kb = 0;
    *minflt = 0;
    *majflt = 0;

    // -------------------------------------------------------------
    // 1) Ler /proc/[pid]/status (RSS, VSZ, Swap)
    // -------------------------------------------------------------
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    fp = fopen(path, "r");

    if (!fp) {
        if (errno == ENOENT)
            fprintf(stderr, "⚠️  Processo %d não existe mais.\n", pid);
        else if (errno == EACCES)
            fprintf(stderr, "🔒 Sem permissão para ler /proc/%d/status\n", pid);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        unsigned long tmp;
        if (sscanf(line, "VmRSS: %lu", &tmp) == 1) *rss_kb = tmp;
        if (sscanf(line, "VmSize: %lu", &tmp) == 1) *vmsize_kb = tmp;
        if (sscanf(line, "VmSwap: %lu", &tmp) == 1) *swap_kb = tmp;
    }
    fclose(fp);

    // -------------------------------------------------------------
    // 2) Ler Page Faults de /proc/[pid]/stat
    // Campos:
    //   10 = minflt
    //   12 = majflt
    // -------------------------------------------------------------
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    fp = fopen(path, "r");

    if (fp) {
        int dummy_pid;
        char comm[64], state;

        // ler início
        if (fscanf(fp, "%d %63s %c", &dummy_pid, comm, &state) == 3) {

            // pular campos até chegar em minflt
            for (int i = 0; i < 6; i++) {
                if (fscanf(fp, "%255s", line) != 1) { break; }
            }

            if (fscanf(fp, "%lu", minflt) != 1) {
                /* não foi possível ler minflt */
            }

            // pular 1 campo até majflt
            if (fscanf(fp, "%255s", line) == 1) {
                if (fscanf(fp, "%lu", majflt) != 1) {
                    /* não foi possível ler majflt */
                }
            }
        }

        fclose(fp);
    }

    // -------------------------------------------------------------
    // 3) Fallback se RSS ou VSZ vierem zerados
    // -------------------------------------------------------------
    if (*rss_kb == 0 && *vmsize_kb == 0) {
        snprintf(path, sizeof(path), "/proc/%d/statm", pid);
        fp = fopen(path, "r");
        if (fp) {
            unsigned long total_pages = 0, resident_pages = 0;
            if (fscanf(fp, "%lu %lu", &total_pages, &resident_pages) == 2) {
                long page_kb = sysconf(_SC_PAGESIZE) / 1024;
                *rss_kb = resident_pages * page_kb;
                *vmsize_kb = total_pages * page_kb;
            }
            fclose(fp);
        }
    }

    // -------------------------------------------------------------
    // Imprimir métricas
    // -------------------------------------------------------------
    printf("[MEM] RSS=%lu KB | VSZ=%lu KB | Swap=%lu KB | minflt=%lu | majflt=%lu\n",
           *rss_kb, *vmsize_kb, *swap_kb, *minflt, *majflt);

    return 0;
}
