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

## Arquitetura do Container / Integração com namespaces e cgroups

O projeto foi pensado para funcionar bem tanto em hosts nativos quanto em containers. Os pontos principais de integração:

- Namespaces: o `namespace_analyzer` lê links em `/proc/<pid>/ns/*` e, portanto, identifica namespaces de processos dentro do mesmo kernel (mesmo container ou host). Em containers, os namespaces podem ser isolados; o analisador só verá os PIDs visíveis dentro do mount de `/proc` do ambiente onde o binário é executado.

- Cgroups v2: o `cgroup_manager` assume que o sistema usa cgroup v2 e que o filesystem de cgroup está montado em `/sys/fs/cgroup`. O gerenciador cria um subdiretório `resource_monitor/<grupo>` sob esse caminho e escreve em arquivos de controle (`cgroup.procs`, `cpu.max`, `memory.max`, etc.).

- Permissões: operações de escrita em `/sys/fs/cgroup` geralmente exigem privilégios (root). As leituras podem funcionar sem root dependendo das políticas do sistema. O código trata erros de permissão e emite mensagens claras ao usuário.

### Recomendações de implantação em container

- Se executar dentro de um container e precisar controlar cgroups do host, o container precisa executar com privilégios (`--privileged`) ou com montagens específicas de `/sys/fs/cgroup` habilitadas. Caso contrário, as operações de criação/limit/remap falharão por permissão.
- Para análise de namespaces global (mostrar PIDs de todo o host), execute o binário no host ou em um container com `/proc` do host montado (por exemplo `-v /proc:/host_proc:ro`) e ajuste caminhos no código para usar `/host_proc`.

### Segurança e isolamento

- O analisador de namespaces e o gerenciador de cgroup não elevam privilégios por si só — operações que requerem root falham com mensagens informativas. Testes que modificam cgroups são implementados como stubs ou marcados para execução manual com `sudo`.

Essas considerações ajudam a entender como a ferramenta se comporta em ambientes containerizados e quais ajustes são necessários para integração com infraestruturas que usam namespaces e cgroups intensivamente.
