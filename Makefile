CC      ?= cc
CFLAGS  ?= -std=c11 -O3 -Wall -Wextra -Wpedantic
LDFLAGS ?= -lm

UNAME_M := $(shell uname -m)
ifneq (,$(filter aarch64 arm64 armv7l armv8l,$(UNAME_M)))
    CFLAGS += -mtune=native
endif

TARGET  := mini_blender
SRC     := src/mini_blender.c

.PHONY: all clean run stress help

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) -f 0 -n 12 -c 24 -o renders

stress: $(TARGET)
	./$(TARGET) -f 0 -n 24 -c 48 -o renders -s scenes/blender_like_scene.xml | tee logs/stress_v4.log

help: $(TARGET)
	./$(TARGET) -h

clean:
	rm -f $(TARGET)
