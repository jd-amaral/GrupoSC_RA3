# docs/ARCHITECTURE.md

## Arquitetura do Sistema

O **resource-monitor** segue uma arquitetura modular, dividida em três camadas principais:

### 1. Camada de Coleta (Collectors)

Responsável por interagir com o kernel Linux via `/proc` e coletar métricas específicas:

| Módulo         | Arquivo                | Descrição                                                                  |
| -------------- | ---------------------- | -------------------------------------------------------------------------- |
| CPU Monitor    | `src/cpu_monitor.c`    | Lê `/proc/[pid]/stat` e `/proc/stat` para calcular o uso de CPU.           |
| Memory Monitor | `src/memory_monitor.c` | Lê `/proc/[pid]/status` (RSS/VSZ) e usa `/proc/[pid]/statm` como fallback. |
| IO Monitor     | `src/io_monitor.c`     | Lê `/proc/[pid]/io` para bytes lidos e escritos.                           |

### 2. Camada de Controle (Main Loop)

Implementada em `src/main.c`, é responsável por:

* Validar o PID do processo com `kill(pid, 0)`;
* Fazer leituras periódicas com intervalo configurável;
* Exibir métricas no terminal;
* Salvar os dados coletados em memória;
* Exportar os resultados para CSV ou JSON;
* Executar testes automáticos (`--test`).

### 3. Camada de Interface (Header e Exportação)

* `include/monitor.h` define a estrutura `ProcessMetrics` e as assinaturas das funções.
* Exportadores (`export_metrics_csv` e `export_metrics_json`) são implementados em `main.c` usando `json-c`.

### Fluxo de Execução

```
┌──────────────────┐
│ main()           │
│ (valida PID)     │
└──────┬───────────┘
       │
       ▼
┌──────────────────────────┐
│ monitor_cpu_usage()      │
│ monitor_memory_usage()   │
│ monitor_io_usage()       │
└──────────┬───────────────┘
           ▼
┌──────────────────────────┐
│ Exibe métricas no shell  │
│ e armazena em buffer     │
└──────────┬───────────────┘
           ▼
┌──────────────────────────┐
│ Exporta CSV/JSON         │
└──────────────────────────┘
```

### Tratamento de Erros e Permissões

* **ENOENT** — processo terminou → exibe aviso e encerra loop.
* **EACCES** — sem permissão → alerta o usuário para usar `sudo`.
* **Realloc/Leitura falha** — imprime erro e exporta dados coletados até o momento.

### Extensões Futuras

* Interface ncurses em tempo real.
* Suporte a cgroups e múltiplos PIDs.
* Exportação contínua para banco de dados.
* Modo daemon com limites configuráveis.
