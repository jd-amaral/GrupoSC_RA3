# Compilador e flags
CC = gcc
CFLAGS = -Wall -Wextra -O2
INCLUDE = -Iinclude

# Fontes, objetos e binário final
SRC = src/main.c src/cpu_monitor.c src/memory_monitor.c src/io_monitor.c src/namespace_analyzer.c 
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

	# Teste Namespace
	gcc -Iinclude -o tests/test_namespace tests/test_namespace.c src/namespace_analyzer.c

	@./tests/test_cpu
	@./tests/test_memory
	@./tests/test_io
	@./tests/test_namespace
# Limpeza
clean:
	rm -f $(OBJ) $(TARGET) tests/test_cpu tests/test_memory tests/test_io tests/test_namespace
