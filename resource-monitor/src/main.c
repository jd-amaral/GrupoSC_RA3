#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

static volatile int running = 1;
void handle_sigint(int sig) { running = 0; }

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <PID> [intervalo]\n", argv[0]);
        return 1;
    }

    pid_t pid = atoi(argv[1]);
    int interval = (argc >= 3) ? atoi(argv[2]) : 1;

    signal(SIGINT, handle_sigint);

    printf("Monitorando PID %d a cada %d s (Ctrl+C para parar)...\n", pid, interval);

    while (running) {
        proc_metrics_t m = {0};
        m.pid = pid;
        m.timestamp = time(NULL);

        monitor_cpu_usage(pid, &m.cpu_percent);
        monitor_memory_usage(pid, &m.rss_kb, &m.vmsize_kb);
        monitor_io_usage(pid, &m.read_bytes, &m.write_bytes);

        printf("[%.0f] CPU: %.2f%% | RSS: %lu KB | VSZ: %lu KB | R: %llu | W: %llu\n",
               m.timestamp, m.cpu_percent, m.rss_kb, m.vmsize_kb,
               m.read_bytes, m.write_bytes);

        sleep(interval);
    }

    printf("Encerrando monitoramento.\n");
    return 0;
}
