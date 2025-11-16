#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "monitor.h"

/**
 * Coleta métricas completas de I/O do processo:
 * - rchar:   bytes lidos lógicos
 * - wchar:   bytes escritos lógicos
 * - read_bytes:  bytes lidos do disco
 * - write_bytes: bytes escritos no disco
 * - syscr:   número de syscalls relacionadas a I/O
 */
int monitor_io_usage(pid_t pid,
                     unsigned long long *rchar,
                     unsigned long long *wchar,
                     unsigned long long *read_bytes,
                     unsigned long long *write_bytes,
                     unsigned long long *syscr)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/io", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT)
            fprintf(stderr, "⚠️  Processo %d não encontrado (terminou?)\n", pid);
        else if (errno == EACCES)
            fprintf(stderr, "🔒 Sem permissão para ler /proc/%d/io\n", pid);

        *rchar = *wchar = *read_bytes = *write_bytes = *syscr = 0;
        return -1;
    }

    char key[64];
    unsigned long long value = 0;

    // Iniciar métricas
    *rchar = *wchar = *read_bytes = *write_bytes = *syscr = 0;

    while (fscanf(fp, "%63s %llu", key, &value) == 2) {
        if (strcmp(key, "rchar:") == 0)
            *rchar = value;
        else if (strcmp(key, "wchar:") == 0)
            *wchar = value;
        else if (strcmp(key, "syscr:") == 0)
            *syscr = value;
        else if (strcmp(key, "read_bytes:") == 0)
            *read_bytes = value;
        else if (strcmp(key, "write_bytes:") == 0)
            *write_bytes = value;
    }

    fclose(fp);

    return 0;
}
