/*
 * src/namespace_analyzer.c
 *
 * Ferramenta para analisar namespaces de processos.
 *
 * Funções exportadas (definidas em include/namespace.h):
 *  - int list_namespaces(int pid, NamespaceList *list);
 *  - int find_processes_in_namespace(const char *ns_type, const char *inode);
 *  - int compare_namespaces(int pid1, int pid2);
 *  - int generate_namespace_report(void);
 *
 * Implementação robusta: trata readlink corretamente, evita buffer overflows,
 * usa buffer dinâmico para agregação de PIDs no relatório global.
 */

#include "namespace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

static const char *NAMESPACE_TYPES[MAX_NAMESPACE_TYPES] = {
    "mnt", "uts", "ipc", "net", "pid", "cgroup", "user"
};

/* Lê o link simbólico /proc/<pid>/ns/<type> e extrai o número do inode (entre colchetes).
 * buffer deve ter espaço suficiente; buffer_size é o tamanho disponível.
 * Retorna 0 em sucesso (buffer preenchido com string do inode), -1 em erro.
 */
int read_ns_inode(const char *path, char *buffer, size_t buffer_size) {
    if (!path || !buffer || buffer_size == 0) return -1;

    char linkbuf[256];
    ssize_t len = readlink(path, linkbuf, sizeof(linkbuf) - 1);
    if (len == -1) {
        return -1;
    }
    linkbuf[len] = '\0';

    /* procura "[...]" */
    char *start = strchr(linkbuf, '[');
    char *end = strchr(linkbuf, ']');
    if (!start || !end || end <= start + 1) return -1;

    size_t inode_len = (size_t)(end - start - 1);
    if (inode_len + 1 > buffer_size) {
        /* inode muito grande para o buffer fornecido */
        return -1;
    }

    memcpy(buffer, start + 1, inode_len);
    buffer[inode_len] = '\0';
    return 0;
}

/* Preenche NamespaceList com namespaces do PID (type + inode).
 * Retorna número de namespaces encontrados (>=0) ou -1 em erro.
 */
int list_namespaces(int pid, NamespaceList *list) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/ns", pid);

    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return -1;
    }

    struct dirent *entry;
    list->count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char linkpath[256];
        char target[256];

        snprintf(linkpath, sizeof(linkpath), "%s/%s", path, entry->d_name);

        ssize_t len = readlink(linkpath, target, sizeof(target)-1);
        if (len == -1) continue;

        target[len] = '\0';

        char *inode = strchr(target, '[');
        if (inode) {
            inode++;
            inode[strlen(inode) - 1] = '\0';
        } else {
            continue;
        }

        strncpy(list->entries[list->count].type, entry->d_name, sizeof(list->entries[0].type)-1);
        strncpy(list->entries[list->count].inode, inode, sizeof(list->entries[0].inode)-1);

        list->count++;
        if (list->count >= MAX_NAMESPACE_TYPES) break;
    }

    closedir(dir);
    return 0;
}


/* Percorre /proc e imprime PIDs que estão no namespace (ns_type:[inode]).
 * Retorna 0 em sucesso, -1 em erro.
 */
int find_processes_in_namespace(const char *ns_type, const char *inode) {
    if (!ns_type || !inode) return -1;

    DIR *proc = opendir("/proc");
    if (!proc) return -1;

    struct dirent *entry;
    printf("Processos no namespace %s:[%s]\n", ns_type, inode);

    while ((entry = readdir(proc)) != NULL) {
        /* pula não-numéricos */
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;

        char ns_path[128];
        snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/%s", pid, ns_type);

        /* stat para verificar existência e permissões */
        struct stat st;
        if (stat(ns_path, &st) != 0) continue;

        char found_inode[64];
        if (read_ns_inode(ns_path, found_inode, sizeof(found_inode)) == 0) {
            if (strcmp(found_inode, inode) == 0) {
                printf(" → PID %d\n", pid);
            }
        }
    }

    closedir(proc);
    return 0;
}

/* Compara namespaces entre dois processos e imprime se compartilham ou diferem.
 * Retorna 0 em sucesso, -1 em erro.
 */
int compare_namespaces(int pid1, int pid2) {
    NamespaceList ns1 = {0}, ns2 = {0};
    if (list_namespaces(pid1, &ns1) < 0) {
        fprintf(stderr, "Erro ao ler namespaces do PID %d\n", pid1);
        return -1;
    }
    if (list_namespaces(pid2, &ns2) < 0) {
        fprintf(stderr, "Erro ao ler namespaces do PID %d\n", pid2);
        return -1;
    }

    printf("Comparando PID %d e %d:\n", pid1, pid2);

    for (int i = 0; i < ns1.count; ++i) {
        const char *type1 = ns1.entries[i].type;
        const char *inode1 = ns1.entries[i].inode;

        int found = 0;
        for (int j = 0; j < ns2.count; ++j) {
            if (strcmp(type1, ns2.entries[j].type) == 0) {
                found = 1;
                if (strcmp(inode1, ns2.entries[j].inode) == 0) {
                    printf(" ✔ Compartilham namespace %s\n", type1);
                } else {
                    printf(" ✖ Diferem em %s ( %s != %s )\n", type1, inode1, ns2.entries[j].inode);
                }
                break;
            }
        }
        if (!found) {
            printf(" ⚠️  Tipo %s presente em %d mas ausente em %d\n", type1, pid1, pid2);
        }
    }

    /* verifica se ns2 tem tipos que ns1 não tem */
    for (int j = 0; j < ns2.count; ++j) {
        int found = 0;
        for (int i = 0; i < ns1.count; ++i) {
            if (strcmp(ns2.entries[j].type, ns1.entries[i].type) == 0) { found = 1; break; }
        }
        if (!found) {
            printf(" ⚠️  Tipo %s presente em %d mas ausente em %d\n", ns2.entries[j].type, pid2, pid1);
        }
    }

    return 0;
}

