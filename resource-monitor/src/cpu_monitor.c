#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long clk_tck = 0;

int monitor_cpu_usage(pid_t pid, double *cpu_percent) {
    if (clk_tck == 0)
        clk_tck = sysconf(_SC_CLK_TCK);

    char path[64], line[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    char comm[256], state;
    unsigned long utime, stime;
    sscanf(line, "%*d (%[^)]) %c %*s %*s %*s %*s %*s %*s %*s %*s %*s %lu %lu",
           comm, &state, &utime, &stime);

    static unsigned long long last_total_jiffies = 0, last_proc_jiffies = 0;
    unsigned long long total_jiffies = 0;
    FILE *fs = fopen("/proc/stat", "r");
    if (fs) {
        unsigned long long val; char tag[8];
        fscanf(fs, "%s", tag);
        while (fscanf(fs, "%llu", &val) == 1)
            total_jiffies += val;
        fclose(fs);
    }

    unsigned long long proc_jiffies = utime + stime;

    if (last_total_jiffies == 0) {
        last_total_jiffies = total_jiffies;
        last_proc_jiffies = proc_jiffies;
        *cpu_percent = 0.0;
        return 0;
    }

    unsigned long long delta_total = total_jiffies - last_total_jiffies;
    unsigned long long delta_proc = proc_jiffies - last_proc_jiffies;

    *cpu_percent = delta_total > 0 ? (100.0 * delta_proc / delta_total) : 0.0;

    last_total_jiffies = total_jiffies;
    last_proc_jiffies = proc_jiffies;
    return 0;
}