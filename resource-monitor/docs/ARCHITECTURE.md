# Arquitetura do Resource Monitor

O sistema é dividido em três componentes principais:

1. **Resource Profiler**
   - Monitora métricas de CPU, memória e I/O via `/proc`.
   - Exporta dados em CSV ou JSON.
2. **Namespace Analyzer**
   - Analisa namespaces de processos (pid, net, mnt, ipc, user, uts).
3. **Control Group Manager**
   - Lê e modifica cgroups (CPU, memória, BlkIO).

Cada componente tem seu módulo independente em `src/` e cabeçalho correspondente em `include/`.
O `main.c` atua como ponto de entrada unificado para testes e integração.

Os scripts em `scripts/` auxiliam visualização e comparação com ferramentas do sistema.
