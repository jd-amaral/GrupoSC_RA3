# README.md

## resource-monitor

O **resource-monitor** é uma ferramenta em C para monitorar métricas de uso de recursos de processos Linux (CPU, memória e I/O). Permite exportar resultados em CSV ou JSON e inclui um modo de teste automatizado.

### Compilação

```bash
cd "/mnt/c/Users/vitto/Documents/GitHub/VittPC/Sistemas de Computação - RA3/GrupoSC_RA3/resource-monitor"
make clean
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

### Exemplos de uso com cgroups (cgroup v2)

Para usar as funções de cgroup você precisará de permissões (geralmente `sudo`) e de um kernel com cgroup v2 habilitado.

1) Criar um cgroup chamado `mygroup`:

```bash
sudo ./resource_monitor --cg-create mygroup
```

2) Adicionar um PID (por exemplo 1234) ao cgroup:

```bash
sudo ./resource_monitor --cg-add-pid mygroup 1234
```

3) Definir limite de memória (ex: 512 MB):

```bash
sudo ./resource_monitor --cg-set-mem mygroup 512
```

4) Definir limite de CPU (ex: 50% de um core):

```bash
sudo ./resource_monitor --cg-set-cpu mygroup 50
```

5) Gerar relatório do cgroup:

```bash
./resource_monitor --cg-report mygroup
```

Observação: criar/mover processos no cgroup pode exigir que o sistema tenha habilitados os controllers (`+cpu +memory +io`). Se houver falha por permissão, execute com `sudo`.

---

Namespace commands
--ns-list <pid>
--ns-find <type> <inode>
--ns-compare <pid1> <pid2>
--ns-report