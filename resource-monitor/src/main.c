#include "monitor.h"
#include "namespace.h"
#include "cgroup.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <json-c/json.h>
#include <errno.h>

int check_process_exists(pid_t pid) {
    if (kill(pid, 0) == 0) {
        return 1;
    } else {
        if (errno == ESRCH)
            fprintf(stderr, "Erro: processo %d não existe.\n", pid);
        else if (errno == EPERM)
            fprintf(stderr, "Erro: sem permissão para acessar o processo %d.\n", pid);
        else
            perror("Erro ao verificar processo");
        return 0;
    }
}

/* ===================== EXPORTAÇÃO CSV ====================== */

int export_metrics_csv(const char *filename, const proc_metrics_t *data, size_t count) {
    FILE *f = fopen(filename, "w");
    if (!f) return -1;

    fprintf(f,
        "Timestamp,PID,CPU%%,Threads,VolCtx,InvCtx,"
        "RSS(kB),VSZ(kB),MinFlt,MajFlt,Swap(kB),"
        "RChar,WChar,ReadBytes,WriteBytes,Syscalls\n");

    for (size_t i = 0; i < count; i++) {
        fprintf(f,
            "%.0f,%d,%.2f,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,"
            "%llu,%llu,%llu,%llu,%llu\n",
            data[i].timestamp, data[i].pid, data[i].cpu_percent,
            data[i].threads, data[i].voluntary_ctxt, data[i].involuntary_ctxt,
            data[i].rss_kb, data[i].vmsize_kb, data[i].minflt,
            data[i].majflt, data[i].swap_kb,
            data[i].rchar, data[i].wchar,
            data[i].read_bytes, data[i].write_bytes, data[i].syscalls);
    }

    fclose(f);
    return 0;
}

/* ===================== EXPORTAÇÃO JSON ====================== */

