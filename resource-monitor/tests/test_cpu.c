#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include "../include/monitor.h"

static void burn_cpu(int ms) {
    // Gera carga para o processo atual por "ms" milissegundos
    clock_t start = clock();
    while (1) {
        volatile double x = 1.2345;
        x *= 2.3456; // impede otimização
        if (((clock() - start) * 1000 / CLOCKS_PER_SEC) >= ms)
            break;
    }
}

int main() {
    pid_t pid = getpid();
    double cpu_percent = 0.0;

    printf("=== Teste: CPU Monitor ===\n");

    // 1) Primeira leitura
    if (monitor_cpu_usage(pid, &cpu_percent) == 0) {
        printf("PID %d:\n", pid);
        printf(" - CPU inicial: %.2f%%\n", cpu_percent);
    } else {
        printf("❌ Erro ao ler CPU do processo %d.\n", pid);
        return 1;
    }

    // 2) Gera carga artificial de CPU por 200ms
    printf("Gerando carga de CPU...\n");
    burn_cpu(200);

    // 3) Pequeno intervalo para garantir atualização
    usleep(200000);

    // 4) Segunda leitura
    if (monitor_cpu_usage(pid, &cpu_percent) == 0)
        printf(" - CPU após carga: %.2f%%\n", cpu_percent);
    else
        printf("❌ Erro na segunda leitura.\n");

    printf("✅ Teste de CPU concluído.\n");
    return 0;
}
 