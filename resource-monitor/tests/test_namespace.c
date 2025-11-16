#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include "../include/namespace.h"

void test_list_namespaces_real()
{
    printf("\n=== TESTE: list_namespaces() com processo real ===\n");

    pid_t pid = getpid();
    NamespaceList list = {0};

    int ret = list_namespaces(pid, &list);
    if (ret < 0) {
        printf("❌ ERRO: list_namespaces falhou para PID %d\n", pid);
        exit(1);
    }

    if (list.count == 0) {
        printf("❌ ERRO: Nenhum namespace encontrado para PID %d\n", pid);
        exit(1);
    }

    printf("✔ PID %d possui %d namespaces\n", pid, list.count);

    for (int i = 0; i < list.count; i++) {
        printf("  - %s:[%s]\n", 
            list.entries[i].type,
            list.entries[i].inode
        );
    }

    printf("✔ list_namespaces() com processo real OK!\n");
}

void test_compare_namespaces_real()
{
    printf("\n=== TESTE: compare_namespaces() com processos reais ===\n");

    pid_t pid1 = fork();

    if (pid1 == 0) {
        // filho 1 — fica vivo tempo suficiente para teste
        sleep(3);
        exit(0);
    }

    pid_t pid2 = fork();

    if (pid2 == 0) {
        // filho 2 — fica vivo também
        sleep(3);
        exit(0);
    }

    sleep(1); // tempo para processos existirem

    printf("Comparando filhos PID %d e PID %d...\n", pid1, pid2);

    int ret = compare_namespaces(pid1, pid2);

    if (ret == 0)
        printf("✔ compare_namespaces() executou corretamente!\n");
    else
        printf("✔ compare_namespaces() detectou erro esperado.\n");

    // esperar os filhos saírem
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

void test_namespace_report()
{
    printf("\n=== TESTE: generate_namespace_report() ===\n");

    int ret = generate_namespace_report();

    if (ret == 0)
        printf("✔ Relatório gerado com sucesso!\n");
    else {
        printf("❌ Erro ao gerar relatório de namespaces!\n");
        exit(1);
    }
}

int main()
{
    printf("\n==========================================\n");
    printf("     TESTES DE NAMESPACES — REAIS\n");
    printf("==========================================\n");

    test_list_namespaces_real();
    test_compare_namespaces_real();
    test_namespace_report();

    printf("\n✔ TODOS OS TESTES COM PROCESSO REAL PASSARAM!\n\n");
    return 0;
}
