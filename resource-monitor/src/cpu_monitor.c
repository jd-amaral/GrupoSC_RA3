#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "monitor.h"

static unsigned long long last_total_jiffies = 0;
static unsigned long long last_process_jiffies = 0;

int monitor_cpu_usage(pid_t pid, double *cpu_percent) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT)
            fprintf(stderr, "⚠️  Processo %d não encontrado (terminou?)\n", pid);
        else if (errno == EACCES)
            fprintf(stderr, "🔒 Sem permissão para ler /proc/%d/stat\n", pid);
        *cpu_percent = 0.0;
        return -1;
    }

    char buffer[256];
    unsigned long utime, stime;
    unsigned long long starttime;
    unsigned long long total_jiffies = 0;

    // Lê valores principais do processo
    for (int i = 0; i < 13; i++) fscanf(fp, "%s", buffer);
    fscanf(fp, "%lu %lu", &utime, &stime);
    fclose(fp);

    unsigned long long process_jiffies = utime + stime;

    // Lê tempo total do sistema
    fp = fopen("/proc/stat", "r");
    if (!fp) {
        perror("Erro ao ler /proc/stat");
        *cpu_percent = 0.0;
        return -1;
    }

    char cpu_label[8];
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    fscanf(fp, "%s %llu %llu %llu %llu %llu %llu %llu %llu",
           cpu_label, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    fclose(fp);

    total_jiffies = user + nice + system + idle + iowait + irq + softirq + steal;

    if (last_total_jiffies != 0 && last_process_jiffies != 0) {
        unsigned long long total_diff = total_jiffies - last_total_jiffies;
        unsigned long long proc_diff = process_jiffies - last_process_jiffies;
        *cpu_percent = 100.0 * ((double)proc_diff / (double)total_diff);
    } else {
        *cpu_percent = 0.0;
    }

    last_total_jiffies = total_jiffies;
    last_process_jiffies = process_jiffies;
    return 0;
}
