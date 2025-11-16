#include <stdio.h>
#include <unistd.h>
#include "../include/monitor.h"

int main() {
    pid_t pid = getpid();
    double cpu_percent = 0.0;

    printf("=== Teste: CPU Monitor ===\n");

    if (monitor_cpu_usage(pid, &cpu_percent) == 0) {
        printf("PID %d:\n", pid);
        printf(" - CPU: %.2f%%\n", cpu_percent);
        printf(" - Threads, context switches e tempos serão verificados no módulo principal.\n");
    } else {
        printf("❌ Erro ao ler CPU do processo %d.\n", pid);
        return 1;
    }

    sleep(1);
    if (monitor_cpu_usage(pid, &cpu_percent) == 0)
        printf(" - CPU (segunda leitura): %.2f%%\n", cpu_percent);

    printf("✅ Teste de CPU concluído.\n");
    return 0;
}
