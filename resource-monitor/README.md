# README.md

## resource-monitor

O **resource-monitor** é uma ferramenta em C para monitorar métricas de uso de recursos de processos Linux (CPU, memória e I/O). Permite exportar resultados em CSV ou JSON e inclui um modo de teste automatizado.

### Compilação

```bash
make
```

### Uso

Modo monitoramento:

```bash
./resource_monitor <PID> <saida.csv|saida.json> <intervalo_s>
```

Exemplo:

```bash
./resource_monitor 1234 out.csv 1
```

Modo teste (autoverificação dos módulos):

```bash
./resource_monitor --test
```

### Estrutura do Projeto

```
resource-monitor/
├── include/
│   └── monitor.h         # Interfaces e struct ProcessMetrics
├── src/
│   ├── main.c            # Loop principal, exportação e testes
│   ├── cpu_monitor.c     # Coleta uso de CPU
│   ├── memory_monitor.c  # Coleta uso de memória (status + fallback statm)
│   └── io_monitor.c      # Coleta I/O
├── docs/
│   └── ARCHITECTURE.md   # Documentação da arquitetura
└── Makefile              # Script de build
```

### Dependências

* `libjson-c-dev` — para exportação JSON:

```bash
sudo apt install libjson-c-dev
```

### Observações

* É necessário permissão para ler `/proc` de outros usuários (use `sudo` se necessário).
* No WSL, alguns campos de `/proc/[pid]/status` podem não estar disponíveis; o programa usa fallback automático.
* CSV e JSON são salvos com timestamp, PID, CPU%, RSS/VSZ e bytes de I/O.

---