/* Estrutura auxiliar para mapa dinâmico em generate_namespace_report */
typedef struct {
    char type[32];
    char inode[64];
    char *pids;       /* string dinâmica com PIDs separados por espaço */
    size_t pids_cap;  /* capacidade */
    size_t pids_len;  /* comprimento atual (sem terminador) */
} ns_map_entry_t;

/* adiciona pid à string pids, alocando/reallocando conforme necessário */
static int ns_map_add_pid(ns_map_entry_t *e, int pid) {
    if (!e) return -1;
    char buf[32];
    int n = snprintf(buf, sizeof(buf), (e->pids_len == 0) ? "%d" : " %d", pid);
    if (n <= 0) return -1;
    size_t need = (size_t)n;
    if (e->pids_len + need + 1 > e->pids_cap) {
        size_t newcap = (e->pids_cap == 0) ? 128 : e->pids_cap * 2;
        while (newcap < e->pids_len + need + 1) newcap *= 2;
        char *tmp = realloc(e->pids, newcap);
        if (!tmp) return -1;
        e->pids = tmp;
        e->pids_cap = newcap;
    }
    memcpy(e->pids + e->pids_len, buf, need);
    e->pids_len += need;
    e->pids[e->pids_len] = '\0';
    return 0;
}

/* Gera relatório global: percorre /proc, lê namespaces de cada PID e agrega por (type,inode).
 * Imprime namespaces por PID e depois a agregação.
 * Retorna 0 em sucesso, -1 em erro.
 */
int generate_namespace_report() {
    DIR *proc = opendir("/proc");
    if (!proc) {
        perror("opendir(/proc)");
        return -1;
    }

    struct dirent *entry;

    printf("==== RELATÓRIO GLOBAL DE NAMESPACES ====\n");

    /* vetor dinâmico de entradas do mapa */
    ns_map_entry_t *map = NULL;
    size_t map_len = 0, map_cap = 0;

    while ((entry = readdir(proc)) != NULL) {
        /* pula entradas não numéricas (somente PIDs) */
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;

        NamespaceList nslist;
        if (list_namespaces(pid, &nslist) < 0) {
            /* se não conseguir ler, apenas pula */
            continue;
        }

        printf("Namespaces do processo %d:\n", pid);
        for (int i = 0; i < nslist.count; ++i) {
            printf("  %s:[%s]\n", nslist.entries[i].type, nslist.entries[i].inode);

            /* procura no mapa */
            int found_index = -1;
            for (size_t k = 0; k < map_len; ++k) {
                if (strcmp(map[k].type, nslist.entries[i].type) == 0 &&
                    strcmp(map[k].inode, nslist.entries[i].inode) == 0) {
                    found_index = (int)k;
                    break;
                }
            }

            if (found_index >= 0) {
                ns_map_add_pid(&map[found_index], pid);
            } else {
                /* cria nova entrada no mapa */
                if (map_len + 1 > map_cap) {
                    size_t newcap = (map_cap == 0) ? 32 : map_cap * 2;
                    ns_map_entry_t *tmp = realloc(map, newcap * sizeof(ns_map_entry_t));
                    if (!tmp) { closedir(proc); free(map); return -1; }
                    map = tmp;
                    map_cap = newcap;
                }
                /* inicializa */
                ns_map_entry_t *e = &map[map_len];
                memset(e, 0, sizeof(*e));
                strncpy(e->type, nslist.entries[i].type, sizeof(e->type) - 1);
                strncpy(e->inode, nslist.entries[i].inode, sizeof(e->inode) - 1);
                e->pids = NULL; e->pids_cap = 0; e->pids_len = 0;
                ns_map_add_pid(e, pid);
                map_len++;
            }
        }
    }

    closedir(proc);

    /* imprime agregação final */
    for (size_t i = 0; i < map_len; ++i) {
        printf("%s:[%s]  →  PIDs:%s\n",
               map[i].type,
               map[i].inode,
               map[i].pids ? map[i].pids : "");
    }

    /* limpa memória */
    for (size_t i = 0; i < map_len; ++i) free(map[i].pids);
    free(map);

    return 0;
}
