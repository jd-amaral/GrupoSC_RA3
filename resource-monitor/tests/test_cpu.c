#include <stdio.h>
#include <unistd.h>
#include "../include/monitor.h"

int main() {
    pid_t pid = getpid(); // Testa o próprio processo
    double cpu = 0.0;
    if (monitor_cpu_usage(pid, &cpu) == 0)
        printf("CPU do processo atual: %.2f%%\n", cpu);
    else
        printf("Erro ao ler CPU do processo %d.\n", pid);
    return 0;
}