#ifndef CGROUP_H
#define CGROUP_H

#include <sys/types.h>

/*
 * Estruturas para armazenar métricas lidas dos arquivos
 * de estatísticas do cgroup v2.
 */

typedef struct {
    unsigned long long usage_usec;      // Tempo total de CPU (us)
    unsigned long long user_usec;       // Tempo em modo usuário (us)
    unsigned long long system_usec;     // Tempo em modo kernel (us)
} cgroup_cpu_metrics_t;

typedef struct {
    unsigned long long current;         // Uso de memória atual
    unsigned long long anon;            // Memória anônima (RSS)
    unsigned long long file;            // Cache de página
    unsigned long long pgfault;         // Total de page faults
    unsigned long long pgmajfault;      // Total de major page faults
} cgroup_mem_metrics_t;

typedef struct {
    unsigned long long rbytes;          // Total de bytes lidos
    unsigned long long wbytes;          // Total de bytes escritos
    unsigned long long rios;            // Total de operações de leitura
    unsigned long long wios;            // Total de operações de escrita
} cgroup_io_metrics_t;

/**
 * Estrutura principal que agrega todas as métricas do cgroup.
 */
typedef struct {
    cgroup_cpu_metrics_t cpu;
    cgroup_mem_metrics_t mem;
    cgroup_io_metrics_t io;
} cgroup_metrics_t;


/**
 * @brief Obtém o caminho base do sistema de arquivos cgroup v2.
 * Normalmente /sys/fs/cgroup
 */
const char* cgroup_get_base_path(void);

/**
 * @brief Garante que o diretório base do monitor exista.
 * Ex: /sys/fs/cgroup/resource_monitor
 */
int cgroup_ensure_base_path(const char* base_name);

/**
 * @brief Cria um novo cgroup (um novo diretório).
 * @param relative_path O nome do cgroup (ex: "my-test-group").
 * @return 0 em sucesso, -1 em erro.
 */
int cgroup_create(const char* relative_path);

/**
 * @brief Adiciona um PID a um cgroup.
 * @param relative_path O nome do cgroup.
 * @param pid O PID a ser movido.
 * @return 0 em sucesso, -1 em erro.
 */
int cgroup_add_process(const char* relative_path, pid_t pid);

/**
 * @brief Define o limite máximo de CPU (ex: 50% de 1 core).
 * @param relative_path O nome do cgroup.
 * @param max_usec Tempo máximo de CPU em microssegundos.
 * @param period_usec Período de tempo em microssegundos (normalmente 100000).
 * @return 0 em sucesso, -1 em erro.
 */
int cgroup_set_cpu_limit(const char* relative_path, long max_usec, long period_usec);

/**
 * @brief Define o limite máximo de memória (hard limit).
 * @param relative_path O nome do cgroup.
 * @param limit_bytes Limite de memória em bytes.
 * @return 0 em sucesso, -1 em erro.
 */
int cgroup_set_memory_limit(const char* relative_path, long limit_bytes);

/**
 * @brief Lê todas as métricas (CPU, Mem, IO) de um cgroup.
 * @param relative_path O nome do cgroup.
 * @param metrics Ponteiro para a estrutura onde as métricas serão salvas.
 * @return 0 em sucesso, -1 em erro.
 */
int cgroup_read_metrics(const char* relative_path, cgroup_metrics_t* metrics);

/**
 * @brief Gera um relatório formatado no console com as métricas de um cgroup.
 * @param relative_path O nome do cgroup.
 * @return 0 em sucesso, -1 em erro.
 */
int cgroup_generate_report(const char* relative_path);

#endif // CGROUP_H