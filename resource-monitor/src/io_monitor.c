#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "monitor.h"

int monitor_io_usage(pid_t pid, unsigned long long *read_bytes, unsigned long long *write_bytes) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/io", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT)
            fprintf(stderr, "⚠️  Processo %d não encontrado (terminou?)\n", pid);
        else if (errno == EACCES)
            fprintf(stderr, "🔒 Sem permissão para ler /proc/%d/io\n", pid);
        *read_bytes = *write_bytes = 0;
        return -1;
    }

    char key[64];
    unsigned long long value;
    *read_bytes = *write_bytes = 0;

    while (fscanf(fp, "%63s %llu", key, &value) == 2) {
        if (strcmp(key, "read_bytes:") == 0)
            *read_bytes = value;
        else if (strcmp(key, "write_bytes:") == 0)
            *write_bytes = value;
    }

    fclose(fp);
    return 0;
}
