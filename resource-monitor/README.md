# README.md

## resource-monitor

O **resource-monitor** é uma ferramenta em C para monitorar métricas de uso de recursos de processos Linux (CPU, memória e I/O). Permite exportar resultados em CSV ou JSON e inclui um modo de teste automatizado.

### Compilação

```bash
cd "/mnt/c/Users/vitto/Documents/GitHub/VittPC/Sistemas de Computação - RA3/GrupoSC_RA3/resource-monitor"
make clean
make
```

## Recent changes (summary)

These are the recent improvements added to the repository to make experiments and analysis more robust and usable:

- Safe `DURATION` handling in experiment runner: runner scripts now set a sensible default for `DURATION` so runs invoked under `sudo` or different shells don't fail due to an unbound variable.
- Visualizer hardening (`scripts/visualize.py`): safer overhead calculations (avoid division-by-zero), tolerant parsing of CSVs with unquoted commas, fallback parsing of per-trial log files to recover missing `max_alloc_bytes` values (useful when workload is OOM-killed), and categorical x-axis handling for experiments 4/5.
- cgroup v2 detection: `src/cgroup_manager.c` now detects unified cgroup v2 and only writes to `cgroup.subtree_control` when appropriate; the code emits informative messages and falls back to best-effort behavior on cgroup v1 systems.
- `make valgrind` target: the `Makefile` includes a `valgrind` target which rebuilds the binary with `-g -O0` and runs Valgrind (`--leak-check=full`) to help find memory leaks.
- Ncurses realtime UI: `src/main.c` has an optional `--ui` mode (compile with `-DUSE_NCURSES` and link with `-lncursesw`) that renders CPU/memory/I/O panels in the terminal with non-blocking keyboard input (`q` to quit).
- Online anomaly detection: `src/main.c` includes a lightweight Welford-based online z-score detector (enable with `--anomaly` and configure threshold with `--anomaly-threshold`) which writes anomalies to `<outfile>.anomalies.jsonl`.
- Visualizer server (read-only): `scripts/visualize.py` gained an optional Flask-based viewer (`--serve`) that hosts a minimal dashboard listing experiments and serving the plot images already generated under each experiment's `plots/` directory. The server intentionally does NOT run experiments — it only serves existing artifacts and anomaly JSONL files.

These changes were added to improve robustness when experiments are performed under `sudo`, to make plotting resilient to partial logs, and to support both interactive terminal monitoring and offline anomaly inspection.

## Experimentos (consolidado)

Há um runner consolidado que executa os experimentos e gera os gráficos automaticamente:

- `resource-monitor/scripts/compare_tools.sh`

Modos suportados:
- `exp1` (ou passar um número de segundos como primeiro argumento): overhead do monitor (gera `out/experiments/overhead_summary.csv` e plots em `out/experiments/plots`).
- `exp2`: isolamento por namespaces (gera `out/experiments/experiment2/experiment2_results.csv` e plots em `out/experiments/experiment2/plots`).
- `exp3`: throttling de CPU via cgroup (gera `out/experiments/experiment3/exp3_results.csv` e plots em `out/experiments/experiment3/plots`).

Exemplos (executar a partir da raiz do repositório):

```bash
# Exp1: overhead (curto: 1s) - fast smoke test
cd resource-monitor
bash scripts/compare_tools.sh 1

# Exp2: namespaces
bash scripts/compare_tools.sh exp2

# Exp3: cgroup throttling (requer sudo/root to configure cgroups)
sudo bash scripts/compare_tools.sh exp3
```

