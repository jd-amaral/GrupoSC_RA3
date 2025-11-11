#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <json-c/json.h>   // biblioteca de JSON (instale com `sudo apt install libjson-c-dev`)

// Tratamento de processo inexistente
#include <signal.h>
#include <errno.h>

int check_process_exists(pid_t pid) {
    if (kill(pid, 0) == 0) {
        return 1; // Processo existe
    } else {
        if (errno == ESRCH) {
            fprintf(stderr, "Erro: processo %d não existe.\n", pid);
        } else if (errno == EPERM) {
            fprintf(stderr, "Erro: sem permissão para acessar o processo %d.\n", pid);
        } else {
            perror("Erro ao verificar processo");
        }
        return 0;
    }
}

// Exporta dados em CSV
int export_metrics_csv(const char *filename, const proc_metrics_t *data, size_t count) {
    FILE *f = fopen(filename, "w");
    if (!f) return -1;

    fprintf(f, "PID,CPU%%,RSS(kB),VSZ(kB),Read_Bytes,Write_Bytes\n");
    for (size_t i = 0; i < count; i++) {
            fprintf(f, "%d,%.0f,%.2f,%lu,%lu,%llu,%llu\n",
                data[i].pid,
                data[i].timestamp,
                data[i].cpu_percent,
                data[i].rss_kb,
                data[i].vmsize_kb,
                data[i].read_bytes,
                data[i].write_bytes);
    }
    fclose(f);
    return 0;
}

// Exporta dados em JSON
int export_metrics_json(const char *filename, const proc_metrics_t *data, size_t count) {
    struct json_object *jarray = json_object_new_array();

    for (size_t i = 0; i < count; i++) {
        struct json_object *jobj = json_object_new_object();
        json_object_object_add(jobj, "timestamp", json_object_new_double(data[i].timestamp));
        json_object_object_add(jobj, "pid", json_object_new_int(data[i].pid));
        json_object_object_add(jobj, "cpu_usage", json_object_new_double(data[i].cpu_percent));
        json_object_object_add(jobj, "rss", json_object_new_int64(data[i].rss_kb));
        json_object_object_add(jobj, "vsz", json_object_new_int64(data[i].vmsize_kb));
        json_object_object_add(jobj, "read_bytes", json_object_new_int64(data[i].read_bytes));
        json_object_object_add(jobj, "write_bytes", json_object_new_int64(data[i].write_bytes));
        json_object_array_add(jarray, jobj);
    }

    FILE *f = fopen(filename, "w");
    if (!f) {
        json_object_put(jarray);
        return -1;
    }

    fprintf(f, "%s\n", json_object_to_json_string_ext(jarray, JSON_C_TO_STRING_PRETTY));
    fclose(f);
    json_object_put(jarray);
    return 0;
}

void run_tests() {
    printf("== MODO TESTE: validando módulos ==\n\n");

    pid_t pid = getpid();
    double cpu = 0.0;
    unsigned long rss_kb = 0, vmsize_kb = 0;
    unsigned long long read_b = 0, write_b = 0;

    printf("Testando monitor_cpu_usage()...\n");
    if (monitor_cpu_usage(pid, &cpu) == 0)
        printf("✅ CPU (PID %d): %.2f%%\n", pid, cpu);
    else
        printf("❌ Falha ao medir CPU do processo %d.\n", pid);

    sleep(1);

    if (monitor_cpu_usage(pid, &cpu) == 0)
        printf("✅ CPU (PID %d): %.2f%% (segunda medição)\n\n", pid, cpu);

    printf("Testando monitor_memory_usage()...\n");
    if (monitor_memory_usage(pid, &rss_kb, &vmsize_kb) == 0)
        printf("✅ Memória RSS: %lu KB | VSZ: %lu KB\n\n", rss_kb, vmsize_kb);
    else
        printf("❌ Falha ao medir memória do processo %d.\n", pid);

    printf("Testando monitor_io_usage()...\n");
    if (monitor_io_usage(pid, &read_b, &write_b) == 0)
        printf("✅ IO Read: %llu bytes | Write: %llu bytes\n\n", read_b, write_b);
    else
        printf("❌ Falha ao medir IO do processo %d.\n", pid);

    printf("== TESTES CONCLUÍDOS ==\n");
}

static volatile int running = 1;
void handle_sigint(int sig __attribute__((unused))) {
    running = 0;
}

int main(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "--test") == 0) {
        run_tests();
        return 0;
    }

    if (argc < 3) {
        fprintf(stderr, "Uso: %s <PID> <arquivo_saida.csv|.json> [intervalo]\n", argv[0]);
        return 1;
    }

    pid_t pid = atoi(argv[1]);
    const char *output_file = argv[2];
    int interval = (argc >= 4) ? atoi(argv[3]) : 1;

    // Verifica se o processo existe antes de começar
    if (!check_process_exists(pid)) {
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_sigint);

    printf("Monitorando PID %d a cada %d s (Ctrl+C para parar)...\n", pid, interval);

    // Vetor para armazenar amostras (exemplo: até 1000 amostras)
    proc_metrics_t *data = NULL;
    size_t count = 0;

    while (running && count < 1000) {        
        char proc_path[64];
        snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
        if (access(output_file, F_OK) != 0) {
            fprintf(stderr, "\nProcesso %d terminou. Encerrando monitoramento.\n", pid);
            break;
        }
        
         // Cria nova amostra
        data = realloc(data, (count + 1) * sizeof(proc_metrics_t));
        if (!data) {
            perror("Erro de alocação de memória");
            return EXIT_FAILURE;
        }

        proc_metrics_t *m = &data[count];
        memset(m, 0, sizeof(proc_metrics_t));

        m->pid = pid;
        m->timestamp = time(NULL);

        // Coleta métricas com verificação de erro
        if (monitor_cpu_usage(pid, &m->cpu_percent) != 0)
            m->cpu_percent = 0.0;

        if (monitor_memory_usage(pid, &m->rss_kb, &m->vmsize_kb) != 0)
            m->rss_kb = m->vmsize_kb = 0;

        if (monitor_io_usage(pid, &m->read_bytes, &m->write_bytes) != 0)
            m->read_bytes = m->write_bytes = 0;

        printf("[%.0f] CPU: %.2f%% | RSS: %lu KB | VSZ: %lu KB | R: %llu | W: %llu\n",
               m->timestamp, m->cpu_percent, m->rss_kb, m->vmsize_kb,
               m->read_bytes, m->write_bytes);
        
        count++;
        sleep(interval);

    }

    printf("Encerrando monitoramento. Exportando dados para %s...\n", output_file);

    // Exporta os dados coletados no formato certo
    if (strstr(output_file, ".csv"))
        export_metrics_csv(output_file, data, 1);
    else if (strstr(output_file, ".json"))
        export_metrics_json(output_file, data, 1);
    else
        printf("Formato de saída não reconhecido. Use .csv ou .json\n");

    printf("Exportação concluída.\n");
    free(data);
    return EXIT_SUCCESS;
}
