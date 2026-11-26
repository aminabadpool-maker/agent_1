CC      ?= cc
CFLAGS  ?= -std=c11 -O3 -Wall -Wextra -Wpedantic
LDFLAGS ?= -lm

UNAME_M := $(shell uname -m)
ifneq (,$(filter aarch64 arm64 armv7l armv8l,$(UNAME_M)))
    CFLAGS += -mtune=native
endif

TARGET    := mini_blender
PI_TARGET := mini_blender_pi
SRC       := src/mini_blender.c

.PHONY: all desktop pi clean run stress shell test help

all: desktop

desktop: $(SRC)
	$(CC) $(CFLAGS) $< -o $(TARGET) $(LDFLAGS)

pi: $(SRC)
	$(CC) $(CFLAGS) -DMB_NO_PREVIEW $< -o $(PI_TARGET) $(LDFLAGS)

run: desktop
	./$(TARGET) -f 0 -n 12 -c 24 -o renders -p

shell: desktop
	./$(TARGET) -i

stress: pi
	./$(PI_TARGET) -f 0 -n 80 -c 80 -o renders -s scenes/heavy_stress_scene.xml -g 0.0,0.3,0.0 -r 0.04,0.15,0.0 -m 1.0 -q | tee logs/stress_v7_pi.log

test: desktop pi
	./tests/run_all_tests.sh

help: desktop
	./$(TARGET) -h

clean:
	rm -f $(TARGET) $(PI_TARGET)
