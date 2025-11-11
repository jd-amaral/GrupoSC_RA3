#ifndef MONITOR_H
#define MONITOR_H

#include <sys/types.h>
#include <unistd.h>  // POSIX systems


typedef struct {
    double timestamp;
    pid_t pid;
    double cpu_percent;
    unsigned long rss_kb;
    unsigned long vmsize_kb;
    unsigned long long read_bytes;
    unsigned long long write_bytes;
} proc_metrics_t;

int monitor_cpu_usage(pid_t pid, double *cpu_percent);
int monitor_memory_usage(pid_t pid, unsigned long *rss_kb, unsigned long *vmsize_kb);
int monitor_io_usage(pid_t pid, unsigned long long *read_bytes, unsigned long long *write_bytes);
int export_to_csv(const char *filename, const proc_metrics_t *data, size_t count);
int export_to_json(const char *filename, const proc_metrics_t *data, size_t count);

#endif