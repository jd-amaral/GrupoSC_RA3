#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "monitor.h"

static unsigned long long last_total_jiffies = 0;
static unsigned long long last_process_jiffies = 0;

/**
 * Lê e calcula o uso de CPU (%), tempo de usuário/sistema,
 * número de threads e trocas de contexto do processo.
 */
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

    // -------------------------------------------------------------
    // Lê valores básicos do processo: utime, stime, starttime etc.
    // -------------------------------------------------------------
    char buffer[512];
    unsigned long utime = 0, stime = 0;
    unsigned long long starttime = 0;
    unsigned long long total_jiffies = 0;

    // Campos de /proc/[pid]/stat
    int dummy;
    char comm[64], state;
    fscanf(fp, "%d %s %c", &dummy, comm, &state);
    for (int i = 0; i < 11; i++) fscanf(fp, "%s", buffer); // pula até campo 14
    fscanf(fp, "%lu %lu", &utime, &stime);
    fclose(fp);

    unsigned long long process_jiffies = utime + stime;

    // -------------------------------------------------------------
    // Lê tempo total de CPU do sistema
    // -------------------------------------------------------------
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

    // -------------------------------------------------------------
    // Calcula % de uso de CPU (média desde a última medição)
    // -------------------------------------------------------------
    if (last_total_jiffies != 0 && last_process_jiffies != 0) {
        unsigned long long total_diff = total_jiffies - last_total_jiffies;
        unsigned long long proc_diff = process_jiffies - last_process_jiffies;
        *cpu_percent = 100.0 * ((double)proc_diff / (double)total_diff);
    } else {
        *cpu_percent = 0.0;
    }

    last_total_jiffies = total_jiffies;
    last_process_jiffies = process_jiffies;

    // -------------------------------------------------------------
    // Métricas adicionais: context switches e threads
    // -------------------------------------------------------------
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    fp = fopen(path, "r");
    if (!fp) {
        if (errno == EACCES)
            fprintf(stderr, "🔒 Sem permissão para ler /proc/%d/status\n", pid);
        return 0; // já temos CPU%; continuar sem extras
    }

    char line[256];
    unsigned long voluntary_ctxt = 0, nonvoluntary_ctxt = 0;
    int threads = 0;
    unsigned long swap_kb = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "Threads: %d", &threads) == 1) continue;
        if (sscanf(line, "voluntary_ctxt_switches: %lu", &voluntary_ctxt) == 1) continue;
        if (sscanf(line, "nonvoluntary_ctxt_switches: %lu", &nonvoluntary_ctxt) == 1) continue;
    }
    fclose(fp);

    // -------------------------------------------------------------
    // Exibe métricas detalhadas
    // -------------------------------------------------------------
    double hz = sysconf(_SC_CLK_TCK);
    double user_time_sec = utime / hz;
    double sys_time_sec = stime / hz;

    printf("[CPU] %.2f%% | user=%.2fs | sys=%.2fs | threads=%d | ctxt(v/nv)=%lu/%lu\n",
           *cpu_percent, user_time_sec, sys_time_sec,
           threads, voluntary_ctxt, nonvoluntary_ctxt);

    return 0;
}

