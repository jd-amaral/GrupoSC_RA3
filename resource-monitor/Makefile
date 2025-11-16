# Compilador e flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -Werror
INCLUDE = -Iinclude

# Fontes, objetos e binário final
SRC = src/main.c src/cpu_monitor.c src/memory_monitor.c src/io_monitor.c src/namespace_analyzer.c src/cgroup_manager.c
OBJ = $(SRC:.c=.o)
TARGET = resource_monitor

# Bibliotecas externas
LIBS = -ljson-c

# Regra principal
all: $(TARGET)

# Linkagem final (gera o executável)
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $(OBJ) $(LIBS)

# Compilação de cada .c individualmente
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

# Testes
test:
	@echo "== Rodando testes =="

	# Teste CPU
	gcc -Iinclude -o tests/test_cpu tests/test_cpu.c src/cpu_monitor.c

	# Teste Memory
	gcc -Iinclude -o tests/test_memory tests/test_memory.c src/memory_monitor.c

	# Teste IO (usa funções de memória também)
	gcc -Iinclude -o tests/test_io tests/test_io.c src/io_monitor.c src/memory_monitor.c

	@./tests/test_cpu
	@./tests/test_memory
	@./tests/test_io
# Limpeza
clean:
	rm -f $(OBJ) $(TARGET) tests/test_cpu tests/test_memory tests/test_io

# Valgrind target: build with debug flags and run valgrind on the binary
.PHONY: valgrind
valgrind:
	@echo "Building with debug flags and running valgrind..."
	$(MAKE) clean
	CFLAGS="-Wall -Wextra -g -O0" $(MAKE) $(TARGET)
	@echo "Running valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)"
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET) || true

# Experiment convenience targets
.PHONY: exp1 exp2 exp3 exp4 exp4_systemd exp5 exp5_systemd visualize all-experiments

exp1:
	@echo "Running Experiment 1 (overhead) -> out/experiments/experiment1"
	@env DURATION=5 bash scripts/compare_tools.sh

exp2:
	@echo "Running Experiment 2 (namespaces) -> out/experiments/experiment2"
	@bash scripts/compare_tools.sh exp2

exp3:
	@echo "Running Experiment 3 (cpu throttling) -> out/experiments/experiment3"
	@bash scripts/compare_tools.sh exp3

exp4:
	@echo "Running Experiment 4 (memory limits) -> out/experiments/experiment4"
	@sudo env DURATION=5 bash scripts/compare_tools.sh exp4

exp4_systemd:
	@echo "Controlled Experiment 4 (memory) using systemd-run (one trial example)"
	@sudo systemd-run --scope --unit=rm_exp4_make --property=MemoryMax=100M --description="rm exp4 make" bash -c './tmp/mem_alloc 0 > out/experiments/experiment4/mem_trial_systemd_make.log 2>&1' || true

exp5:
	@echo "Running Experiment 5 (I/O limits) -> out/experiments/experiment5"
	@sudo env DURATION=5 bash scripts/compare_tools.sh exp5 || true

exp5_systemd:
	@echo "Controlled Experiment 5 (I/O) using systemd-run (one trial example)"
	@sudo systemd-run --scope --unit=rm_exp5_make --property=Delegate=yes --description="rm exp5 make" bash -c './tmp/io_workload out/experiments/experiment5/io_out_systemd.dat 5 > out/experiments/experiment5/io_trial_systemd_make.log 2>&1' || true

visualize:
	@echo "Generate plots for all experiments"
	@python3 scripts/visualize.py --dir out/experiments --out out/experiments/plots --formats png,svg || true

all-experiments: exp1 exp2 exp3 exp4 exp5 visualize

.PHONY: ncurses
ncurses:
	@echo "Building with ncurses support..."
	$(MAKE) clean >/dev/null || true
	CFLAGS="$(CFLAGS) -DUSE_NCURSES" LIBS="$(LIBS) -lncursesw" $(MAKE) $(TARGET)

