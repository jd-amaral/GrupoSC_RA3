#include <stdio.h>
#include <unistd.h>
#include "../include/monitor.h"

int main() {
    pid_t pid = getpid();
    unsigned long long r, w;
    if (monitor_io_usage(pid, &r, &w) == 0)
        printf("IO Read: %llu | Write: %llu\n", r, w);
    else
        printf("Erro ao ler IO do processo %d.\n", pid);
    return 0;
}
