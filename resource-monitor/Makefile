CC = gcc
CFLAGS = -Wall -Wextra -O2
INCLUDE = -Iinclude
SRC = src/main.c src/cpu_monitor.c src/memory_monitor.c src/io_monitor.c
OBJ = $(SRC:.c=.o)
TARGET = resource_monitor

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
