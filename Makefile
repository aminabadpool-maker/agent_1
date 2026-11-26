CC      ?= cc
CFLAGS  ?= -std=c11 -O3 -Wall -Wextra -Wpedantic
LDFLAGS ?= -lm

UNAME_M := $(shell uname -m)
ifneq (,$(filter aarch64 arm64 armv7l armv8l,$(UNAME_M)))
    CFLAGS += -mtune=native
endif

TARGET  := mini_blender
SRC     := src/mini_blender.c

.PHONY: all clean run stress shell help

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) -f 0 -n 12 -c 24 -o renders -p

shell: $(TARGET)
	./$(TARGET) -i

stress: $(TARGET)
	./$(TARGET) -f 0 -n 60 -c 60 -o renders -s scenes/heavy_stress_scene.xml -g 0.0,0.3,0.0 -r 0.04,0.15,0.0 -m 1.0 -q | tee logs/stress_v6_heavy.log

help: $(TARGET)
	./$(TARGET) -h

clean:
	rm -f $(TARGET)
