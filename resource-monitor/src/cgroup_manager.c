#include "cgroup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>

// --- Variáveis Globais (estáticas ao arquivo) ---

#define CGROUP_V2_BASE "/sys/fs/cgroup"
#define MONITOR_BASE_DIR "resource_monitor"

// Buffer global para construir caminhos
static char g_cgroup_base_path[256];
static int g_base_path_initialized = 0;

// --- Funções Auxiliares ---

/**
 * @brief Inicializa e retorna o caminho base para nossos cgroups.
 * Ex: /sys/fs/cgroup/resource_monitor
 */
static const char* get_monitor_base_path() {
    if (!g_base_path_initialized) {
        snprintf(g_cgroup_base_path, sizeof(g_cgroup_base_path), "%s/%s",
                 CGROUP_V2_BASE, MONITOR_BASE_DIR);
        g_base_path_initialized = 1;
    }
    return g_cgroup_base_path;
}

/**
 * @brief Constrói o caminho completo para um cgroup (ex: /sys/fs/cgroup/resource_monitor/my_group)
 */
static void build_full_path(char* buf, size_t buf_size, const char* relative_path) {
    snprintf(buf, buf_size, "%s/%s", get_monitor_base_path(), relative_path);
}

/**
 * @brief Escreve uma string em um arquivo de controle do cgroup.
 */