Notas importantes:
- `exp3` geralmente precisa de permissões elevadas (criação de cgroups e escrita em `/sys/fs/cgroup`).
- The visualizer (`scripts/visualize.py`) requires `pandas`, `numpy`, `matplotlib` and `seaborn`. Install in a virtualenv in the repo root or in the system python:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install pandas numpy matplotlib seaborn
```

- If you ran `compare_tools.sh` with `sudo`, the visualizer may not run automatically under the same environment (root may not have `pandas`). If plots are missing, run the visualizer as your normal user:

```bash
cd resource-monitor
python3 scripts/visualize.py --dir out/experiments/experiment3 --out out/experiments/experiment3/plots --formats png,svg
```

If you want me to capture workload stdout for more accurate throughput reporting in `exp3`, I can update the runner to persist workload outputs to per-run files and parse their final iteration counts.

## Experimentos 4 & 5 — Memory and I/O limits

Adicionei dois fluxos experimentais para avaliar limites aplicados por cgroups v2:

- `exp4` — Limite de memória: executa um pequeno workload que aloca blocos de 1MB até falhar; grava `out/experiments/experiment4/exp4_results.csv` contendo `limit_bytes,trial,max_alloc_bytes,failcnt,oom_kills,exit_status,logfile`.
- `exp5` — Limite de I/O: executa um workload que escreve em disco por 5s e tenta aplicar limites `rbps/wbps` no cgroup; grava `out/experiments/experiment5/exp5_results.csv` contendo `limit_bps,trial,measured_bytes,measured_bps,avg_write_latency_us,run_time_s,applied,logfile`.

Prerequisitos e notas importantes:

- Ambos os experimentos dependem de cgroup v2 e normalmente requerem permissões elevadas para criar cgroups e escrever em `/sys/fs/cgroup`.
- No host, verifique se o sistema está montado com cgroup v2: `stat -f -c %T /sys/fs/cgroup` deve retornar `cgroup2`.
- Executar os experimentos com `sudo` é a forma mais simples de garantir que as escritas em `/sys/fs/cgroup/*` funcionem:

```bash
cd resource-monitor
sudo bash scripts/compare_tools.sh exp4
sudo bash scripts/compare_tools.sh exp5
```

- Após executar como `sudo`, a geração de gráficos deve ser feita como usuário normal (pois o Python do root pode não ter as mesmas dependências). Por exemplo:

```bash
python3 scripts/visualize.py --dir out/experiments/experiment4 --out out/experiments/experiment4/plots --formats png,svg
python3 scripts/visualize.py --dir out/experiments/experiment5 --out out/experiments/experiment5/plots --formats png,svg
```

- Os resultados dependem fortemente do suporte do kernel e da configuração dos controladores (`+memory`, `+io`, `+cpu`). Se um arquivo esperado (por exemplo `memory.max` ou `io.max`) não existir, o runner tentará fazer um set/echo em modo "best-effort" e gravará `applied=no` no CSV.

Se quiser, eu posso também:

- ajustar os tamanhos de bloco e a duração do workload de I/O para tornar a medição mais estável;
- coletar métricas adicionais do cgroup (por exemplo `cpu.stat`, `memory.current`, `io.stat`) para complementar os CSVs.


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

### Verificação de memory leaks (Valgrind)

O repositório inclui um alvo `Makefile` para executar o binário sob Valgrind. Esse alvo recompila o binário com símbolos de depuração e sem otimizações, e executa Valgrind com checagem completa de leaks.

Instale o Valgrind no sistema (Debian/Ubuntu):

```bash
sudo apt install valgrind
```

Para rodar a verificação de leaks:

```bash
cd resource-monitor
make valgrind
```

Valgrind irá reportar leaks e erros de memória — corrija os problemas reportados nos módulos relevantes (`src/*.c`) e re-execute até que não haja leaks.

### Suporte e detecção de cgroup v2

O `cgroup_manager` agora detecta automaticamente se o sistema usa cgroup v2 (hierarquia unificada) ao inicializar. Se cgroup v2 for detectado, o gerenciador tenta habilitar controllers via `cgroup.subtree_control` quando apropriado; caso contrário, o código tenta usar fallback para cgroup v1 e emitirá notas explicativas.

Para verificar manualmente qual versão está ativa no host:

```bash
stat -f -c %T /sys/fs/cgroup  # deve mostrar 'cgroup2' para v2
ls -l /sys/fs/cgroup | sed -n '1,40p'
cat /sys/fs/cgroup/cgroup.controllers  # só existe em v2
```

Se o host estiver em cgroup v1, algumas operações de limite (especialmente I/O) podem precisar de tratamento diferente; os scripts de experimento tentarão operar em modo "best-effort" e gravarão avisos nos logs/CSV.

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

## Experimento 1 — Overhead (automático)

Este repositório inclui um pequeno fluxo para rodar o Experimento 1 (overhead do monitor) e gerar gráficos automaticamente.

Arquivos criados por este fluxo:
- `resource-monitor/scripts/run_experiment_and_visualize.sh` — wrapper que executa o experimento e em seguida o visualizador; força a saída para `resource-monitor/out/experiments`.
- `resource-monitor/scripts/experiment_overhead.sh` — script que executa o benchmark (`tests/bench_cpu`) e o monitor (`resource_monitor`) e exporta CSVs (padrão: escreve em `out/experiments` quando a variável `OUTDIR` estiver definida).
- `resource-monitor/out/experiments/overhead_summary.csv` — tabela resumo do experimento (modo, intervalo, run, elapsed_sec, percent_cpu).
- `resource-monitor/out/experiments/metrics_<interval>_<run>.csv` — métricas por execução, usadas para análise de latência de amostragem.
- `resource-monitor/out/experiments/plots/*` — gráficos gerados pelo visualizador (PNG/SVG) e `aggregated_summary.csv`.

Pré-requisitos:
- Ter um ambiente Python com `pandas`, `numpy`, `matplotlib` e `seaborn` instalado. O fluxo espera encontrar um virtualenv em `.venv` no root do repositório; caso não exista, o wrapper tentará usar o `python3` do sistema.

Como executar (modo recomendado — a partir do diretório raiz do repositório):

```bash
# (opcional) criar e ativar virtualenv no root do repo
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install pandas numpy matplotlib seaborn

# rodar o wrapper (5 segundos de teste curto)
bash resource-monitor/scripts/run_experiment_and_visualize.sh 5
```

O wrapper fará:
- garantir que `resource-monitor/out/experiments` exista,
- executar `resource-monitor/scripts/experiment_overhead.sh` (passando `OUTDIR` por variável de ambiente),
- executar `resource-monitor/scripts/visualize.py --dir resource-monitor/out/experiments --out resource-monitor/out/experiments/plots --formats png,svg` usando o `.venv` ou `python3` do sistema.

Verificação rápida dos resultados:

```bash
ls -l resource-monitor/out/experiments
ls -l resource-monitor/out/experiments/plots
```

Se preferir rodar manualmente as etapas:

1) Rodar o experimento (exemplo com OUTDIR explícito):

```bash
mkdir -p resource-monitor/out/experiments
OUTDIR="$(pwd)/resource-monitor/out/experiments" resource-monitor/scripts/experiment_overhead.sh 5
```

2) Rodar o visualizador com o Python do `.venv`:

```bash
.venv/bin/python resource-monitor/scripts/visualize.py --dir resource-monitor/out/experiments --out resource-monitor/out/experiments/plots --formats png,svg
```

Se encontrar avisos do Seaborn sobre unidades categóricas no eixo X, os gráficos ainda serão salvos; posso ajustar o código para forçar casting numérico de `interval` se preferir melhorar a aparência.
