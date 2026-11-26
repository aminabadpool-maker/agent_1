CC ?= gcc
CFLAGS ?= -std=c11 -O3 -Wall -Wextra -Wpedantic
LDFLAGS ?= -lm

all: agent_1 mini_blender

agent_1: src/main.c
	$(CC) $(CFLAGS) src/main.c -o agent_1

mini_blender: src/mini_blender.c
	$(CC) $(CFLAGS) src/mini_blender.c $(LDFLAGS) -o mini_blender

clean:
	rm -f agent_1 mini_blender *.o *.ppm

.PHONY: all clean
