#include "monitor.h"
#include <stdio.h>

int main() {
    double cpu;
    if (monitor_cpu_usage(getpid(), &cpu) == 0)
        printf("CPU do próprio processo: %.2f%%\n", cpu);
    else
        printf("Erro ao ler CPU.\n");
    return 0;
}
