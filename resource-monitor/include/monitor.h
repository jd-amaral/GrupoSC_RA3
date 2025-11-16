#ifndef MONITOR_H
#define MONITOR_H

#include <sys/types.h>
#include <unistd.h>  // POSIX systems

typedef struct {
    double timestamp;              // tempo da amostra (epoch)
    pid_t pid;                     // PID do processo monitorado

    // CPU
    double cpu_percent;            // uso da CPU (%)
    unsigned long threads;         // número de threads
    unsigned long voluntary_ctxt;  // trocas de contexto voluntárias
    unsigned long involuntary_ctxt;// trocas de contexto involuntárias

    // Memória
    unsigned long rss_kb;          // memória residente (KB)
    unsigned long vmsize_kb;       // memória virtual total (KB)
    unsigned long minflt;          // minor page faults
    unsigned long majflt;          // major page faults
    unsigned long swap_kb;         // uso de swap (KB)

    // I/O
    unsigned long long rchar;          // bytes lidos (nível lógico)
    unsigned long long wchar;          // bytes escritos (nível lógico)
    unsigned long long read_bytes;     // bytes lidos do disco
    unsigned long long write_bytes;    // bytes escritos no disco
    unsigned long long syscalls;       // número de syscalls de I/O
    /* Taxas por segundo (derivadas entre amostras) */
    double read_bytes_per_s;
    double write_bytes_per_s;
    double rchar_per_s;
    double wchar_per_s;
    double syscalls_per_s;
} proc_metrics_t;


// --- Protótipos dos módulos ---
int monitor_cpu_usage(pid_t pid, double *cpu_percent);
int monitor_memory_usage(pid_t pid,
                         unsigned long *rss_kb,
                         unsigned long *vmsize_kb,
                         unsigned long *minflt,
                         unsigned long *majflt,
                         unsigned long *swap_kb);
int monitor_io_usage(pid_t pid,
                     unsigned long long *rchar,
                     unsigned long long *wchar,
                     unsigned long long *read_bytes,
                     unsigned long long *write_bytes,
                     unsigned long long *syscalls);

int export_metrics_csv(const char *filename, const proc_metrics_t *data, size_t count);
int export_metrics_json(const char *filename, const proc_metrics_t *data, size_t count);

#endif