int export_metrics_json(const char *filename, const proc_metrics_t *data, size_t count) {
    struct json_object *jarray = json_object_new_array();

    for (size_t i = 0; i < count; i++) {
        struct json_object *jobj = json_object_new_object();

        json_object_object_add(jobj, "timestamp", json_object_new_double(data[i].timestamp));
        json_object_object_add(jobj, "pid", json_object_new_int(data[i].pid));
        json_object_object_add(jobj, "cpu_percent", json_object_new_double(data[i].cpu_percent));

        json_object_object_add(jobj, "threads", json_object_new_int64(data[i].threads));
        json_object_object_add(jobj, "voluntary_ctxt", json_object_new_int64(data[i].voluntary_ctxt));
        json_object_object_add(jobj, "involuntary_ctxt", json_object_new_int64(data[i].involuntary_ctxt));

        json_object_object_add(jobj, "rss_kb", json_object_new_int64(data[i].rss_kb));
        json_object_object_add(jobj, "vmsize_kb", json_object_new_int64(data[i].vmsize_kb));

        json_object_object_add(jobj, "minflt", json_object_new_int64(data[i].minflt));
        json_object_object_add(jobj, "majflt", json_object_new_int64(data[i].majflt));
        json_object_object_add(jobj, "swap_kb", json_object_new_int64(data[i].swap_kb));

        json_object_object_add(jobj, "rchar", json_object_new_int64(data[i].rchar));
        json_object_object_add(jobj, "wchar", json_object_new_int64(data[i].wchar));
        json_object_object_add(jobj, "read_bytes", json_object_new_int64(data[i].read_bytes));
        json_object_object_add(jobj, "write_bytes", json_object_new_int64(data[i].write_bytes));
        json_object_object_add(jobj, "syscalls", json_object_new_int64(data[i].syscalls));

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

/* ===================== TESTES ====================== */

void run_tests() {
    printf("== TESTES DO RESOURCE MONITOR ==\n\n");

    pid_t pid = getpid();
    proc_metrics_t test = {0};
    test.pid = pid;

    printf("→ Testando CPU...\n");
    if (monitor_cpu_usage(pid, &test.cpu_percent) == 0)
        printf("   OK  CPU %.2f%%\n", test.cpu_percent);

    printf("→ Testando Memória...\n");
    if (monitor_memory_usage(pid,
            &test.rss_kb, &test.vmsize_kb,
            &test.minflt, &test.majflt, &test.swap_kb) == 0)
        printf("   OK  RSS=%lu KB | VSZ=%lu KB | minflt=%lu | majflt=%lu | swap=%lu\n",
               test.rss_kb, test.vmsize_kb, test.minflt, test.majflt, test.swap_kb);

    printf("→ Testando I/O e Syscalls...\n");
    if (monitor_io_usage(pid,
            &test.rchar, &test.wchar,
            &test.read_bytes, &test.write_bytes, &test.syscalls) == 0)
        printf("   OK  rchar=%llu | wchar=%llu | read=%llu | write=%llu | syscalls=%llu\n",
               test.rchar, test.wchar, test.read_bytes, test.write_bytes, test.syscalls);

    printf("\n== Testes concluídos ==\n");
}

/* ===================== LOOP PRINCIPAL ====================== */

static volatile int running = 1;
void handle_sigint(int sig __attribute__((unused))) { running = 0; }

int main(int argc, char *argv[]) {
    
    if (argc == 2 && strcmp(argv[1], "--test") == 0) {
        run_tests();
        return 0;
    }

    /* ===================== Cgroup Manager ====================== */
    // <<< 2. ADICIONE TODO ESTE BLOCO NOVO
    // Garante que o diretório base exista (ignora falha se não for sudo)
    if (argc > 1 && strncmp(argv[1], "--cg-", 5) == 0) {
        if (cgroup_ensure_base_path(NULL) != 0) {
             fprintf(stderr, "Aviso: Falha ao garantir o caminho base do cgroup. Comandos 'cg' podem falhar.\n");
             // Continua mesmo assim, pode ser só leitura
        }
    }

    if (argc == 3 && strcmp(argv[1], "--cg-create") == 0) {
        // Uso: ./resource_monitor --cg-create <nome_grupo>
        return cgroup_create(argv[2]);
    }

    if (argc == 4 && strcmp(argv[1], "--cg-add-pid") == 0) {
        // Uso: ./resource_monitor --cg-add-pid <nome_grupo> <PID>
        return cgroup_add_process(argv[2], atoi(argv[3]));
    }
    
    if (argc == 4 && strcmp(argv[1], "--cg-set-mem") == 0) {
        // Uso: ./resource_monitor --cg-set-mem <nome_grupo> <limite_MB>
        long limit_mb = atol(argv[3]);
        long limit_bytes = limit_mb * 1024 * 1024;
        return cgroup_set_memory_limit(argv[2], limit_bytes);
    }

    if (argc == 4 && strcmp(argv[1], "--cg-set-cpu") == 0) {
        // Uso: ./resource_monitor --cg-set-cpu <nome_grupo> <percent>
        // Ex: 50 -> 50% de 1 core (50000us / 100000us)
        long percent = atol(argv[3]);
        if (percent <= 0 || percent > 400) { // Limite de sanidade
             fprintf(stderr, "Percentual de CPU deve ser > 0\n"); return 1;
        }
        long max_usec = 100000 * percent / 100; // 100000us = 0.1s
        long period_usec = 100000;
        return cgroup_set_cpu_limit(argv[2], max_usec, period_usec);
    }

    if (argc == 3 && strcmp(argv[1], "--cg-report") == 0) {
        // Uso: ./resource_monitor --cg-report <nome_grupo>
        return cgroup_generate_report(argv[2]);
    }
    /* ===================== Fim Cgroup Manager ================== */

    /* ===== Namespace Analyzer ===== */
    if (argc >= 2) {

        if ((argc == 3 && (strcmp(argv[1], "--ns-list") == 0 || strcmp(argv[1], "--list-ns") == 0))) {
            int pid = atoi(argv[2]);
            if (pid <= 0) {
                fprintf(stderr, "PID inválido: %s\n", argv[2]);
                return 1;
            }

            NamespaceList list;
            memset(&list, 0, sizeof(list));

            if (list_namespaces(pid, &list) != 0) {
                fprintf(stderr, "Falha ao ler namespaces do PID %d\n", pid);
                return 1;
            }

            printf("Namespaces do PID %d:\n", pid);
            for (int i = 0; i < list.count; i++) {
                printf("  %s:[%s]\n", list.entries[i].type, list.entries[i].inode);
            }
            return 0;
        }

        if (argc == 4 && strcmp(argv[1], "--ns-find") == 0) {
            const char *type = argv[2];
            const char *inode = argv[3];
            return find_processes_in_namespace(type, inode);
        }

        if (argc == 4 && strcmp(argv[1], "--ns-compare") == 0) {
            int pid1 = atoi(argv[2]);
            int pid2 = atoi(argv[3]);
            if (pid1 <= 0 || pid2 <= 0) { fprintf(stderr, "PIDs inválidos\n"); return 1; }
            return compare_namespaces(pid1, pid2);
        }

        if (argc == 2 && strcmp(argv[1], "--ns-report") == 0) {
            return generate_namespace_report();
        }
    }

    if (argc < 3) { // [cite: 63]
        fprintf(stderr, "Uso (Monitor PID): %s <PID> <arquivo_saida.csv|.json> [intervalo]\n", argv[0]);
        fprintf(stderr, "Uso (Namespace):   %s --ns-list <PID> | --ns-find <tipo> <inode> | ...\n", argv[0]);
        fprintf(stderr, "Uso (Cgroup):      %s --cg-create <grupo> | --cg-add-pid <grupo> <PID> | ...\n", argv[0]);
        fprintf(stderr, "Uso: %s <PID> <arquivo_saida.csv|.json> [intervalo]\n", argv[0]);
        return 1;
    }
    
    pid_t pid = atoi(argv[1]);
    const char *outfile = argv[2];
    int interval = (argc >= 4) ? atoi(argv[3]) : 1;

    if (!check_process_exists(pid))
        return EXIT_FAILURE;

    signal(SIGINT, handle_sigint);

    printf("Monitorando PID %d a cada %d s... (Ctrl+C para sair)\n", pid, interval);

    proc_metrics_t *data = NULL;
    size_t count = 0;

    while (running && count < 1000) {
        data = realloc(data, (count + 1) * sizeof(proc_metrics_t));
        if (!data) {
            perror("Erro de alocação");
            return EXIT_FAILURE;
        }

        proc_metrics_t *m = &data[count];
        memset(m, 0, sizeof(*m));

        m->pid = pid;
        m->timestamp = time(NULL);

        /* COLETA COMPLETA */
        monitor_cpu_usage(pid, &m->cpu_percent);
        monitor_memory_usage(pid, &m->rss_kb, &m->vmsize_kb, &m->minflt, &m->majflt, &m->swap_kb);
        monitor_io_usage(pid, &m->rchar, &m->wchar, &m->read_bytes, &m->write_bytes, &m->syscalls);

         printf("[%.0f] CPU: %.2f%% | RSS: %lu KB | VSZ: %lu KB "
             "| RChar/WChar: %llu/%llu | Read/Write: %llu/%llu | Syscalls: %llu\n",
             m->timestamp, m->cpu_percent, m->rss_kb, m->vmsize_kb,
             m->rchar, m->wchar, m->read_bytes, m->write_bytes, m->syscalls);

        count++;
        sleep(interval);
    }

    printf("\nEncerrando e exportando para %s...\n", outfile);

    if (strstr(outfile, ".csv"))
        export_metrics_csv(outfile, data, count);
    else if (strstr(outfile, ".json"))
        export_metrics_json(outfile, data, count);
    else
        fprintf(stderr, "Formato não reconhecido (use .csv ou .json)\n");

    free(data);
    printf("Exportação concluída.\n");
    return EXIT_SUCCESS;
}
