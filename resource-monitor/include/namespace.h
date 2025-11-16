#ifndef NAMESPACE_H
#define NAMESPACE_H

#define MAX_NAMESPACE_TYPES 7

typedef struct {
    char type[32];
    char inode[32];
} NamespaceEntry;

typedef struct {
    NamespaceEntry entries[MAX_NAMESPACE_TYPES];
    int count;
} NamespaceList;

/**
 * Lista todos os namespaces associados a um processo.
 */
int list_namespaces(int pid, NamespaceList *list);

/**
 * Busca todos os processos pertencentes a um namespace.
 */
int find_processes_in_namespace(const char *ns_type, const char *inode);

/**
 * Compara namespaces entre dois processos.
 */
int compare_namespaces(int pid1, int pid2);

/**
 * Gera relatório completo de namespaces do sistema.
 */
int generate_namespace_report();

#endif
