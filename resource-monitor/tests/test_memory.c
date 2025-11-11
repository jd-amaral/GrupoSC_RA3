#include <stdio.h>
#include <unistd.h>
#include "../include/monitor.h"

int main() {
    pid_t pid = getpid();
    unsigned long rss, vsz;
    if (monitor_memory_usage(pid, &rss, &vsz) == 0)
        printf("Memória RSS: %lu KB | VSZ: %lu KB\n", rss, vsz);
    else
        printf("Erro ao ler memória do processo %d.\n", pid);
    return 0;
}