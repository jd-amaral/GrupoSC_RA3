#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../include/namespace.h"

#define PRINT_HEADER(title) \
    printf("\n==================== %s ====================\n", title);

int main() {
    int pid = getpid();   // usa o PID atual
    int pid2 = 1;         // init/systemd, sempre existente
    NamespaceList nslist;

    PRINT_HEADER("TESTE list_namespaces()");
    int count = list_namespaces(pid, &nslist);
    if (count <= 0) {
        printf("❌ ERRO: list_namespaces() não retornou namespaces válidos.\n");
    } else {
        printf("✔ OK — %d namespaces encontrados.\n", count);
    }

    PRINT_HEADER("TESTE compare_namespaces()");
    if (compare_namespaces(pid, pid2) == 0)
        printf("✔ OK — compare_namespaces executou sem falhas.\n");
    else
        printf("❌ ERRO em compare_namespaces().\n");

    PRINT_HEADER("TESTE find_processes_in_namespace()");
    if (count > 0) {
        printf("Testando namespace: %s:[%s]\n",
               nslist.entries[0].type,
               nslist.entries[0].inode);

        if (find_processes_in_namespace(nslist.entries[0].type,
                                        nslist.entries[0].inode) == 0)
            printf("✔ OK — find_processes_in_namespace executou sem falhas.\n");
        else
            printf("❌ ERRO em find_processes_in_namespace().\n");
    } else {
        printf("⚠️  Pulando teste pois nenhum namespace foi encontrado no PID atual.\n");
    }

    PRINT_HEADER("TESTE generate_namespace_report()");
    if (generate_namespace_report() == 0)
        printf("✔ OK — relatório global gerado.\n");
    else
        printf("❌ ERRO ao gerar relatório global.\n");

    PRINT_HEADER("FIM DOS TESTES");
    return 0;
}