static int write_cgroup_file(const char* cgroup_path, const char* file, const char* value) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", cgroup_path, file);

    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "🔒 Erro ao abrir (w) '%s': %s (Precisa de 'sudo'?)\n", path, strerror(errno));
        return -1;
    }

    if (fprintf(f, "%s", value) < 0) {
        fprintf(stderr, "Erro ao escrever em '%s': %s\n", path, strerror(errno));
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

/**
 * @brief Lê um arquivo de estatísticas (key-value) e preenche uma estrutura.
 * Esta função é genérica para cpu.stat, memory.stat e io.stat.
 */
static int parse_stat_file(const char* path, const char* keys[], unsigned long long* targets[], int count) {
    FILE* f = fopen(path, "r");
    if (!f) {
        if (errno != ENOENT) { // ENOENT é ok, o processo pode ter morrido
             fprintf(stderr, "Erro ao abrir '%s': %s\n", path, strerror(errno));
        }
        return -1;
    }

    char line[256];
    char key_buf[128];
    unsigned long long val_buf;

    while (fgets(line, sizeof(line), f)) {
        // io.stat usa "=", cpu.stat e memory.stat usam espaço
        if (sscanf(line, "%127s %llu", key_buf, &val_buf) == 2 || 
            sscanf(line, "%127[^=]=%llu", key_buf, &val_buf) == 2) 
        {
            for (int i = 0; i < count; i++) {
                if (strcmp(key_buf, keys[i]) == 0) {
                    *(targets[i]) = val_buf;
                    break;
                }
            }
        } else if (strstr(line, "rbytes=") || strstr(line, "wbytes=")) {
            // Caso especial para io.stat (agregação de múltiplos discos)
            // Ex: 8:0 rbytes=123 wbytes=456 ...
            char *p = line;
            while ((p = strstr(p, "rbytes="))) {
                if (sscanf(p, "rbytes=%llu", &val_buf) == 1) *(targets[0]) += val_buf; // rbytes
                p++;
            }
            p = line;
            while ((p = strstr(p, "wbytes="))) {
                if (sscanf(p, "wbytes=%llu", &val_buf) == 1) *(targets[1]) += val_buf; // wbytes
                p++;
            }
             p = line;
            while ((p = strstr(p, "rios="))) {
                if (sscanf(p, "rios=%llu", &val_buf) == 1) *(targets[2]) += val_buf; // rios
                p++;
            }
             p = line;
            while ((p = strstr(p, "wios="))) {
                if (sscanf(p, "wios=%llu", &val_buf) == 1) *(targets[3]) += val_buf; // wios
                p++;
            }
        }
    }

    fclose(f);
    return 0;
}


// --- Implementação das Funções Públicas (cgroup.h) ---

const char* cgroup_get_base_path(void) {
    return CGROUP_V2_BASE;
}

int cgroup_ensure_base_path(const char* base_name) {
    // Atualiza o nome base se fornecido
    if (base_name) {
        strncpy(g_cgroup_base_path, base_name, sizeof(g_cgroup_base_path) - 1);
        g_cgroup_base_path[sizeof(g_cgroup_base_path) - 1] = '\0';
    } else {
        get_monitor_base_path(); // Pega o padrão
    }

    // Tenta criar o diretório base
    if (mkdir(g_cgroup_base_path, 0755) != 0) {
        if (errno == EEXIST) {
            return 0; // Já existe, sucesso
        }
        fprintf(stderr, "🔒 Falha ao criar cgroup base '%s': %s (Precisa de 'sudo'?)\n",
                g_cgroup_base_path, strerror(errno));
        return -1;
    }
    printf("Caminho base do cgroup '%s' verificado/criado.\n", g_cgroup_base_path);
    return 0;
}

int cgroup_create(const char* relative_path) {
    char path[512];
    build_full_path(path, sizeof(path), relative_path);

    if (mkdir(path, 0755) != 0) {
        if (errno == EEXIST) {
             printf("Cgroup '%s' já existe.\n", relative_path);
             return 0;
        }
        fprintf(stderr, "🔒 Falha ao criar cgroup '%s': %s\n", path, strerror(errno));
        return -1;
    }
    
    // Para um cgroup ser válido, precisamos habilitar os controllers
    // Escrevemos "+cpu +memory +io" no 'cgroup.subtree_control' do PAI
    // para que fiquem disponíveis no FILHO.
    if (write_cgroup_file(get_monitor_base_path(), "cgroup.subtree_control", "+cpu +memory +io") != 0) {
        fprintf(stderr, "Aviso: Falha ao habilitar controllers (cpu, memory, io). Limites podem não funcionar.\n");
        // Não retorna -1, pois o diretório foi criado
    }

    printf("Cgroup '%s' criado.\n", relative_path);
    return 0;
}

int cgroup_add_process(const char* relative_path, pid_t pid) {
    char path[512];
    char pid_str[32];
    build_full_path(path, sizeof(path), relative_path);
    snprintf(pid_str, sizeof(pid_str), "%d", pid);

    if (write_cgroup_file(path, "cgroup.procs", pid_str) != 0) {
        fprintf(stderr, "Falha ao mover PID %d para '%s'\n", pid, relative_path);
        return -1;
    }

    printf("PID %d movido para '%s'\n", pid, relative_path);
    return 0;
}

int cgroup_set_cpu_limit(const char* relative_path, long max_usec, long period_usec) {
    char path[512];
    char value[64];
    build_full_path(path, sizeof(path), relative_path);
    snprintf(value, sizeof(value), "%ld %ld", max_usec, period_usec);

    if (write_cgroup_file(path, "cpu.max", value) != 0) {
        fprintf(stderr, "Falha ao definir limite de CPU para '%s'\n", relative_path);
        return -1;
    }

    printf("Limite de CPU para '%s' definido como %ld / %ld\n", relative_path, max_usec, period_usec);
    return 0;
}

int cgroup_set_memory_limit(const char* relative_path, long limit_bytes) {
    char path[512];
    char value[64];
    build_full_path(path, sizeof(path), relative_path);
    snprintf(value, sizeof(value), "%ld", limit_bytes);

    if (write_cgroup_file(path, "memory.max", value) != 0) {
        fprintf(stderr, "Falha ao definir limite de memória para '%s'\n", relative_path);
        return -1;
    }

    printf("Limite de memória para '%s' definido como %ld bytes\n", relative_path, limit_bytes);
    return 0;
}

int cgroup_read_metrics(const char* relative_path, cgroup_metrics_t* metrics) {
    char cgroup_path[512];
    char stat_path[512];
    build_full_path(cgroup_path, sizeof(cgroup_path), relative_path);

    memset(metrics, 0, sizeof(cgroup_metrics_t));

    // 1. CPU Metrics (cpu.stat)
    {
        size_t need = strlen(cgroup_path) + sizeof("/cpu.stat");
        if (need > sizeof(stat_path)) {
            fprintf(stderr, "Caminho muito longo para cpu.stat (%zu > %zu)\n", need, sizeof(stat_path));
            return -1;
        }
        strcpy(stat_path, cgroup_path);
        strcat(stat_path, "/cpu.stat");
    }
    const char* cpu_keys[] = {"usage_usec", "user_usec", "system_usec"};
    unsigned long long* cpu_targets[] = {&metrics->cpu.usage_usec, &metrics->cpu.user_usec, &metrics->cpu.system_usec};
    parse_stat_file(stat_path, cpu_keys, cpu_targets, 3);

    // 2. Memory Metrics (memory.stat e memory.current)
    {
        size_t need = strlen(cgroup_path) + sizeof("/memory.stat");
        if (need > sizeof(stat_path)) {
            fprintf(stderr, "Caminho muito longo para memory.stat (%zu > %zu)\n", need, sizeof(stat_path));
            return -1;
        }
        strcpy(stat_path, cgroup_path);
        strcat(stat_path, "/memory.stat");
    }
    const char* mem_keys[] = {"anon", "file", "pgfault", "pgmajfault"};
    unsigned long long* mem_targets[] = {&metrics->mem.anon, &metrics->mem.file, &metrics->mem.pgfault, &metrics->mem.pgmajfault};
    parse_stat_file(stat_path, mem_keys, mem_targets, 4);

    // memory.current é um arquivo separado
    {
        size_t need = strlen(cgroup_path) + sizeof("/memory.current");
        if (need > sizeof(stat_path)) {
            fprintf(stderr, "Caminho muito longo para memory.current (%zu > %zu)\n", need, sizeof(stat_path));
            return -1;
        }
        strcpy(stat_path, cgroup_path);
        strcat(stat_path, "/memory.current");
        FILE* f_mem = fopen(stat_path, "r");
        if (f_mem) {
            if (fscanf(f_mem, "%llu", &metrics->mem.current) != 1) {
                fprintf(stderr, "Aviso: falha ao ler memory.current em '%s'\n", stat_path);
            }
            fclose(f_mem);
        }
    }

    // 3. IO Metrics (io.stat)
    {
        size_t need = strlen(cgroup_path) + sizeof("/io.stat");
        if (need > sizeof(stat_path)) {
            fprintf(stderr, "Caminho muito longo para io.stat (%zu > %zu)\n", need, sizeof(stat_path));
            return -1;
        }
        strcpy(stat_path, cgroup_path);
        strcat(stat_path, "/io.stat");
    }
    const char* io_keys[] = {"rbytes", "wbytes", "rios", "wios"};
    unsigned long long* io_targets[] = {&metrics->io.rbytes, &metrics->io.wbytes, &metrics->io.rios, &metrics->io.wios};
    parse_stat_file(stat_path, io_keys, io_targets, 4);

    return 0;
}

int cgroup_generate_report(const char* relative_path) {
    cgroup_metrics_t metrics;
    if (cgroup_read_metrics(relative_path, &metrics) != 0) {
        fprintf(stderr, "Falha ao ler métricas do cgroup '%s'\n", relative_path);
        return -1;
    }

    printf("\n==== Relatório de Utilização Cgroup: '%s' ====\n", relative_path);
    
    printf("\n[CPU]\n");
    printf("  Total Usage: %.2f s\n", (double)metrics.cpu.usage_usec / 1000000.0);
    printf("  User:        %.2f s\n", (double)metrics.cpu.user_usec / 1000000.0);
    printf("  System:      %.2f s\n", (double)metrics.cpu.system_usec / 1000000.0);

    printf("\n[Memória]\n");
    printf("  Current:     %llu KB\n", metrics.mem.current / 1024);
    printf("  Anon (RSS):  %llu KB\n", metrics.mem.anon / 1024);
    printf("  File Cache:  %llu KB\n", metrics.mem.file / 1024);
    printf("  Page Faults: %llu (Major: %llu)\n", metrics.mem.pgfault, metrics.mem.pgmajfault);

    printf("\n[I/O (BlkIO)]\n");
    printf("  Bytes Lidos: %llu MB\n", metrics.io.rbytes / (1024 * 1024));
    printf("  Bytes Escritos: %llu MB\n", metrics.io.wbytes / (1024 * 1024));
    printf("  IOPS Leitura: %llu\n", metrics.io.rios);
    printf("  IOPS Escrita: %llu\n", metrics.io.wios);

    printf("===================================================\n");
    return 0;
}