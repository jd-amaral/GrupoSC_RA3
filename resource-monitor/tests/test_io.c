#include <stdio.h>
#include <unistd.h>
#include "../include/monitor.h"

int main() {
    pid_t pid = getpid();
    unsigned long rss_kb = 0, vsz_kb = 0;
    unsigned long minflt = 0, majflt = 0, swap_kb = 0;

    printf("=== Teste: Memory Monitor ===\n");

    if (monitor_memory_usage(pid, &rss_kb, &vsz_kb, &minflt, &majflt, &swap_kb) == 0) {
        printf("PID %d:\n", pid);
        printf(" - RSS: %lu KB\n", rss_kb);
        printf(" - VSZ: %lu KB\n", vsz_kb);
        printf(" - Minor Faults: %lu\n", minflt);
        printf(" - Major Faults: %lu\n", majflt);
        printf(" - Swap Used: %lu KB\n", swap_kb);
        printf("✅ Teste de memória concluído.\n");
    } else {
        printf("❌ Erro ao ler memória do processo %d.\n", pid);
        return 1;
    }

    return 0;
}
